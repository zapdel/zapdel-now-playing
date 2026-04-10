# zapdel's Now Playing — Theme Authoring Guide

Themes are plain HTML/CSS/JS files placed in `data/themes/<theme-name>/`. No build step, no framework required. Swap them in and out by selecting the theme name in OBS source properties.

---

## File structure

```
data/themes/
└── my-theme/
    ├── index.html   ← required: the page the plugin loads
    ├── style.css    ← recommended: keep styles separate
    └── theme.json   ← optional: display name and metadata
```

### theme.json (optional)

```json
{
    "name":             "My Theme",
    "author":           "you",
    "version":          "1.0.0",
    "description":      "A brief description.",
    "minPluginVersion": "1.0.0"
}
```

If `theme.json` is absent or the `name` field is missing, the theme directory name is used in the OBS properties dropdown.

---

## The JS contract

The plugin calls `window.__nowPlaying.update(payload)` every time playback state changes (track change, play/pause, progress tick, etc.).

Your `index.html` must define this object before the plugin calls it. The plugin guards each call with `window.__nowPlaying && window.__nowPlaying.update(...)`, so calls before the page has finished loading are silently dropped — your first `update()` will arrive once the page is ready.

```javascript
window.__nowPlaying = {
    // Required. Called on every state change.
    update: function (payload) { /* ... */ },

    // Optional. Called once on page load with the initial state.
    // If omitted, the plugin falls back to calling update() instead.
    init: function (payload) { /* ... */ }
};
```

---

## Payload schema

```json
{
    "title":          "Track title — empty string if nothing is playing",
    "artist":         "Artist name",
    "album":          "Album name",
    "appId":          "Source app identifier, e.g. 'Spotify.exe'",
    "isPlaying":      true,
    "progress":       0.42,
    "thumbnail":      "data:image/png;base64,... — or empty string",
    "showAlbumArt":   true,
    "showProgress":   true,
    "visibilityMode": "always",
    "hasNewTrack":    false
}
```

### Field notes

| Field | Notes |
|-------|-------|
| `title` | Empty string means nothing is currently playing. Check this before showing the widget. |
| `progress` | `0.0`–`1.0`. May be `0.0` even while playing if the source app doesn't report timeline data. |
| `thumbnail` | Always a string — either a `data:image/png;base64,...` data URI, or `""`. Never null. |
| `showAlbumArt` | Controlled by the user in OBS properties. Your theme must respect it. |
| `showProgress` | Same — show or hide the progress bar accordingly. |
| `visibilityMode` | `"always"` or `"on_track_change"`. **Your theme drives show/hide behaviour** — the plugin does not touch source visibility or opacity. |
| `hasNewTrack` | `true` for exactly one `update()` call when the track title changes. Use this to trigger entrance animations in `on_track_change` mode. |

---

## Visibility modes

### `"always"`

Show the widget whenever `title` is non-empty; hide it when `title` is `""`.

```javascript
function update(payload) {
    if (payload.title) {
        showWidget();
    } else {
        hideWidget();
    }
}
```

### `"on_track_change"`

Show the widget briefly when a new track starts, then hide it again. The display duration is up to your theme — use whatever fits your animation cadence. A reasonable default is 8 seconds.

```javascript
var hideTimer = null;

function update(payload) {
    if (payload.hasNewTrack && payload.title) {
        if (hideTimer) clearTimeout(hideTimer);
        showWidget();
        hideTimer = setTimeout(hideWidget, 8000);
    } else if (!payload.title) {
        if (hideTimer) { clearTimeout(hideTimer); hideTimer = null; }
        hideWidget();
    }
    // No new track, title exists → leave current visibility state
}
```

The plugin does **not** provide a duration property — each theme implements its own timing. This lets themes coordinate the duration with their animation length.

---

## CSS variables injected by the plugin

When the page loads (and when the user resizes the source in OBS properties), the plugin injects:

```javascript
document.documentElement.style.setProperty('--np-width',  '800px');
document.documentElement.style.setProperty('--np-height', '120px');
```

Use these in your CSS to make the layout adapt to whatever canvas size the user has chosen:

```css
html, body {
    width:  var(--np-width,  800px);
    height: var(--np-height, 120px);
    overflow: hidden;
    background: transparent; /* required for OBS compositing */
}
```

---

## Tips

- **Transparent background**: Set `body { background: transparent; }`. OBS composites the CEF frame as a texture — any non-transparent pixels will appear opaque in the scene.
- **Fonts**: Use system fonts or embed them with `@font-face` / a local `<link>` to a font file inside your theme directory. Themes cannot access the internet.
- **Images**: Reference images by relative path. `<img src="background.png">` will work if `background.png` is in the same directory as `index.html`.
- **Multiple instances**: Multiple Now Playing sources can run simultaneously, each with a different theme. Each instance has its own CEF page — they don't share state.
- **Tolerance**: Your `update()` function must not throw if any field is missing or null. Use `payload.title || ''` style defaults throughout.

---

## Minimal example

```html
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <style>
        body { margin: 0; background: transparent; font-family: sans-serif; }
        #widget { 
            padding: 12px 20px; 
            background: rgba(0,0,0,0.75); 
            color: #fff; 
            border-radius: 8px; 
            display: none;
        }
    </style>
</head>
<body>
    <div id="widget">
        <div id="title"></div>
        <div id="artist" style="opacity:0.7; font-size:0.85em;"></div>
    </div>

    <script>
    window.__nowPlaying = {
        update: function(p) {
            document.getElementById('title').textContent  = p.title  || '';
            document.getElementById('artist').textContent = p.artist || '';
            document.getElementById('widget').style.display = p.title ? 'block' : 'none';
        }
    };
    </script>
</body>
</html>
```
