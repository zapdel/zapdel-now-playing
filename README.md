# Now Playing — OBS Plugin

An OBS Studio source plugin for Windows that displays your currently playing media as a fully themeable overlay. Reads track metadata directly from the Windows System Media Transport Controls (SMTC), so it works with any app that reports to Windows: Spotify, Chrome, Edge, Windows Media Player, Tidal, and more.

Themes are plain HTML, CSS, and JavaScript files — no recompiling required. Swap themes, edit colours, or build your own. The plugin appears as a single source in OBS; no separate browser source setup needed.

---

## Features

- Works with any app that reports to Windows SMTC — Spotify, Tidal, Chrome, Edge, and more
- Displays track title, artist, album, album art, and a live progress bar
- Pause indicator shown over album art when playback is paused
- Two visibility modes: always visible, or slide in briefly on each track change
- Fully themeable — themes are plain HTML/CSS/JS files
- Three built-in themes included
- Multiple instances supported — run different themes on different scenes simultaneously

---

## Built-in Themes

| Theme | Default Size | Description |
|-------|-------------|-------------|
| **Default** | 800 × 120 | Horizontal bar — album art on the left, track info on the right. Slides up from the bottom edge. |
| **Art Top** | 400 × 500 | Square album art above a compact info strip. |
| **Compact** | 400 × 400 | Full-bleed album art with a dark gradient overlay and metadata at the bottom. |

---

## Requirements

- OBS Studio 29 or later (Windows)
- Windows 10 or later
- obs-browser plugin (ships with OBS by default)

---

## Installation

1. Download the latest installer (`zapdel-now-playing-x.x.x-windows-x64-installer.exe`) from the [Releases](../../releases) page.
2. Run the installer — it places the plugin DLL and data files in your OBS installation automatically.
3. Restart OBS Studio.
4. In any scene, click **Add Source → Now Playing**.

### Manual install (ZIP)

If you prefer not to use the installer, download the `.zip` release and extract its contents into your OBS installation directory, keeping the folder structure intact:

```
obs-studio/
  obs-plugins/64bit/zapdel-now-playing.dll
  data/obs-plugins/zapdel-now-playing/
    locale/en-US.ini
    themes/...
```

---

## Usage

After adding a Now Playing source, open its **Properties** to configure it:

| Property | Description |
|----------|-------------|
| Width / Height | Canvas size in pixels. Changing the theme resets these to that theme's recommended defaults. |
| Theme | Choose from installed themes. |
| Show album art | Toggle album artwork display. |
| Show progress bar | Toggle the playback progress bar. |
| Visibility | **Always visible** — shows whenever something is playing. **Show on track change** — slides in briefly when the track changes, then hides. |
| Open Themes Folder | Opens the themes directory so you can add or edit themes without hunting for the path. |

> **Note:** Progress bar and playback position rely on the source app reporting timeline data to Windows. Most apps do, but some (older or less common players) may not.

---

## Custom Themes

Themes live in the `themes` folder (use the **Open Themes Folder** button in properties to find it). Each theme is a subdirectory containing at minimum an `index.html`.

The plugin ships with `np-core.js` — a shared script that handles all data updates, marquee scrolling, album art, progress bar, and visibility logic for standard element IDs (`#np-title`, `#np-artist`, `#np-album`, etc.). Using it, a theme can be just HTML and CSS with no JavaScript required.

For the full API reference and a minimal example, see **[docs/theme-authoring.md](docs/theme-authoring.md)**.

### Quick start

```
themes/
└── my-theme/
    ├── index.html
    ├── style.css
    ├── theme.js       (optional — theme-specific behaviour)
    └── theme.json     (optional — display name and metadata)
```

`theme.json` example:
```json
{
    "name": "My Theme",
    "author": "you",
    "version": "1.0.0",
    "description": "A brief description.",
    "defaultWidth": 800,
    "defaultHeight": 120
}
```

---

## Building from Source

```bash
git clone --recursive https://github.com/<your-username>/zapdel-now-playing.git
cd zapdel-now-playing
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

**Requirements:** Visual Studio 2022, Windows SDK 10.0.22000.0+, CMake 3.24+.

The build system fetches OBS headers and libraries automatically via `buildspec.json`.

### Releases

Push a semver tag (e.g. `1.0.0`) to `master` and GitHub Actions will build the project and create a draft release with a Windows installer and ZIP attached.

```bash
git tag 1.0.0
git push origin 1.0.0
```

---

## License

This plugin is licensed under the **GNU General Public License v2.0**. See [LICENSE](LICENSE) for details.
