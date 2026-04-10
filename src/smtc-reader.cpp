// smtc-reader.cpp — Windows System Media Transport Controls reader
// Compiled as C++20 (required for WinRT coroutines)

#include "smtc-reader.h"

// WinRT includes — must come before any Windows.h included without NOMINMAX
#define NOMINMAX
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <obs-module.h>
#include <plugin-support.h>

namespace wmc = winrt::Windows::Media::Control;
namespace wss = winrt::Windows::Storage::Streams;

// ---------------------------------------------------------------------------
// Base64 encoder
// ---------------------------------------------------------------------------

static const char kB64Table[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const uint8_t *data, size_t len)
{
	std::string out;
	out.reserve(((len + 2) / 3) * 4);
	for (size_t i = 0; i < len; i += 3) {
		uint32_t n = static_cast<uint32_t>(data[i]) << 16;
		if (i + 1 < len)
			n |= static_cast<uint32_t>(data[i + 1]) << 8;
		if (i + 2 < len)
			n |= static_cast<uint32_t>(data[i + 2]);
		out += kB64Table[(n >> 18) & 0x3F];
		out += kB64Table[(n >> 12) & 0x3F];
		out += (i + 1 < len) ? kB64Table[(n >> 6) & 0x3F] : '=';
		out += (i + 2 < len) ? kB64Table[n & 0x3F] : '=';
	}
	return out;
}

// ---------------------------------------------------------------------------
// SMTCReader::Impl — lives on a dedicated background thread
// ---------------------------------------------------------------------------

struct SMTCReader::Impl : std::enable_shared_from_this<Impl> {
	mutable std::mutex state_mutex;
	NowPlayingState    state;

	std::thread  bg_thread;
	std::atomic<bool> running{false};
	HANDLE stop_event = nullptr;

	wmc::GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
	wmc::GlobalSystemMediaTransportControlsSession current_session{nullptr};

	winrt::event_token sessions_changed_token;
	winrt::event_token media_props_token;
	winrt::event_token playback_info_token;

	// Timeline data for live progress calculation
	int64_t  m_position_ticks = 0;   // position at last PlaybackInfoChanged (100-ns units)
	int64_t  m_end_ticks      = 0;   // track duration (100-ns units); 0 = unknown
	winrt::Windows::Foundation::DateTime m_position_time{}; // when position was sampled

	Impl()
	{
		stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		running    = true;
		bg_thread  = std::thread([this] { run(); });
	}

	~Impl()
	{
		running = false;
		if (stop_event) {
			SetEvent(stop_event);
		}
		if (bg_thread.joinable()) {
			bg_thread.join();
		}
		if (stop_event) {
			CloseHandle(stop_event);
			stop_event = nullptr;
		}
	}

	void run()
	{
		try {
			winrt::init_apartment(winrt::apartment_type::multi_threaded);

			manager = wmc::GlobalSystemMediaTransportControlsSessionManager::
					  RequestAsync()
					  .get();

			sessions_changed_token = manager.SessionsChanged(
				[this](auto &&, auto &&) { on_sessions_changed(); });

			on_sessions_changed();

			WaitForSingleObject(stop_event, INFINITE);

			// Clean up subscriptions before the apartment is torn down
			cleanup_session();
			if (manager) {
				try {
					manager.SessionsChanged(sessions_changed_token);
				} catch (...) {
				}
				manager = nullptr;
			}

			winrt::uninit_apartment();
		} catch (...) {
			obs_log(LOG_WARNING, "[SMTCReader] Background thread initialisation failed");
		}
	}

	void cleanup_session()
	{
		if (!current_session)
			return;
		try {
			current_session.MediaPropertiesChanged(media_props_token);
			current_session.PlaybackInfoChanged(playback_info_token);
		} catch (...) {
		}
		current_session = nullptr;
	}

	void on_sessions_changed()
	{
		try {
			auto new_session = manager.GetCurrentSession();
			if (new_session == current_session)
				return;

			obs_log(LOG_INFO, "[SMTCReader] Session changed");
			cleanup_session();
			current_session = new_session;

			if (!current_session) {
				obs_log(LOG_INFO, "[SMTCReader] No active session");
				std::lock_guard<std::mutex> lock(state_mutex);
				state = NowPlayingState{};
				return;
			}

			media_props_token = current_session.MediaPropertiesChanged(
				[this](auto &&, auto &&) { refresh_media_properties(); });
			playback_info_token = current_session.PlaybackInfoChanged(
				[this](auto &&, auto &&) { refresh_playback_info(); });

			refresh_media_properties();
			//refresh_playback_info();
		} catch (...) {
			obs_log(LOG_WARNING, "[SMTCReader] on_sessions_changed exception");
		}
	}

	void refresh_media_properties()
	{
		if (!current_session)
			return;
		try {
			auto props = current_session.TryGetMediaPropertiesAsync().get();
			if (!props) {
				obs_log(LOG_INFO, "[SMTCReader] MediaProperties: props null");
				return;
			}

			std::wstring new_title  = std::wstring(props.Title());
			std::wstring new_artist = std::wstring(props.Artist());
			std::wstring new_album  = std::wstring(props.AlbumTitle());

			// Spotify blank metadata quirk: skip empty title during transitions
			if (new_title.empty()) {
				obs_log(LOG_INFO, "[SMTCReader] MediaProperties: skipping empty title");
				return;
			}

			obs_log(LOG_INFO, "[SMTCReader] MediaProperties: title=%s",
				new_title == state.title ? "(same)" : "(changed)");

			bool track_changed;

			{
				std::lock_guard<std::mutex> lock(state_mutex);
				track_changed    = (new_title != state.title);
				state.title      = new_title;
				state.artist     = new_artist;
				state.album      = new_album;
				state.has_new_track = track_changed;

				try {
					state.app_id = std::wstring(
						current_session.SourceAppUserModelId());
				} catch (...) {
				}

				if (track_changed) {
					state.thumbnail_b64.clear();
					// Reset position/duration so GetSnapshot() returns 0% until
					// PlaybackInfoChanged fires with fresh data for the new track.
					m_position_ticks = 0;
					m_end_ticks      = 0;
				}
			}

			if (track_changed) {
				fetch_thumbnail();
			}
			refresh_playback_info();
		} catch (...) {
			obs_log(LOG_WARNING, "[SMTCReader] refresh_media_properties exception");
		}
	}

	void fetch_thumbnail()
	{
		try {
			// Re-fetch properties after a short delay. MediaPropertiesChanged can
			// fire before the app has updated the thumbnail in SMTC, so the
			// thumb_ref captured at event time may still point to the old image.
			std::this_thread::sleep_for(std::chrono::milliseconds(500));

			if (!current_session)
				return;
			auto props = current_session.TryGetMediaPropertiesAsync().get();
			if (!props)
				return;
			auto thumb_ref = props.Thumbnail();
			if (!thumb_ref)
				return;

			// OpenReadAsync().get() is safe on our dedicated MTA background thread
			auto stream = thumb_ref.OpenReadAsync().get();
			if (!stream)
				return;

			uint64_t size = stream.Size();
			if (size == 0 || size > 10u * 1024u * 1024u) // skip empty or >10 MB
				return;

			// Derive MIME type from the stream's content type header
			std::string mime = "image/jpeg"; // safe default for most album art
			winrt::hstring ct = stream.ContentType();
			if (!ct.empty()) {
				std::string cts = winrt::to_string(ct);
				if (cts.find("png") != std::string::npos)
					mime = "image/png";
				else if (cts.find("webp") != std::string::npos)
					mime = "image/webp";
			}

			// Read all bytes synchronously
			wss::DataReader reader(stream.GetInputStreamAt(0));
			uint32_t loaded = reader.LoadAsync(static_cast<uint32_t>(size)).get();
			if (loaded == 0)
				return;

			std::vector<uint8_t> buf(loaded);
			reader.ReadBytes(buf);
			reader.DetachStream(); // don't let DataReader close the stream on destruct

			std::string b64 = "data:" + mime + ";base64," +
			                  base64_encode(buf.data(), buf.size());

			std::lock_guard<std::mutex> lock(state_mutex);
			state.thumbnail_b64 = std::move(b64);
		} catch (...) {
			obs_log(LOG_WARNING, "[SMTCReader] fetch_thumbnail exception");
		}
	}

	void refresh_playback_info()
	
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		if (!current_session)
			return;
		try {
			auto info = current_session.GetPlaybackInfo();
			if (!info)
				return;

			bool is_playing =
				info.PlaybackStatus() ==
				wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;

			// Snapshot timeline — live progress is extrapolated in GetSnapshot()
			try {
				auto timeline = current_session.GetTimelineProperties();
				if (timeline) {
					int64_t end_ticks = timeline.EndTime().count();
					if (end_ticks > 0) {
						std::lock_guard<std::mutex> lock(state_mutex);
						state.is_playing  = is_playing;
						m_end_ticks       = end_ticks;
						m_position_ticks  = timeline.Position().count();
						m_position_time   = winrt::clock::now();
						return;
					}
				}
			} catch (...) {
			}

			std::lock_guard<std::mutex> lock(state_mutex);
			state.is_playing = is_playing;
			// No timeline data: leave progress unchanged
			static_cast<void>(0);
		} catch (...) {
			obs_log(LOG_WARNING, "[SMTCReader] refresh_playback_info exception");
		}
	}
};

// ---------------------------------------------------------------------------
// SMTCReader public API
// ---------------------------------------------------------------------------

SMTCReader::SMTCReader() : m_impl(std::make_shared<Impl>()) {}

SMTCReader::~SMTCReader() = default;

NowPlayingState SMTCReader::GetSnapshot()
{
	std::lock_guard<std::mutex> lock(m_impl->state_mutex);
	NowPlayingState snap = m_impl->state;
	m_impl->state.has_new_track = false; // consume the flag

	// Extrapolate live playback position from the stored timeline snapshot.
	// SMTC only fires PlaybackInfoChanged on state transitions, so we add
	// the time elapsed since the last snapshot to get an accurate position.
	if (m_impl->m_end_ticks > 0) {
		int64_t pos = m_impl->m_position_ticks;
		if (snap.is_playing) {
			auto now     = winrt::clock::now();
			int64_t elapsed = (now - m_impl->m_position_time).count();
			// Sanity cap: don't extrapolate beyond 10 minutes of drift
			if (elapsed > 0 && elapsed < 6000LL * 10'000'000LL)
				pos += elapsed;
		}
		snap.progress = std::clamp(
			static_cast<double>(pos) / static_cast<double>(m_impl->m_end_ticks),
			0.0, 1.0);
	}

	return snap;
}
