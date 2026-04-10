#pragma once

#include <memory>
#include <mutex>
#include <string>

struct NowPlayingState {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring app_id;        // SourceAppUserModelId, e.g. L"Spotify.exe"
    bool         is_playing    = false;
    double       progress      = 0.0;   // 0.0–1.0; 0.0 if app doesn't report timeline
    std::string  thumbnail_b64;         // "data:image/png;base64,..." or empty
    bool         has_new_track = false; // true for one GetSnapshot() call after title changes
};

// Reads current playback state from Windows System Media Transport Controls (SMTC).
// Each instance owns its own WinRT apartment and event subscriptions — no shared state.
// Safe to instantiate multiple times (one per plugin source instance).
class SMTCReader {
public:
    SMTCReader();
    ~SMTCReader();

    // Returns a copy of the current state under mutex.
    // Clears has_new_track after returning it — consuming the flag.
    NowPlayingState GetSnapshot();

    // Non-copyable, non-movable (owns a thread and WinRT apartment)
    SMTCReader(const SMTCReader&)            = delete;
    SMTCReader& operator=(const SMTCReader&) = delete;

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl; // shared_ptr so fire_and_forget coroutines can extend lifetime
};
