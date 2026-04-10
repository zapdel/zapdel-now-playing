#pragma once

#include "smtc-reader.h"

#include <obs-module.h>
#include <string>

// Wraps an obs-browser source. Creates a hidden browser window pointed at the
// selected theme's index.html, and pushes NowPlayingState to it via JS injection.
// Each source instance owns its own BrowserBridge — no shared state.
class BrowserBridge {
public:
    BrowserBridge()  = default;
    ~BrowserBridge() = default;

    // Creates the internal browser source. Returns false if obs-browser is unavailable.
    bool Create(obs_data_t* settings);
    void Destroy();

    // Called when the user changes source properties. Re-applies all settings.
    void Update(obs_data_t* settings);

    // Serialises state to JSON and injects it via window.__nowPlaying.update().
    // Safe to call on video_tick thread.
    void PushState(const NowPlayingState& state);

    // Delegate video_render to the internal browser source.
    void Render(gs_effect_t* effect);

    uint32_t GetWidth()  const { return m_width; }
    uint32_t GetHeight() const { return m_height; }

    // Non-copyable
    BrowserBridge(const BrowserBridge&)            = delete;
    BrowserBridge& operator=(const BrowserBridge&) = delete;

private:
    void     ApplySettings(obs_data_t* settings);
    void     InjectCSSVars();
    std::string BuildJSPayload(const NowPlayingState& state) const;
    std::string ThemeURL(const std::string& theme_id) const;

    obs_source_t* m_browser_source  = nullptr;
    uint32_t      m_width           = 800;
    uint32_t      m_height          = 120;
    std::string   m_theme_id        = "default";
    bool          m_show_album_art  = true;
    bool          m_show_progress   = true;
    std::string   m_visibility_mode = "always";

    // One-shot log flags — prevent log spam on the render thread
    bool m_logged_first_push   = false;
    bool m_logged_no_source    = false;
    bool m_logged_no_proc      = false;

    // Render diagnostic counter — log browser frame state periodically
    int m_render_count = 0;
};
