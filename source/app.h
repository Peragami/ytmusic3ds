// app.h - shared application state, paths and colours.
//
// v3.17 split the old 2,352-line main.c into main.c (flows and
// the main loop), ui.c (rendering), store.c (favourites /
// config / dislikes persistence) and keyboard.c (the flick
// keyboard). Everything they share lives here; behaviour is
// unchanged from v3.16.
#ifndef APP_H
#define APP_H

#include <3ds.h>
#include <citro2d.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "net.h"    // result_t, g_results, g_quality
#include "player.h" // play_ctl_t
#include "thumb.h"  // THUMB_MAX_*

// ---------- paths ----------
#define APP_VER "3.41"
// v3.37 (user request): one fixed folder for every version -
// updates no longer leave a new ytmusic3ds-vX.Y folder behind
// each time. Older versioned folders are still read once via
// store.c's legacy import walk.
#define APP_DIR "sdmc:/3ds/ytmusic3ds"
#define FAV_PATH APP_DIR "/favorites.txt"
#define CFG_PATH APP_DIR "/config.txt"
#define DISLIKE_PATH APP_DIR "/dislikes.txt"
#define LIKE_PATH APP_DIR "/likes.txt"
// Previous versions' folders: favourites and config are
// imported from the newest one that has them (store.c tries
// each in order).

// v3.12: the update manifest URL is baked in (config.txt can
// still override it). Distribution lives in the dist/ folder of
// the user's GitHub repository.
#define DEFAULT_UPDATE_URL \
    "https://raw.githubusercontent.com/Peragami/ytmusic3ds/main/dist/version.json"

// ---------- tabs ----------
#define TAB_HOME 0
#define TAB_SEARCH 1 // action only (opens the keyboard)
#define TAB_LIBRARY 2
#define TAB_SETTINGS 3

// ---------- colours (YouTube Music palette) ----------
#define COL_BG C2D_Color32(0, 0, 0, 255)
#define COL_WHITE C2D_Color32(255, 255, 255, 255)
#define COL_DIM C2D_Color32(170, 170, 170, 255)
#define COL_FAINT C2D_Color32(113, 113, 113, 255)
#define COL_ACCENT C2D_Color32(255, 0, 0, 255)
#define COL_RULE C2D_Color32(45, 45, 45, 255)
#define COL_SURFACE C2D_Color32(33, 33, 33, 255)
#define COL_NAV C2D_Color32(0, 0, 0, 255)
#define COL_CHIP C2D_Color32(42, 42, 42, 255)
#define COL_THUMB C2D_Color32(52, 52, 52, 255)
#define COL_TRACK C2D_Color32(63, 63, 63, 255)

// ---------- shared state (defined in main.c) ----------
extern int g_tab;
extern char g_query[128];
extern int g_selected;
extern char g_status[160];
extern char g_route_label[96];
extern play_ctl_t g_ctl;
extern u64 g_last_draw_ms;
extern int g_playback_view;
extern int g_playing;
extern int g_playing_tab;
extern int g_switch_target;
extern result_t g_np_song;
extern long g_np_likes;
extern int g_has_np;
extern int g_player_view;
extern int g_lyrics_view;
extern int g_lyrics_scroll;
// Display-row pitch of the two-screen lyrics sheet (v3.34);
// shared by the renderer (ui.c) and the drag handler (main.c).
#define LYR_PITCH 30
extern int g_shuffle;
extern int g_repeat;
extern int g_deferred_pending;
extern int g_thumbs_on;
extern int g_kana_on;
extern int g_set_sel;
extern int g_tq_bank;
extern int g_tq_next;
extern int g_tq_total;
extern int g_tq_favs_dirty;

// ---------- state defined in store.c ----------
extern result_t g_favs[THUMB_MAX_FAVS];
extern int g_fav_count;
extern char g_update_url[384];
extern char g_self_path[320];
extern char g_update_msg[96];

// ---------- state defined in ui.c ----------
extern int g_lyrics_state; // 0 not tried / 1 found / 2 not found

#endif // APP_H
