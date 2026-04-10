#pragma once

#include "smtc-reader.h"
#include "browser-bridge.h"

// Property key constants — used in both get_properties and update callbacks
#define PROP_WIDTH            "width"
#define PROP_HEIGHT           "height"
#define PROP_THEME            "theme"
#define PROP_SHOW_ALBUM_ART   "show_album_art"
#define PROP_SHOW_PROGRESS    "show_progress"
#define PROP_VISIBILITY_MODE  "visibility_mode"

#define VISIBILITY_ALWAYS          "always"
#define VISIBILITY_ON_TRACK_CHANGE "on_track_change"

// Per-instance data allocated in obs_source_info::create, freed in ::destroy.
// No shared or static state between instances.
struct NowPlayingSource {
    SMTCReader      smtc;
    BrowserBridge   browser;
    NowPlayingState last_state;
    int             tick_count = 0; // used to schedule a deferred re-push after page load
};

// Registers the "zapdel_now_playing" source with OBS.
// Called once from obs_module_load().
void register_now_playing_source();
