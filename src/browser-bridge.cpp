// browser-bridge.cpp — wraps an obs-browser source and injects JS state updates

#include "browser-bridge.h"

#define NOMINMAX
#include <Windows.h>

#include <obs-module.h>
#include <plugin-support.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string wstr_to_utf8(const std::wstring &ws)
{
	if (ws.empty())
		return {};
	int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (len <= 1)
		return {};
	std::string out(static_cast<size_t>(len - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, out.data(), len, nullptr, nullptr);
	return out;
}

static std::string escape_json_string(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (unsigned char c : s) {
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			if (c < 0x20) {
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			} else {
				out += static_cast<char>(c);
			}
		}
	}
	return out;
}

// Extract an integer value from a minimal JSON string by key name.
// Returns true and sets out if found; false otherwise.
static bool read_json_int(const std::string &content, const char *key, int &out)
{
	std::string search = std::string("\"") + key + "\"";
	auto pos = content.find(search);
	if (pos == std::string::npos)
		return false;
	pos = content.find(':', pos + search.size());
	if (pos == std::string::npos)
		return false;
	pos++;
	while (pos < content.size() &&
	       (content[pos] == ' ' || content[pos] == '\t' || content[pos] == '\r' || content[pos] == '\n'))
		pos++;
	if (pos >= content.size() || !std::isdigit((unsigned char)content[pos]))
		return false;
	out = std::stoi(content.substr(pos));
	return true;
}

// Read the "name" field from a theme.json file.
// Returns the directory name if the file is absent or the field is missing.
static std::string read_theme_name(const std::filesystem::path &theme_dir)
{
	std::filesystem::path json_path = theme_dir / "theme.json";
	if (!std::filesystem::exists(json_path))
		return theme_dir.filename().string();

	std::ifstream f(json_path);
	if (!f.is_open())
		return theme_dir.filename().string();

	std::string content((std::istreambuf_iterator<char>(f)), {});

	// Minimal "name" extraction — find "name" key and read its string value
	auto pos = content.find("\"name\"");
	if (pos == std::string::npos)
		return theme_dir.filename().string();

	pos = content.find('"', pos + 6); // opening quote of value
	if (pos == std::string::npos)
		return theme_dir.filename().string();
	pos++;
	auto end = content.find('"', pos);
	if (end == std::string::npos)
		return theme_dir.filename().string();

	return content.substr(pos, end - pos);
}

// ---------------------------------------------------------------------------
// BrowserBridge
// ---------------------------------------------------------------------------

bool BrowserBridge::Create(obs_data_t *settings)
{
	ApplySettings(settings);

	std::string url = ThemeURL(m_theme_id);
	//obs_log(LOG_INFO, "[BrowserBridge] Creating browser source, URL: %s", url.c_str());

	obs_data_t *browser_settings = obs_data_create();
	obs_data_set_string(browser_settings, "url", url.c_str());
	obs_data_set_int(browser_settings, "width", static_cast<long long>(m_width));
	obs_data_set_int(browser_settings, "height", static_cast<long long>(m_height));
	obs_data_set_bool(browser_settings, "fps_custom", false);
	obs_data_set_bool(browser_settings, "shutdown", false);
	obs_data_set_bool(browser_settings, "restart_when_active", false);

	m_browser_source = obs_source_create_private(
		"browser_source", "zapdel_np_browser_internal", browser_settings);

	obs_data_release(browser_settings);

	if (!m_browser_source) {
		obs_log(LOG_WARNING,
			"[BrowserBridge] obs_source_create_private('browser_source') returned null — "
			"is the obs-browser plugin installed and loaded?");
		return false;
	}

	//obs_log(LOG_INFO, "[BrowserBridge] Browser source created OK (%p)", (void *)m_browser_source);

	// inc_active triggers activate() (CEF init) AND show() (WasHidden(false))
	// on the browser source's next video tick. A single call covers both.
	obs_source_inc_active(m_browser_source);

	InjectCSSVars();
	return true;
}

void BrowserBridge::Destroy()
{
	if (m_browser_source) {
		obs_source_dec_active(m_browser_source);
		obs_source_release(m_browser_source);
		m_browser_source = nullptr;
	}
}

void BrowserBridge::Update(obs_data_t *settings)
{
	ApplySettings(settings);

	if (!m_browser_source)
		return;

	// Resize the browser source
	obs_data_t *browser_settings = obs_source_get_settings(m_browser_source);
	obs_data_set_string(browser_settings, "url", ThemeURL(m_theme_id).c_str());
	obs_data_set_int(browser_settings, "width", static_cast<long long>(m_width));
	obs_data_set_int(browser_settings, "height", static_cast<long long>(m_height));
	obs_source_update(m_browser_source, browser_settings);
	obs_data_release(browser_settings);

	InjectCSSVars();
}

void BrowserBridge::PushState(const NowPlayingState &state)
{
	if (!m_browser_source)
		return;

	std::string json = BuildJSPayload(state);

	if (!m_logged_first_push) {
		//obs_log(LOG_INFO, "[BrowserBridge] First PushState, payload: %s", json.c_str());
		m_logged_first_push = true;
	}

	proc_handler_t *ph = obs_source_get_proc_handler(m_browser_source);
	if (!ph) {
		if (!m_logged_no_proc) {
			obs_log(LOG_WARNING, "[BrowserBridge] No proc_handler on browser source");
			m_logged_no_proc = true;
		}
		return;
	}

	// obs-browser dispatches a DOM CustomEvent: window.dispatchEvent(new CustomEvent(eventName, {detail: JSON.parse(jsonString)}))
	calldata_t cd = {};
	calldata_init(&cd);
	calldata_set_string(&cd, "eventName", "obsNowPlayingUpdate");
	calldata_set_string(&cd, "jsonString", json.c_str());
	bool ok = proc_handler_call(ph, "javascript_event", &cd);
	calldata_free(&cd);

	if (!ok && !m_logged_no_proc) {
		obs_log(LOG_WARNING, "[BrowserBridge] javascript_event proc call failed — "
			"update() will not reach the theme");
		m_logged_no_proc = true;
	}
}

void BrowserBridge::Render(gs_effect_t *effect)
{
	if (!m_browser_source) {
		if (!m_logged_no_source) {
			obs_log(LOG_WARNING, "[BrowserBridge] Render called but m_browser_source is null");
			m_logged_no_source = true;
		}
		return;
	}

	// Log browser frame size at render 1, 60, 300 to diagnose whether CEF is producing frames.
	// A 0x0 size means no frames have been submitted yet.
	m_render_count++;
	if (m_render_count == 1 || m_render_count == 60 || m_render_count == 300) {
		uint32_t bw = obs_source_get_width(m_browser_source);
		uint32_t bh = obs_source_get_height(m_browser_source);
		/*
		obs_log(LOG_INFO,
			"[BrowserBridge] Render #%d: browser frame size %dx%d (active=%d showing=%d)",
			m_render_count, bw, bh,
			(int)obs_source_active(m_browser_source),
			(int)obs_source_showing(m_browser_source));
		*/
	}

	obs_source_video_render(m_browser_source);
	UNUSED_PARAMETER(effect);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void BrowserBridge::ApplySettings(obs_data_t *settings)
{
	m_width           = static_cast<uint32_t>(obs_data_get_int(settings, "width"));
	m_height          = static_cast<uint32_t>(obs_data_get_int(settings, "height"));
	m_show_album_art  = obs_data_get_bool(settings, "show_album_art");
	m_show_progress   = obs_data_get_bool(settings, "show_progress");
	m_visibility_mode = obs_data_get_string(settings, "visibility_mode");

	const char *theme = obs_data_get_string(settings, "theme");
	m_theme_id        = theme ? theme : "default";

	if (m_width  == 0) m_width  = 800;
	if (m_height == 0) m_height = 120;
}

void BrowserBridge::InjectCSSVars()
{
	// CSS variables (--np-width, --np-height) are now set by the theme
	// from the width/height fields in the obsNowPlayingUpdate payload.
	// No separate injection needed.
}

std::string BrowserBridge::BuildJSPayload(const NowPlayingState &state) const
{
	std::ostringstream ss;
	// clang-format off
	ss << "{"
	   << "\"title\":\""       << escape_json_string(wstr_to_utf8(state.title))    << "\","
	   << "\"artist\":\""      << escape_json_string(wstr_to_utf8(state.artist))   << "\","
	   << "\"album\":\""       << escape_json_string(wstr_to_utf8(state.album))    << "\","
	   << "\"appId\":\""       << escape_json_string(wstr_to_utf8(state.app_id))   << "\","
	   << "\"isPlaying\":"     << (state.is_playing ? "true" : "false")            << ","
	   << "\"progress\":"      << state.progress                                    << ","
	   << "\"thumbnail\":\""   << escape_json_string(state.thumbnail_b64)          << "\","
	   << "\"showAlbumArt\":"  << (m_show_album_art  ? "true" : "false")           << ","
	   << "\"showProgress\":"  << (m_show_progress   ? "true" : "false")           << ","
	   << "\"visibilityMode\":\"" << escape_json_string(m_visibility_mode)         << "\","
	   << "\"hasNewTrack\":"   << (state.has_new_track ? "true" : "false")         << ","
	   << "\"width\":"         << m_width                                           << ","
	   << "\"height\":"        << m_height
	   << "}";
	// clang-format on
	return ss.str();
}

std::string BrowserBridge::ThemeURL(const std::string &theme_id) const
{
	char *path = obs_module_file(("themes/" + theme_id + "/index.html").c_str());
	if (!path)
		return {};

	// obs_module_file() may return a relative path for system plugin installs.
	// Resolve to absolute so file:// URLs work in CEF.
	std::filesystem::path abs = std::filesystem::absolute(path);
	bfree(path);

	// Build file:// URL: forward slashes, spaces percent-encoded
	std::string url;
	url.reserve(abs.string().size() + 8);
	url = "file:///";
	for (char c : abs.string()) {
		if (c == '\\')
			url += '/';
		else if (c == ' ')
			url += "%20";
		else
			url += c;
	}
	return url;
}

// ---------------------------------------------------------------------------
// Theme list population — called from now-playing-source get_properties
// ---------------------------------------------------------------------------

extern "C" void browser_bridge_populate_theme_list(obs_property_t *list)
{
	char *themes_path_raw = obs_module_file("themes");
	if (!themes_path_raw)
		return;

	std::filesystem::path themes_dir(themes_path_raw);
	bfree(themes_path_raw);

	if (!std::filesystem::exists(themes_dir) ||
	    !std::filesystem::is_directory(themes_dir))
		return;

	for (auto &entry : std::filesystem::directory_iterator(themes_dir)) {
		if (!entry.is_directory())
			continue;
		if (!std::filesystem::exists(entry.path() / "index.html"))
			continue;

		std::string dir_name     = entry.path().filename().string();
		std::string display_name = read_theme_name(entry.path());
		obs_property_list_add_string(list, display_name.c_str(), dir_name.c_str());
	}
}

// Read defaultWidth / defaultHeight from a theme's theme.json.
// Returns true if both fields were found; out_width and out_height are set.
// Returns false if the file is absent or either field is missing.
extern "C" bool browser_bridge_get_theme_defaults(const char *theme_id, int *out_width,
						   int *out_height)
{
	if (!theme_id || !out_width || !out_height)
		return false;

	char *raw = obs_module_file(("themes/" + std::string(theme_id) + "/theme.json").c_str());
	if (!raw)
		return false;

	std::filesystem::path json_path(raw);
	bfree(raw);

	if (!std::filesystem::exists(json_path))
		return false;

	std::ifstream f(json_path);
	if (!f.is_open())
		return false;

	std::string content((std::istreambuf_iterator<char>(f)), {});

	int w = 0, h = 0;
	if (!read_json_int(content, "defaultWidth", w) || !read_json_int(content, "defaultHeight", h))
		return false;
	*out_width  = w;
	*out_height = h;
	return true;
}
