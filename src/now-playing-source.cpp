// now-playing-source.cpp — OBS source registration and callbacks

#include "now-playing-source.h"

#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

#include <obs-module.h>
#include <plugin-support.h>

// Declared in browser-bridge.cpp
extern "C" void browser_bridge_populate_theme_list(obs_property_t *list);
extern "C" bool browser_bridge_get_theme_defaults(const char *theme_id, int *out_width,
						   int *out_height);

// ---------------------------------------------------------------------------
// Properties callbacks
// ---------------------------------------------------------------------------

// Called when the user picks a different theme. Updates width/height to the
// theme's defaults if it declares them in theme.json.
static bool on_theme_changed(obs_properties_t * /*props*/, obs_property_t * /*property*/,
			     obs_data_t *settings)
{
	const char *theme_id = obs_data_get_string(settings, PROP_THEME);
	int w = 0, h = 0;
	if (theme_id && browser_bridge_get_theme_defaults(theme_id, &w, &h)) {
		obs_data_set_int(settings, PROP_WIDTH,  w);
		obs_data_set_int(settings, PROP_HEIGHT, h);
		std::string info = "Default: " + std::to_string(w) + " \xc3\x97 " + std::to_string(h);
		obs_data_set_string(settings, "theme_defaults_info", info.c_str());
	} else {
		obs_data_set_string(settings, "theme_defaults_info", "");
	}
	return true;
}

// Opens the themes folder in Windows Explorer.
static bool open_themes_folder(obs_properties_t * /*props*/, obs_property_t * /*property*/,
			       void * /*data*/)
{
	char *raw = obs_module_file("themes");
	if (!raw)
		return false;

	// Resolve to absolute path — obs_module_file() may return a relative path
	std::filesystem::path abs = std::filesystem::absolute(raw);
	bfree(raw);

	ShellExecuteW(nullptr, L"explore", abs.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);

	return false;
}

// ---------------------------------------------------------------------------
// Snapshot comparison — returns true if anything changed
// ---------------------------------------------------------------------------

static bool states_differ(const NowPlayingState &a, const NowPlayingState &b)
{
	return a.title         != b.title   ||
	       a.artist        != b.artist  ||
	       a.album         != b.album   ||
	       a.is_playing    != b.is_playing ||
	       a.thumbnail_b64 != b.thumbnail_b64;
	// progress is excluded — it changes every frame while playing and would
	// flood CEF with execute_script calls. It's pushed via the tick_count path.
}

// ---------------------------------------------------------------------------
// Source callbacks
// ---------------------------------------------------------------------------

static const char *np_get_name(void * /*type_data*/)
{
	return obs_module_text("NowPlaying.SourceName");
}

static void *np_create(obs_data_t *settings, obs_source_t * /*source*/)
{
	auto *ctx = new NowPlayingSource();
	if (!ctx->browser.Create(settings)) {
		obs_log(LOG_WARNING, "[NowPlayingSource] BrowserBridge::Create failed — "
				     "obs-browser may not be available");
	}
	return ctx;
}

static void np_destroy(void *data)
{
	auto *ctx = reinterpret_cast<NowPlayingSource *>(data);
	ctx->browser.Destroy();
	delete ctx;
}

static void np_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, PROP_WIDTH,          800);
	obs_data_set_default_int(settings, PROP_HEIGHT,         120);
	obs_data_set_default_string(settings, PROP_THEME,       "default");
	obs_data_set_default_bool(settings, PROP_SHOW_ALBUM_ART, true);
	obs_data_set_default_bool(settings, PROP_SHOW_PROGRESS,  true);
	obs_data_set_default_string(settings, PROP_VISIBILITY_MODE, VISIBILITY_ALWAYS);
	obs_data_set_default_string(settings, "theme_defaults_info", "Default: 800 \xc3\x97 120");
}

static obs_properties_t *np_get_properties(void * /*data*/)
{
	obs_properties_t *props = obs_properties_create();

	// Canvas size
	obs_properties_add_int(props, PROP_WIDTH,
			       obs_module_text("NowPlaying.Width"),
			       64, 7680, 1);
	obs_properties_add_int(props, PROP_HEIGHT,
			       obs_module_text("NowPlaying.Height"),
			       32, 4320, 1);

	// Theme selector — populated by scanning data/themes/
	obs_property_t *theme_list = obs_properties_add_list(
		props, PROP_THEME,
		obs_module_text("NowPlaying.Theme"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	browser_bridge_populate_theme_list(theme_list);
	obs_property_set_modified_callback(theme_list, on_theme_changed);

	obs_properties_add_text(props, "theme_defaults_info", "", OBS_TEXT_INFO);

	obs_properties_add_button(props, "open_themes_folder",
				  obs_module_text("NowPlaying.OpenThemesFolder"),
				  open_themes_folder);

	// Display options
	obs_properties_add_bool(props, PROP_SHOW_ALBUM_ART,
				obs_module_text("NowPlaying.ShowAlbumArt"));
	obs_properties_add_bool(props, PROP_SHOW_PROGRESS,
				obs_module_text("NowPlaying.ShowProgress"));

	// Visibility mode
	obs_property_t *vis = obs_properties_add_list(
		props, PROP_VISIBILITY_MODE,
		obs_module_text("NowPlaying.VisibilityMode"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(vis,
				     obs_module_text("NowPlaying.Visibility.Always"),
				     VISIBILITY_ALWAYS);
	obs_property_list_add_string(vis,
				     obs_module_text("NowPlaying.Visibility.OnTrackChange"),
				     VISIBILITY_ON_TRACK_CHANGE);

	return props;
}

static void np_update(void *data, obs_data_t *settings)
{
	auto *ctx = reinterpret_cast<NowPlayingSource *>(data);
	ctx->browser.Update(settings);
}

static void np_video_tick(void *data, float /*seconds*/)
{
	auto *ctx = reinterpret_cast<NowPlayingSource *>(data);
	ctx->tick_count++;

	NowPlayingState snap = ctx->smtc.GetSnapshot();

	// Push on state change (title/artist/playing/etc), or:
	//   tick 60  (~1 s): deferred initial push so page has loaded by then
	//   tick 120 (~2 s): second safety push
	//   every 60 ticks thereafter: keeps progress bar current at ~1 Hz
	bool timed_push = (ctx->tick_count == 60 || ctx->tick_count == 120 ||
	                   (ctx->tick_count > 120 && ctx->tick_count % 60 == 0));

	if (timed_push || states_differ(snap, ctx->last_state) || snap.has_new_track) {
		ctx->last_state = snap;
		ctx->browser.PushState(snap);
	}
}

static void np_video_render(void *data, gs_effect_t *effect)
{
	auto *ctx = reinterpret_cast<NowPlayingSource *>(data);
	ctx->browser.Render(effect);
}

static uint32_t np_get_width(void *data)
{
	auto *ctx = reinterpret_cast<NowPlayingSource *>(data);
	return ctx->browser.GetWidth();
}

static uint32_t np_get_height(void *data)
{
	auto *ctx = reinterpret_cast<NowPlayingSource *>(data);
	return ctx->browser.GetHeight();
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_now_playing_source()
{
	static obs_source_info info = {};

	info.id           = "zapdel_now_playing";
	info.type         = OBS_SOURCE_TYPE_INPUT;
	info.icon_type    = OBS_ICON_TYPE_MEDIA;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_CUSTOM_DRAW;

	info.get_name      = np_get_name;
	info.create        = np_create;
	info.destroy       = np_destroy;
	info.get_defaults  = np_get_defaults;
	info.get_properties = np_get_properties;
	info.update        = np_update;
	info.video_tick    = np_video_tick;
	info.video_render  = np_video_render;
	info.get_width     = np_get_width;
	info.get_height    = np_get_height;

	obs_register_source(&info);
	obs_log(LOG_INFO, "[NowPlayingSource] Registered source 'zapdel_now_playing'");
}
