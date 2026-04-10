# zapdel-now-playing

**Project name:** zapdel's Now Playing Plugin
**OBS display name:** Now Playing (appears as "Now Playing" in the OBS Add Source menu)
**Repo:** `zapdel-now-playing`

A native OBS Studio source plugin for Windows that displays currently playing media as a fully themeable overlay. Reads metadata from the Windows System Media Transport Controls (SMTC/WinRT), so it works with any app that reports to Windows: Spotify, Chrome, Edge, Windows Media Player, Tidal, etc.

Rendering is handled by an embedded browser (CEF via `obs-browser`) so themes are plain HTML/CSS/JS files — swappable, shareable, and editable without recompiling. The plugin appears as a single source in OBS; no separate browser source setup required.

## Project Goal

A standalone, distributable OBS plugin — not tied to StreamBrain. Users install it, add it as a source, pick a theme, and it works. Theme creators get a well-documented default theme and a clean JS data contract to build against. Power users can edit theme files directly, use AI, or hire someone on Fiverr.

---

## Tech Stack

- **Language:** C++17 (C++20 where WinRT/cppwinrt requires it)
- **Build system:** CMake, using `obs-plugintemplate` as the base
- **OBS API:** libobs + `obs-browser` plugin API for CEF embedding
- **WinRT interop:** C++/WinRT (`winrt/Windows.Media.Control.h`) for SMTC
- **Rendering:** CEF (Chromium Embedded Framework) via `obs-browser` — themes are HTML/CSS/JS
- **UI:** OBS native properties panel (`obs_properties_t`) — no Qt UI files
- **Platform:** Windows only (SMTC is Windows 10+ exclusive); Linux/macOS out of scope

### Key dependency: `obs-browser`
OBS ships with `obs-browser` by default. The plugin links against it and uses its API to create and control an embedded browser panel as part of the source. If `obs-browser` is not present, the plugin logs an error and the source renders nothing (fail gracefully, never crash OBS).

---

## Architecture

```
obs-now-playing/
├── CMakeLists.txt
├── buildspec.json
├── CLAUDE.md
├── src/
│   ├── plugin-main.cpp          # OBS_DECLARE_MODULE, obs_module_load, registers source
│   ├── now-playing-source.cpp   # obs_source_info: create/destroy/properties/update
│   ├── now-playing-source.h
│   ├── smtc-reader.cpp          # WinRT/SMTC polling + event subscriptions
│   ├── smtc-reader.h            # Exposes NowPlayingState snapshot (thread-safe)
│   ├── browser-bridge.cpp       # Wraps obs-browser source; injects JS state updates
│   └── browser-bridge.h
├── data/
│   ├── locale/
│   │   └── en-US.ini            # All UI strings; never hardcode display text
│   └── themes/
│       └── default/
│           ├── index.html       # Default theme — also the reference for theme creators
│           ├── style.css
│           └── theme.json       # Theme metadata: name, author, version, description
├── .github/
│   └── workflows/
│       └── build.yml
└── docs/
    └── theme-authoring.md       # How to create a theme; documents the JS data contract
```

---

## Data Flow

```
[SMTC (Windows)]
      │  MediaPropertiesChanged / PlaybackInfoChanged events
      ▼
[SMTCReader]  ──→  NowPlayingState snapshot (mutex-protected)
      │
      ▼
[NowPlayingSource::video_tick()]
      │  polls snapshot each frame; detects changes
      ▼
[BrowserBridge::PushState()]
      │  calls obs_browser_source_call_js() with JSON payload
      ▼
[Theme JS]  ──→  receives window.__nowPlaying.update(payload)
      │
      ▼
[CEF renders frame]  ──→  OBS composites it as a source texture
```

State is pushed to the theme via JS injection, not polling. The theme does not make HTTP requests or access the internet.

---

## OBS Source Registration

Registers a single source of type `OBS_SOURCE_TYPE_INPUT`.

Key `obs_source_info` fields:
- `id`: `"zapdel_now_playing"` — namespaced to avoid collisions in the OBS source registry
- `display_name`: returns `obs_module_text("NowPlaying.SourceName")` → `"Now Playing"` (defined in `en-US.ini`)
- `type`: `OBS_SOURCE_TYPE_INPUT`
- `output_flags`: `OBS_SOURCE_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE`
- `get_width` / `get_height`: return values from the `width` / `height` properties
- `video_tick`: polls SMTCReader; detects state changes; calls BrowserBridge if changed
- `video_render`: delegates to the embedded browser source's render call
- `get_properties`: returns the properties panel definition
- `update`: called when user changes a property; updates BrowserBridge and browser source size

`OBS_SOURCE_DO_NOT_DUPLICATE` is set because the embedded browser source cannot be safely cloned.

### Multiple simultaneous instances

Multiple instances of this source are explicitly supported and a primary use case — e.g. a compact corner widget on a game scene set to `on_track_change`, and a full-width always-visible bar on a Just Chatting scene, each with a different theme.

Each instance owns its own `SMTCReader` and `BrowserBridge`. There must be **no shared static state between instances** — no global pointers, no singleton SMTCReader, no shared browser handle. Every resource is allocated in `create` and released in `destroy` on a per-instance basis. This must be maintained as an invariant throughout development.

---

## SMTC Reader (`smtc-reader`)

Uses `GlobalSystemMediaTransportControlsSessionManager` via C++/WinRT.

- Initialises on a dedicated background thread; `winrt::init_apartment(winrt::apartment_type::multi_threaded)`
- Subscribes to `CurrentSessionChanged` and `SessionsChanged` on the manager
- On session change: re-subscribes to `MediaPropertiesChanged` and `PlaybackInfoChanged`
- Thumbnail decoded from `IRandomAccessStreamReference` → raw RGBA bytes, then base64-encoded PNG, cached until track changes
- Exposes a thread-safe snapshot:

```cpp
struct NowPlayingState {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring app_id;          // SourceAppUserModelId, e.g. "Spotify.exe"
    bool         is_playing;
    double       progress;        // 0.0–1.0, derived from timeline position/end
    std::string  thumbnail_b64;   // Base64-encoded PNG data URI, empty if no art
    bool         has_new_track;   // true for one snapshot cycle after track title changes
};
```

- `GetSnapshot()` returns a copy under `std::mutex` lock — safe to call from any thread
- If no session active: empty strings, `is_playing = false`, `progress = 0.0`

### Known SMTC Quirks
- **Spotify blank metadata flash:** Spotify briefly reports empty title/artist during track transitions. Strategy: only update the "current track" fields when the incoming title is non-empty. Keep showing the last known good track until a confirmed non-empty replacement arrives.
- **Thumbnail async decode:** Do not block the render thread. Decode thumbnail on the SMTC background thread; store result in snapshot. Theme receives it as a base64 data URI — CEF renders `<img src="data:image/png;base64,...">` natively.
- **Progress availability:** Not all apps report timeline data. `progress` defaults to `0.0`. Themes must handle `0.0` gracefully (e.g. hide the progress bar if `progress` is `0.0` and `isPlaying` is false).

---

## Browser Bridge (`browser-bridge`)

Thin wrapper around the `obs-browser` plugin API.

- On `create`: instantiates an internal browser source pointed at the selected theme's `index.html` as a `file://` URL
- On `update` (settings changed): resizes browser source; re-injects current settings; re-pushes last known state
- On `PushState(NowPlayingState)`: serialises to JSON and calls:

```cpp
obs_browser_source_call_js(browser_source,
    "window.__nowPlaying && window.__nowPlaying.update(" + json + ")");
```

- On `destroy`: releases browser source ref

### CSS Variable Injection

On page load and whenever settings change, the bridge injects width and height as CSS variables so themes can lay out responsively within the canvas:

```javascript
document.documentElement.style.setProperty('--np-width',  '800px');
document.documentElement.style.setProperty('--np-height', '120px');
```

These are the only values the plugin injects into CSS. All colours, fonts, spacing, and animation styles belong to the theme.

---

## Properties Panel

Intentionally minimal — themes own all visual styling.

| Property key       | Type | Default            | Description |
|--------------------|------|--------------------|-------------|
| `width`            | INT  | 800                | Source canvas width (px) |
| `height`           | INT  | 120                | Source canvas height (px) |
| `theme`            | LIST | `"default"`        | Dropdown populated from `data/themes/` subdirectories |
| `show_album_art`   | BOOL | true               | Passed to theme as `showAlbumArt` in JS payload |
| `show_progress`    | BOOL | true               | Passed to theme as `showProgress` in JS payload |
| `visibility_mode`  | LIST | `"always"`         | `"always"` or `"on_track_change"` |

**`visibility_mode`:** when set to `"on_track_change"`, the plugin passes this in the payload and the **theme is entirely responsible** for show/hide timing and animation duration. There is no plugin-level duration property — this is an intentional design decision so each theme can implement its own animation cadence without being constrained by a global timer.

**Theme discovery:** on `get_properties`, scan `data/themes/` for subdirectories containing `index.html`. Populate the `theme` list from `theme.json` `name` fields (fall back to directory name if `theme.json` is absent or malformed).

---

## JS Data Contract (Theme API)

Themes must expose `window.__nowPlaying.update(payload)`. The plugin calls this on every state change. Themes must tolerate missing or null fields without throwing.

```javascript
window.__nowPlaying = {
  update(payload) { /* ... */ }
};
```

### Payload schema

```json
{
  "title":           "string — track title, empty string if nothing playing",
  "artist":          "string — artist name",
  "album":           "string — album name",
  "appId":           "string — source app identifier, e.g. 'Spotify.exe'",
  "isPlaying":       true,
  "progress":        0.42,
  "thumbnail":       "data:image/png;base64,... OR empty string",
  "showAlbumArt":    true,
  "showProgress":    true,
  "visibilityMode":  "always",
  "displayDuration": 8
}
```

**Field notes:**
- `title` being an empty string means nothing is currently playing
- `progress` is `0.0`–`1.0`; may be `0.0` even when playing if the app doesn't report timeline data
- `thumbnail` is always a string (data URI or empty) — never null
- `showAlbumArt` / `showProgress` are booleans — theme must show/hide those elements accordingly
- `visibilityMode` and `displayDuration` are passed so the **theme drives its own show/hide animation**. The plugin does not manipulate source visibility or opacity — the theme handles this entirely based on the payload values

### Theme lifecycle

```javascript
// Optional. Called once after CEF finishes loading, before the first update().
// Use for one-time setup. If not defined, plugin falls back to calling update() on load.
window.__nowPlaying.init = function(initialPayload) { };
```

---

## Default Theme

Located at `data/themes/default/`. Serves two purposes: a working Now Playing widget, and the canonical reference for theme authors.

### The default theme must:
- Define all colour values as CSS custom properties near the top of `style.css`, with clear comments — this is the primary customisation surface for users who edit files directly
- Visibly respond to `showAlbumArt` (show/hide album art element)
- Visibly respond to `showProgress` (show/hide progress bar element)
- Implement `on_track_change` behaviour: when `visibilityMode === 'on_track_change'`, animate in when a new track starts (`has_new_track` equivalent detected via title change), stay visible for `displayDuration` seconds, then animate out
- Include at least one entrance animation and one exit animation — the default should be a broadcast bottom-third style: slides up from the bottom edge, holds, slides back down
- Handle empty `title` gracefully (show a placeholder or be invisible)
- Handle missing thumbnail (show a placeholder icon or collapse the art area)
- Scroll long titles that overflow available width using a CSS marquee animation (not JS)
- Use a transparent background so it composites cleanly over any OBS scene

### `theme.json` format
```json
{
  "name":             "Default",
  "author":           "zapdel",
  "version":          "1.0.0",
  "description":      "The default theme. Also a reference for theme creators.",
  "minPluginVersion": "1.0.0"
}
```

---

## Build Setup

Based on `obs-plugintemplate`. Do not diverge from template conventions without a documented reason.

```bash
git clone --recursive https://github.com/obsproject/obs-plugintemplate.git zapdel-now-playing
cd zapdel-now-playing
# Edit buildspec.json: name → "zapdel-now-playing", author → "zapdel", website → your repo URL
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

**Windows requirements:**
- Visual Studio 2022 (MSVC v143)
- Windows SDK 10.0.22000.0+ (for WinRT `Windows.Media.Control`)
- OBS Studio installed (CMake scripts in the template fetch headers/libs via `buildspec.json`)

**WinRT linkage — add to `CMakeLists.txt`:**
```cmake
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /std:c++20)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE WindowsApp.lib)
```

**obs-browser linkage:**
Link against the `obs-browser` import lib. It ships with OBS; locate it via the OBS install path in CMake. Check `obs-plugintemplate` issues/discussions for the current recommended pattern — this has changed across OBS major versions and the template may have updated guidance.

---

## Coding Conventions

- C++17 minimum; C++20 only where WinRT requires it
- Use `winrt::` namespace prefix explicitly — do not `using namespace winrt`
- No exceptions across OBS callback boundaries — catch at the boundary, log via `obs_log()`
- All user-visible strings in `data/locale/en-US.ini`, referenced via `obs_module_text()`
- Property key constants as `#define PROP_WIDTH "width"` etc. in `now-playing-source.h`
- State pushed to browser only on actual change — compare snapshots before calling `PushState()`
- Never allocate on the render thread; pre-allocate in `create`, resize on settings change only

---

## Not In Scope (v1)

- macOS / Linux
- Playback control (play/pause/skip) — read-only
- Multi-session picker (always uses SMTC current session)
- Plugin-hosted HTTP server (state is injected via JS, not polled)
- StreamBrain integration

---

## Key References

- OBS Plugin Template: https://github.com/obsproject/obs-plugintemplate
- OBS Plugin Docs: https://docs.obsproject.com/plugins
- OBS Source API: https://docs.obsproject.com/reference-sources
- obs-browser plugin (CEF embedding API): https://github.com/obsproject/obs-browser
- SMTC C++ walkthrough: https://devblogs.microsoft.com/oldnewthing/20231108-00/?p=108980
- GlobalSystemMediaTransportControlsSession API: https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssession
- C++/WinRT docs: https://learn.microsoft.com/en-us/windows/uwp/cpp-and-winrt-apis/
