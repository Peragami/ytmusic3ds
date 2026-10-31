// ytmusic3ds v3.29 - YouTube Music style client for (Old) 3DS.
//
// v3.29 changes over v3.28 (user report, 2026-10-31):
//   - Selecting a song no longer freezes the app for ~1 s:
//     the resolve took two blocking round trips (visitorData
//     GET + /player POST) with no input or redraw. net.c
//     requests now run on a curl multi handle with a UI pump
//     (input + redraw keep running, B cancels), and the
//     visitorData token is cached for 30 minutes so later
//     resolves need only the /player POST.
//
// v3.28 changes over v3.27 (user request, 2026-10-31):
//   - The player view's background is tinted with the
//     now-playing jacket's average colour (vertical gradient
//     toward black on both screens), like the real app. The
//     average is measured while the jacket texture is
//     uploaded (thumb_np_avg). List screens stay black.
//
// v3.27 changes over v3.26 (user requests, 2026-10-31):
//   - The app now boots into the Library tab (the user's
//     favourites, loaded from the app folder) instead of
//     opening the search prompt every time.
//   - Thumbnails and jackets are cached on disk in
//     APP_DIR/cache/<id>_<hq|mq>.jpg and reused on later
//     launches, so saved songs show their art instantly.
//
// v3.26 changes over v3.25 (user requests, 2026-10-31):
//   - Removed the "Y: 一覧に戻る" hint from the top (jacket)
//     screen of the player view; the bottom screen keeps its
//     footer hints.
//   - Jacket corner rounding reduced toward the real app:
//     the now-playing texture mask is 5% of the side (was
//     12%) and the jacket placeholder matches. List
//     thumbnails are unchanged.
//
// v3.25 changes over v3.24 (user report, 2026-10-31):
//   - The combined like|bad pill now hugs its content: the
//     divider sits just after the like count and Bad right
//     after it, instead of at a fixed position that left a
//     wide empty gap with short counts. The lyrics and save
//     pills flow after it. Layout is shared between the
//     drawing code and the touch zones (pill_layout in ui.c).
//
// v3.24 changes over v3.23 (user request, 2026-10-31): the
//   player pills now match the real app, compared against the
//   user's screenshot: Like and Bad share one pill split by a
//   divider; the Lyrics pill uses the real app's quote-mark
//   glyph (hand-drawn, ICON_LYRICS) and carries its label
//   again; a Save pill (ICON_PLAYLIST_ADD / _CHECK) joins the
//   row. Like is independent from the Library now (likes.txt);
//   Save is the favourite toggle. New icons are appended, the
//   existing masks are unchanged.
//
// v3.23 changes over v3.22 (user request, 2026-10-31):
//   - Bad button gets the same treatment as Like: white
//     outline thumb-down when not disliked, filled white when
//     the song is in dislikes.txt. Adds ICON_THUMB_DOWN_OFF.
//
// v3.22 changes over v3.21 (user request, 2026-10-31):
//   - Like button now matches the real app: a white outline
//     thumb when not liked, filled white when liked - never
//     red. Adds ICON_THUMB_UP_OFF (classic outline glyph paired
//     with the existing filled thumb_up) to the icon set.
//
// v3.21 changes over v3.20 (user report, 2026-10-31):
//   - Tab bar labels are now centred on their tabs by measured
//     text width; the old fixed offsets left the 2-character
//     labels （検索/設定） sitting left of their icons.
//
// v3.20 changes over v3.19 (user request, 2026-10-31): UI pass
//   toward the real YouTube Music app, compared against the
//   user's screenshots: pure-black background and nav bar,
//   rounded corners on all artwork (alpha mask baked into the
//   thumbnail textures, rounded placeholder blocks), floating
//   rounded highlight on the selected row, chunkier list rows
//   and chips, taller mini player with a 44px jacket, filled
//   bookmark icon on the active library tab, bigger player
//   title. Also: the corner version label now uses APP_VER
//   (it had stayed "v3.16" since that build), APP_VER moved to
//   app.h, and the favourites/config import now walks every
//   previous version's folder newest-first instead of only
//   v3.16's.
//
// v3.19 changes over v3.18 (user report, 2026-10-31):
//   - The v3.18 worker thread never actually started: it was
//     created with priority 0x40, outside the valid 0x00-0x3F
//     range, so threadCreate failed and the silent synchronous
//     fallback froze the UI like the old code. Fixed to 0x3F
//     (app core), creation is retried, the synchronous fallback
//     is removed, and the fetch progress line now shows
//     （スレッド未動作） if the worker is not running.
//
// v3.18 changes over v3.17 (user report, 2026-10-31):
//   - Thumbnail downloads + JPEG decode now run on a worker
//     thread; the main thread only uploads finished textures.
//     The v3.16 input-abort workaround is gone - fetches can
//     no longer block input or delay playback start at all.
//
// v3.17 changes over v3.16 (refactor, user request, 2026-10-31):
//   - No behaviour change. The old single-file main.c (2,352
//     lines) is split into main.c (flows + main loop), ui.c
//     (rendering), store.c (favourites/config/dislikes) and
//     keyboard.c, sharing state via app.h.
//
// v3.16 changes over v3.15 (user feedback, 2026-10-31):
//   - Thumbnail fetches abort the moment the user touches
//     anything (plus 3s connect / 8s total timeouts) and the
//     queue retries interrupted items, so fetches can no
//     longer lock the UI out.
//   - Player screen: all button text captions removed. The
//     like pill shows the song's like count (Return YouTube
//     Dislike API estimate, fetched with the deferred assets),
//     bad and lyrics pills are icon-only, the routine "Playing"
//     status line is hidden, and the lyrics icon is the
//     text-lines "subject" glyph instead of the mic.
//
// v3.15 changes over v3.14 (user request, 2026-10-31):
//   - Keyboard rebuilt to the layout in the user's smartphone
//     screenshots: 5x4 grid, utility columns (back/delete/
//     cursor/space/symbols/mode/search), three modes (kana,
//     ABC, digits+symbols with printed sub-characters), and a
//     real text cursor with insert-at-cursor editing and
//     hold-to-repeat delete/cursor keys.
//
// v3.14 changes over v3.13 (bug report, 2026-10-31):
//   - The self-update failed with "cannot create temp file":
//     the app never created its own APP_DIR folder and relied on
//     the install zip. Startup now mkdirs it, the updater mkdirs
//     before writing its temp file, and the error names the path.
//
// v3.13 changes over v3.12 (user request, 2026-10-31):
//   - Smartphone-style search input: a flick/toggle kana
//     keyboard on the bottom screen (tap repeatedly to cycle,
//     flick for direct input, dakuten/small/case transform key,
//     ABC mode included). Settings gains a フリック入力 on/off
//     row (config key kana=); off restores the system keyboard.
//
// v3.12 changes over v3.11 (2026-10-31):
//   - The update manifest URL (the dist/version.json in the
//     user's GitHub repo, Peragami/ytmusic3ds) is baked in as
//     the default, so Settings -> アップデート works out of the
//     box. config.txt can still override it. This release is
//     also the first one published to the repo, which makes a
//     real v3.11 -> v3.12 over-the-air update test possible.
//
// v3.11 changes over v3.10 (user request, 2026-10-31):
//   - Self-update: Settings gains an アップデート row. The app
//     fetches a JSON manifest from the update_url in config.txt,
//     and when a newer version exists it downloads the new .3dsx,
//     verifies size + SHA-256, and swaps it in place of the
//     running file (old one kept as .bak until the swap lands),
//     then asks for a restart. Manual check only - nothing is
//     fetched at startup, so playback start is never delayed.
//
// v3.10 changes over v3.9 (Azahar screenshot analysis, 2026-10-31):
//   - Texture channel order fix. GPU_RGBA8 texels are stored
//     A,B,G,R in memory; both the icon and thumbnail uploads
//     packed R first, so the GPU read red as alpha and the alpha
//     as red. Icons came out cyan ((0,255,255) measured in the
//     user's screenshot) and thumbnails/jackets have been
//     colour-rotated all along. Packing fixed in icons.c and
//     thumb.c; verified by a host-side simulation of the sampler.
//
// v3.9 changes over v3.8 (user feedback, 2026-10-31):
//   - Pressing play no longer waits for the HQ jacket or the
//     lyrics: both are fetched from the playback hook while the
//     song downloads. Until the HQ art lands, the player screen
//     shows the list thumbnail.
//   - Lyrics: LRCLIB turned out to fail intermittently with 503
//     ("server busy") - http_get now retries 5xx, the fetch tries
//     more query shapes, and the lyrics screen shows the reason
//     (busy / not registered / network) when nothing is found.
//
// v3.8 changes over v3.7 (hardware/Azahar feedback, 2026-10-31):
//   - Icons rendered as solid tint-coloured squares in v3.7 (the
//     tint path dropped the glyph alpha). Icons now bake one
//     texture per UI colour and draw untinted, like thumbnails.
//   - Thumbnail fetching is incremental (one image per main-loop
//     iteration, progress shown on the list screen) so the UI
//     stays fully operable while thumbnails load.
//
// v3.7 changes over v3.6 (user requests, 2026-10-31):
//   - Faithfulness pass: the icons are Google's Material icons
//     (the same set the real app uses) converted to 3DS textures;
//     colours matched to the app (near-black #030303 background,
//     #212121 surfaces, white progress bars, grey secondary text).
//   - The normal screens get the app's MINI PLAYER above the tab
//     bar: jacket thumbnail, title, artist and a play/pause button,
//     with the thin progress bar between it and the tabs. Tapping
//     the mini player opens the player screen; its button pauses
//     or resumes during playback, replays when idle.
//   - Jacket images are auto-trimmed: solid/blurred letterbox bars
//     in the source thumbnails are detected and cropped so the art
//     fills the frame like in the app.
//
// v3.6 changes over v3.5 (user requests, 2026-10-31, modelled on
// the app's now-playing screen):
//   - Selecting a song opens a PLAYER screen: TOP = big jacket
//     artwork (hqdefault thumbnail at 128px), BOTTOM = song title,
//     Like / Bad / Lyrics pills, progress bar with times, and a
//     control row (shuffle, previous, play/pause, next, repeat).
//     Controls work by touch and by buttons (A = pause/resume,
//     L/R = prev/next). Pause is real (ndsp channel pause).
//   - Y toggles between the player screen and the list screens,
//     during playback and while idle.
//   - Like = likes.txt only (v3.24 split it from the Library;
//     the Save pill / X key manage favourites). Bad = records
//     the song in dislikes.txt and skips to the next song.
//   - Lyrics come from the free LRCLIB API (best effort) and are
//     shown on the top screen, scrollable with the D-pad.
//   - Queue behaviour: when a song finishes, playback advances to
//     the next song in the list (shuffle and repeat off/all/one
//     honoured); manual next/prev wraps/skips within the list.
//
// v3.5 changes over v3.4 (user requests, 2026-10-31, modelled on a
// screenshot of the real YouTube Music app):
//   - Layout follows the app: TOP screen = song list (logo header,
//     chips row, section title, big rows with REAL thumbnails
//     downloaded from i.ytimg.com and decoded with libjpeg),
//     BOTTOM screen = now-playing area on top and a 4-tab bar
//     (Home / Search / Library / Settings) at the bottom, tappable
//     via the touch screen (L/R also switch tabs).
//   - Library tab = favourites stored on the SD card
//     (sdmc:/3ds/ytmusic3ds-v3.16/favorites.txt). X toggles a song
//     as favourite; favourites can be played like search results.
//   - Settings tab: sound quality (itag preference order in net.c)
//     and thumbnail on/off, persisted to config.txt on the SD card.
//   - Playback (v3.3 engine) and mid-playback song switching (v3.4)
//     are unchanged; during playback the bottom screen shows the
//     playing song above the tab bar.
//
// v3.4: YouTube Music palette + mid-playback switching (D-pad
// browse in the playback hook, A switches to the selected song).
// v3.3: VISIONOS direct resolution + persistent Range downloader.
// v3.2: system-font UI (Japanese titles), stack fixes.
// See ../report.md for the full history.


#include <3ds.h>
#include <citro2d.h>
#include <curl/curl.h>

#include <malloc.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "icons.h"
#include "keyboard.h"
#include "lyrics.h"
#include "store.h"
#include "ui.h"
#include "updater.h"

// APP_VER lives in app.h (v3.20: ui.c draws it too).

// The resolve/playback paths run deep curl + mbedTLS call chains on
// this thread; give the main stack real headroom (v3.1 crash fix).
u32 __stacksize__ = 256 * 1024;

// ---------- tabs ----------
int g_tab = TAB_HOME;

// ---------- state ----------
char g_query[128] = "";
int g_selected = 0;
char g_status[160] = "A: search.";
char g_route_label[96] = "-";

play_ctl_t g_ctl;
u64 g_last_draw_ms = 0;
int g_playback_view = 0;
int g_playing = -1; // index into the playing list
int g_playing_tab = TAB_HOME;
int g_switch_target = -1;
result_t g_np_song; // now-playing (or last played) song
long g_np_likes = -1; // like count, -1 = unknown (v3.16)
int g_has_np = 0;

// ---------- v3.6 player screen ----------
int g_player_view = 0; // 1 = player screen (artwork+controls)
int g_lyrics_view = 0; // top screen shows lyrics instead
int g_lyrics_scroll = 0;
int g_shuffle = 0;
int g_repeat = 0; // 0 off / 1 all / 2 one
// v3.9: HQ artwork + lyrics are fetched from the playback hook
// (while the song downloads) instead of before playback, so
// pressing play starts the song immediately.
int g_deferred_pending = 0;

// ---------- settings (persisted) ----------
int g_thumbs_on = 1; // g_quality lives in net.c
int g_kana_on = 1;   // v3.13: flick/toggle keyboard
int g_set_sel = 0;

// ---------- incremental thumbnail fetching (v3.8) ----------
// Thumbnails are fetched one per main-loop iteration so the UI
// stays responsive while they load (v3.7 fetched them in a
// blocking loop and froze the controls for seconds).
int g_tq_bank = -1; // bank being fetched, or -1
int g_tq_next = 0;
int g_tq_total = 0;
int g_tq_favs_dirty = 0; // favourites bank was invalidated

static void artist_from_sub(const char *sub, char *out, size_t cap);
static void thumb_poll(void);

// ---------- playback UI hook ----------
// Touch actions on the player screen while playing. Zones come
// from player_zone(); like/bad/lyrics/shuffle/repeat act in place,
// prev/next/pause drive the player through ctl.
static void hook_player_touch(play_ctl_t *ctl, int px, int py) {
    switch (player_zone(px, py)) {
    case 1:
        like_toggle(g_np_song.id);
        break;
    case 9:
        fav_toggle(&g_np_song);
        break;
    case 2:
        dislike_add(g_np_song.id);
        ctl->skip_dir = 1;
        ctl->stop_requested = 1;
        break;
    case 3:
        g_lyrics_view = !g_lyrics_view;
        g_lyrics_scroll = 0;
        break;
    case 4:
        g_shuffle = !g_shuffle;
        config_save();
        break;
    case 5:
        ctl->skip_dir = -1;
        ctl->stop_requested = 1;
        break;
    case 6:
        if (ctl->phase == PHASE_PLAY) ctl->pause_flip = 1;
        break;
    case 7:
        ctl->skip_dir = 1;
        ctl->stop_requested = 1;
        break;
    case 8:
        g_repeat = (g_repeat + 1) % 3;
        config_save();
        break;
    }
    g_last_draw_ms = 0; // redraw promptly
}

static void ui_hook(void *user) {
    play_ctl_t *ctl = (play_ctl_t *)user;
    hidScanInput();
    u32 k = hidKeysDown();
    if (k & KEY_B) ctl->stop_requested = 1;
    if (k & KEY_Y) {
        g_player_view = !g_player_view;
        g_last_draw_ms = 0;
    }
    if (k & KEY_TOUCH) {
        touchPosition tp;
        hidTouchRead(&tp);
        if (g_player_view) {
            hook_player_touch(ctl, tp.px, tp.py);
        } else if (g_has_np && tp.py >= MINI_TOP && tp.py < NAV_TOP) {
            // Mini player: button toggles pause, body opens the
            // player screen.
            if (tp.px >= 260) {
                if (ctl->phase == PHASE_PLAY) ctl->pause_flip = 1;
            } else {
                g_player_view = 1;
            }
            g_last_draw_ms = 0;
        }
    }
    if (g_player_view) {
        // Player screen controls.
        if ((k & KEY_A) && ctl->phase == PHASE_PLAY) ctl->pause_flip = 1;
        if (k & KEY_R) {
            ctl->skip_dir = 1;
            ctl->stop_requested = 1;
        }
        if (k & KEY_L) {
            ctl->skip_dir = -1;
            ctl->stop_requested = 1;
        }
        if (g_lyrics_view) {
            if (k & KEY_DOWN) g_lyrics_scroll++;
            if ((k & KEY_UP) && g_lyrics_scroll > 0) g_lyrics_scroll--;
        }
    } else {
        // List browsing + mid-playback switch (v3.4 behaviour).
        if (k & KEY_DOWN) {
            if (g_selected + 1 < act_count()) g_selected++;
        } else if (k & KEY_UP) {
            if (g_selected > 0) g_selected--;
        } else if (k & KEY_RIGHT) {
            int n = g_selected + 5;
            if (n >= act_count()) n = act_count() - 1;
            if (n != g_selected && n >= 0) g_selected = n;
        } else if (k & KEY_LEFT) {
            int n = g_selected - 5;
            if (n < 0) n = 0;
            if (n != g_selected) g_selected = n;
        }
        if ((k & KEY_A) && act_count() > 0 && g_selected != g_playing) {
            g_switch_target = g_selected;
            ctl->switch_requested = 1;
            ctl->stop_requested = 1;
        }
        if (k & KEY_R) {
            ctl->skip_dir = 1;
            ctl->stop_requested = 1;
        }
        if (k & KEY_L) {
            ctl->skip_dir = -1;
            ctl->stop_requested = 1;
        }
    }
    if (!aptMainLoop()) {
        ctl->stop_requested = 1;
        ctl->exit_requested = 1;
    }
    // v3.9 deferred fetches: HQ artwork and lyrics load here,
    // while the song file downloads, instead of blocking the
    // start of playback. Runs once per song.
    // HQ artwork loads on the worker thread (v3.18): keep asking
    // until the job is accepted, and land it via thumb_poll.
    if (g_thumbs_on && g_has_np && !thumb_get(THUMB_BANK_NP, 0))
        thumb_job_start(THUMB_BANK_NP, 0, g_np_song.id);
    thumb_poll();
    if (g_deferred_pending) {
        g_deferred_pending = 0;
        char artist[64];
        artist_from_sub(g_np_song.sub, artist, sizeof(artist));
        g_lyrics_state =
            lyrics_fetch(g_np_song.id, g_np_song.title, artist) == 0
                ? 1
                : 2;
        long lk = -1;
        if (likes_fetch(g_np_song.id, &lk) == 0) g_np_likes = lk;
        g_last_draw_ms = 0;
    }
    u64 now = osGetTime();
    if (now - g_last_draw_ms >= 150) {
        g_last_draw_ms = now;
        render();
    }
}

// ---------- thumbnails (incremental queue, v3.8) ----------
static void thumb_queue_start(int bank) {
    if (!g_thumbs_on) {
        g_tq_bank = -1;
        return;
    }
    g_tq_bank = bank;
    g_tq_next = 0;
    g_tq_total = bank == THUMB_BANK_FAVS ? g_fav_count : g_result_count;
}

// Land a finished worker result if it still matches what the
// UI wants; drop it if the list moved on meanwhile (v3.18).
static void thumb_poll(void) {
    if (thumb_job_state() != 2) return;
    int bank, idx;
    char id[16];
    thumb_job_peek(&bank, &idx, id, sizeof(id));
    int ok = 0;
    if (bank == THUMB_BANK_NP)
        ok = g_has_np && strcmp(id, g_np_song.id) == 0;
    else if (bank == THUMB_BANK_FAVS)
        ok = idx < g_fav_count && strcmp(id, g_favs[idx].id) == 0;
    else
        ok = idx < g_result_count && strcmp(id, g_results[idx].id) == 0;
    if (ok)
        thumb_job_commit();
    else
        thumb_job_discard();
}

// Keep at most one fetch queued on the worker thread; the main
// loop calls this every iteration and it never blocks (v3.18:
// fetches run on the worker, this only hands out job tickets).
static void thumb_queue_step(void) {
    thumb_poll();
    if (g_tq_bank < 0) return;
    if (thumb_job_state() != 0) return; // worker busy
    int bank = g_tq_bank;
    while (g_tq_next < g_tq_total) {
        int idx = g_tq_next++;
        if (thumb_get(bank, idx)) continue;
        int rc = thumb_job_start(bank, idx,
                                 bank == THUMB_BANK_FAVS
                                     ? g_favs[idx].id
                                     : g_results[idx].id);
        if (rc == -2) g_tq_next--; // no worker yet: keep the item
        return;
    }
    g_tq_bank = -1;
}

static void fetch_result_thumbs(void) {
    thumb_queue_start(THUMB_BANK_RESULTS);
}

static void fetch_fav_thumbs(void) {
    thumb_queue_start(THUMB_BANK_FAVS);
}

// ---------- flows ----------
static int prompt_query_swkbd(void) {
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, sizeof(g_query) - 1);
    swkbdSetHintText(&swkbd, "Search YouTube Music");
    if (g_query[0]) swkbdSetInitialText(&swkbd, g_query);
    char buf[sizeof(g_query)];
    buf[0] = '\0';
    SwkbdButton btn = swkbdInputText(&swkbd, buf, sizeof(buf));
    if (btn != SWKBD_BUTTON_CONFIRM || buf[0] == '\0') return -1;
    strncpy(g_query, buf, sizeof(g_query) - 1);
    g_query[sizeof(g_query) - 1] = '\0';
    return 0;
}

static int prompt_query(void) {
    return g_kana_on ? kana_prompt() : prompt_query_swkbd();
}

// v3.29: net.c calls this while a request is in flight, so
// the screen keeps redrawing and B cancels the wait instead
// of the app looking frozen (the user reported ~1 s of dead
// input at song select, where resolve made two round trips).
static int g_net_cancelled = 0;
static int net_pump(void) {
    hidScanInput();
    if (hidKeysDown() & KEY_B) g_net_cancelled = 1;
    draw_all();
    return g_net_cancelled;
}

static void do_search_flow(void) {
    if (prompt_query() != 0) return;
    snprintf(g_status, sizeof(g_status), "Searching...");
    draw_all();
    thumbs_clear_bank(THUMB_BANK_RESULTS);
    g_net_cancelled = 0;
    int search_rc = yt_search(g_query);
    if (g_net_cancelled) {
        snprintf(g_status, sizeof(g_status), "キャンセルしました");
        draw_all();
        return;
    }
    if (search_rc == 0) {
        g_tab = TAB_HOME;
        g_selected = 0;
        snprintf(g_status, sizeof(g_status), "%d results. A: play.",
                 g_result_count);
        draw_all();
        fetch_result_thumbs();
        snprintf(g_status, sizeof(g_status), "%d results. A: play.",
                 g_result_count);
    } else {
        snprintf(g_status, sizeof(g_status),
                 "Search failed or no song results.");
    }
    draw_all();
}

// ---------- play flow (v3.3 cascade, unchanged logic) ----------
static char g_attempt_log[128];

static void log_reset(void) { g_attempt_log[0] = '\0'; }

static void log_add(const char *entry) {
    size_t used = strlen(g_attempt_log);
    size_t need = strlen(entry) + (used ? 1 : 0);
    if (used + need >= sizeof(g_attempt_log)) return;
    if (used) strcat(g_attempt_log, " ");
    strcat(g_attempt_log, entry);
}

static void log_dl_fail(const char *tag) {
    char entry[48];
    unsigned kb = (unsigned)(g_ctl.fail_offset / 1024);
    if (g_ctl.fail_http > 0)
        snprintf(entry, sizeof(entry), "%s@%uKB H%ld", tag, kb,
                 g_ctl.fail_http);
    else
        snprintf(entry, sizeof(entry), "%s@%uKB C%d", tag, kb,
                 g_ctl.fail_curl);
    log_add(entry);
}

static void log_res_fail(const char *tag, long http) {
    char entry[48];
    if (http > 0)
        snprintf(entry, sizeof(entry), "%s res H%ld", tag, http);
    else
        snprintf(entry, sizeof(entry), "%s res net", tag);
    log_add(entry);
}

static void host_tag(const char *host, char *out, size_t cap) {
    const char *p = host;
    if (strncmp(p, "yt.", 3) == 0) p += 3;
    else if (strncmp(p, "inv.", 4) == 0) p += 4;
    else if (strncmp(p, "invidious.", 10) == 0) p += 10;
    size_t i = 0;
    while (p[i] && p[i] != '.' && i + 1 < cap) {
        out[i] = p[i];
        i++;
    }
    out[i] = '\0';
    if (i == 0) {
        strncpy(out, "inst", cap - 1);
        out[cap - 1] = '\0';
    }
}

static int attempt_play(const char *url, long length_sec,
                        const char *video_id, int instance_idx) {
    memset(&g_ctl, 0, sizeof(g_ctl));
    g_playback_view = 1;
    render();
    if (instance_idx >= 0)
        return play_stream_url_inv(url, (unsigned)length_sec, &g_ctl,
                                   ui_hook, &g_ctl, video_id, instance_idx);
    return play_stream_url_ex(url, (unsigned)length_sec, &g_ctl, ui_hook,
                              &g_ctl, video_id);
}

// Artist hint for the lyrics lookup: the sub line looks like
// "Artist • Album • 3:32"; take everything before the bullet.
static void artist_from_sub(const char *sub, char *out, size_t cap) {
    out[0] = '\0';
    if (!sub) return;
    const char *cut = NULL;
    const char *b1 = strstr(sub, "\xE2\x80\xA2"); // •
    const char *b2 = strstr(sub, "\xE3\x83\xBB"); // ・
    if (b1 && (!b2 || b1 < b2)) cut = b1;
    if (b2 && (!cut || b2 < cut)) cut = b2;
    size_t n = cut ? (size_t)(cut - sub) : strlen(sub);
    while (n > 0 && sub[n - 1] == ' ') n--;
    if (n >= cap) n = cap - 1;
    if (n > 60) return; // implausible as an artist name; skip hint
    memcpy(out, sub, n);
    out[n] = '\0';
}

// Play the selected song once. Outcome codes for do_play_flow:
// 0 finished, 1 stopped, 2 skip-next, 3 skip-prev, -1 error/other.
static int do_play_once(void) {
    result_t *r = act_at(g_selected);
    g_playing = g_selected;
    g_playing_tab = g_tab;
    g_np_song = *r;
    g_has_np = 1;
    thumbs_clear_bank(THUMB_BANK_NP); // drop the previous song's art
    g_player_view = 1;
    g_lyrics_view = 0;
    g_lyrics_scroll = 0;
    snprintf(g_status, sizeof(g_status), "Resolving stream...");
    draw_all();
    static char url[2048];
    int itag = 0;
    long length_sec = 0;
    log_reset();
    g_net_cancelled = 0;
    if (resolve_stream(r->id, url, sizeof(url), &itag, &length_sec) != 0) {
        if (g_net_cancelled) {
            snprintf(g_status, sizeof(g_status), "キャンセルしました");
            draw_all();
            return 1;
        }
        snprintf(g_status, sizeof(g_status),
                 "Could not resolve a stream (network/YouTube busy?).");
        draw_all();
        return -1;
    }
    // v3.9: no asset fetches here any more - pressing play goes
    // straight to the download. The HQ artwork and the lyrics are
    // fetched by the playback hook while the file downloads (see
    // g_deferred_pending), so they never delay the music.
    g_lyrics_state = 0;
    g_np_likes = -1;
    g_deferred_pending = 1;

    int rc = -1;
    int settled = 0;
    int tried_instance = -1;

    if (g_route == ROUTE_DIRECT) {
        snprintf(g_route_label, sizeof(g_route_label),
                 "direct YouTube (%s) (itag %d)",
                 g_direct_ctx == 1 ? "VISIONOS" : "recipe", itag);
        rc = attempt_play(url, length_sec, r->id, -1);
        if (rc == PLAY_DL_FAIL)
            log_dl_fail("dir");
        else
            settled = 1;
    } else {
        int idx = g_route_instance;
        tried_instance = idx;
        snprintf(g_route_label, sizeof(g_route_label),
                 "Invidious via %s (itag %d)", invidious_instance_host(idx),
                 itag);
        rc = attempt_play(url, length_sec, r->id, idx);
        if (rc == PLAY_DL_FAIL) {
            char tag[24];
            host_tag(invidious_instance_host(idx), tag, sizeof(tag));
            log_dl_fail(tag);
        } else
            settled = 1;
    }

    for (int idx = 0; !settled && idx < invidious_instance_count(); idx++) {
        if (idx == tried_instance) continue;
        const char *host = invidious_instance_host(idx);
        char tag[24];
        host_tag(host, tag, sizeof(tag));
        g_playback_view = 0;
        snprintf(g_status, sizeof(g_status), "Trying Invidious via %s...",
                 host);
        draw_all();
        long http = 0;
        if (resolve_stream_invidious_at(idx, r->id, url, sizeof(url), &itag,
                                        &length_sec, &http) != 0) {
            log_res_fail(tag, http);
            continue;
        }
        snprintf(g_route_label, sizeof(g_route_label),
                 "Invidious via %s (itag %d)", host, itag);
        rc = attempt_play(url, length_sec, r->id, idx);
        if (rc == PLAY_DL_FAIL)
            log_dl_fail(tag);
        else
            settled = 1;
    }

    g_playback_view = 0;
    if (settled && rc == 0)
        snprintf(g_status, sizeof(g_status), "Finished: %s", r->title);
    else if (settled && rc == 1)
        snprintf(g_status, sizeof(g_status), "Stopped.");
    else if (!settled && g_attempt_log[0])
        snprintf(g_status, sizeof(g_status), "Error: all routes failed: %s",
                 g_attempt_log);
    else
        snprintf(g_status, sizeof(g_status), "Error: %s", g_ctl.message);
    draw_all();
    if (g_ctl.skip_dir > 0) return 2;
    if (g_ctl.skip_dir < 0) return 3;
    if (settled && rc == 0) return 0;
    if (settled && rc == 1) return 1;
    return -1;
}

// Pick the next index in the playing list. dir=+1: next (shuffle
// honoured; wraps for manual skips, stops at the end for automatic
// advance unless repeat-all). dir=-1: previous (at the top, a
// manual skip restarts the current song).
static int pick_next(int from, int dir, int for_auto) {
    int n = act_count();
    if (n <= 0 || from < 0) return -1;
    if (dir > 0) {
        if (g_shuffle && n > 1) {
            int r;
            do {
                r = rand() % n;
            } while (r == from);
            return r;
        }
        int nx = from + 1;
        if (nx >= n) {
            if (for_auto && g_repeat != 1) return -1;
            nx = 0;
        }
        return nx;
    }
    int pv = from - 1;
    if (pv < 0) pv = for_auto ? -1 : from;
    return pv;
}

// Play flow with queue behaviour: manual song switches (v3.4),
// prev/next skips, and automatic advance when a song finishes
// (repeat one replays, repeat all wraps, shuffle randomises).
static void do_play_flow(void) {
    while (1) {
        g_switch_target = -1;
        int oc = do_play_once();
        if (g_ctl.exit_requested) break;
        if (g_switch_target >= 0) {
            g_selected = g_switch_target;
            continue;
        }
        if (oc == 2) {
            int nx = pick_next(g_playing, 1, 0);
            if (nx < 0) break;
            g_selected = nx;
            continue;
        }
        if (oc == 3) {
            int pv = pick_next(g_playing, -1, 0);
            if (pv < 0) break;
            g_selected = pv;
            continue;
        }
        if (oc == 0) {
            if (g_repeat == 2) continue; // replay the same song
            int nx = pick_next(g_playing, 1, 1);
            if (nx < 0) break;
            g_selected = nx;
            continue;
        }
        break; // stopped or error
    }
    g_playing = -1;
}

// ---------- tabs ----------
static void switch_tab(int tab) {
    if (tab == g_tab) return;
    g_tab = tab;
    g_selected = 0;
    if (tab == TAB_LIBRARY)
        fetch_fav_thumbs();
    else if (tab == TAB_HOME)
        fetch_result_thumbs();
    render();
}

// From the idle player screen: replay the shown song (which=0) or
// play its neighbour in the list it came from (which=+1/-1).
static void play_from_player_view(int which) {
    if (!g_has_np) return;
    int n = g_playing_tab == TAB_LIBRARY ? g_fav_count : g_result_count;
    int base = -1;
    for (int i = 0; i < n; i++) {
        const char *id = g_playing_tab == TAB_LIBRARY
                             ? g_favs[i].id
                             : g_results[i].id;
        if (strcmp(id, g_np_song.id) == 0) {
            base = i;
            break;
        }
    }
    if (base < 0) {
        snprintf(g_status, sizeof(g_status),
                 "その曲は今のリストにありません");
        return;
    }
    int target = base + which;
    if (target >= n) target = 0;
    if (target < 0) target = n - 1;
    g_tab = g_playing_tab;
    g_selected = target;
    do_play_flow();
}

// Touch actions on the idle player screen (same zones as during
// playback; play/prev/next start the corresponding song).
static void idle_player_touch(int px, int py) {
    switch (player_zone(px, py)) {
    case 1: {
        int rc = like_toggle(g_np_song.id);
        snprintf(g_status, sizeof(g_status),
                 rc == 1 ? "グッドしました" : "グッドを取り消しました");
        break;
    }
    case 9: {
        int rc = fav_toggle(&g_np_song);
        snprintf(g_status, sizeof(g_status),
                 rc == 1 ? "ライブラリに保存しました"
                 : rc == 0 ? "保存を取り消しました"
                           : "お気に入りがいっぱいです");
        break;
    }
    case 2:
        dislike_add(g_np_song.id);
        snprintf(g_status, sizeof(g_status), "バッドしました");
        break;
    case 3:
        g_lyrics_view = !g_lyrics_view;
        g_lyrics_scroll = 0;
        break;
    case 4:
        g_shuffle = !g_shuffle;
        config_save();
        break;
    case 5:
        play_from_player_view(-1);
        break;
    case 6:
        play_from_player_view(0);
        break;
    case 7:
        play_from_player_view(1);
        break;
    case 8:
        g_repeat = (g_repeat + 1) % 3;
        config_save();
        break;
    }
}

// ---------- v3.11 self-update flow ----------
static void upd_wait_key(u32 mask) {
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & mask) return;
        svcSleepThread(20000000ULL);
    }
}

static u32 upd_wait_choice(void) {
    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown() & (KEY_A | KEY_B);
        if (k) return k;
        svcSleepThread(20000000ULL);
    }
    return KEY_B;
}

static void upd_cb(int pct, const char *phase) {
    char l2[96];
    if (pct >= 0)
        snprintf(l2, sizeof(l2), "%s %d%%", phase, pct);
    else
        snprintf(l2, sizeof(l2), "%s", phase);
    render_message("アップデート", l2);
}

// Returns 1 when the app should exit (update installed).
static int do_update_flow(void) {
    char err[128];
    if (!g_update_url[0]) {
        render_message("アップデート",
                       "配信URLが未設定です (config.txt)");
        upd_wait_key(KEY_A | KEY_B | KEY_START);
        snprintf(g_update_msg, sizeof(g_update_msg), "URL未設定");
        return 0;
    }
    render_message("アップデート", "確認中...");
    upd_info_t info;
    if (updater_fetch_manifest(g_update_url, &info, err,
                               sizeof(err)) != 0) {
        render_message("アップデート", err);
        upd_wait_key(KEY_A | KEY_B | KEY_START);
        snprintf(g_update_msg, sizeof(g_update_msg), "確認失敗");
        return 0;
    }
    if (!updater_version_newer(info.version, APP_VER)) {
        char l2[64];
        snprintf(l2, sizeof(l2), "最新版です (v%s)", APP_VER);
        render_message("アップデート", l2);
        upd_wait_key(KEY_A | KEY_B | KEY_START);
        snprintf(g_update_msg, sizeof(g_update_msg), "最新版です");
        return 0;
    }
    {
        char l1[64];
        snprintf(l1, sizeof(l1), "v%s があります", info.version);
        render_message(l1, "A：更新する　B：やめる");
    }
    snprintf(g_update_msg, sizeof(g_update_msg), "v%s あり",
             info.version);
    if (upd_wait_choice() != KEY_A) return 0;
    char self[340], tmp[360];
    snprintf(self, sizeof(self), "%s",
             g_self_path[0] ? g_self_path
                            : APP_DIR "/ytmusic3ds-v" APP_VER ".3dsx");
    snprintf(tmp, sizeof(tmp), "%s/update.tmp", APP_DIR);
    if (updater_apply(&info, self, tmp, upd_cb, err,
                      sizeof(err)) != 0) {
        render_message("アップデート失敗", err);
        upd_wait_key(KEY_A | KEY_B | KEY_START);
        snprintf(g_update_msg, sizeof(g_update_msg), "失敗");
        return 0;
    }
    render_message("アップデート完了", "アプリを再起動してください");
    snprintf(g_update_msg, sizeof(g_update_msg), "再起動を");
    upd_wait_key(KEY_A | KEY_B | KEY_START);
    return 1;
}

int main(int argc, char **argv) {
    // Where am I? (for the self-update swap)
    if (argc > 0 && argv && argv[0] && strstr(argv[0], ".3dsx")) {
        if (strncmp(argv[0], "sdmc:", 5) == 0)
            strncpy(g_self_path, argv[0], sizeof(g_self_path) - 1);
        else if (argv[0][0] == '/')
            snprintf(g_self_path, sizeof(g_self_path), "sdmc:%s",
                     argv[0]);
    }
    if (gfx_setup() != 0) return 1;

    u32 *soc_buffer = (u32 *)memalign(0x1000, 0x100000);
    if (!soc_buffer || socInit(soc_buffer, 0x100000) != 0) {
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            render_message("socInit failed - is Wi-Fi connected?",
                           "Press START to exit.");
        }
        gfx_teardown();
        return 0;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    net_set_pump(net_pump); // v3.29: keep the UI alive in requests
    srand((unsigned)osGetTime());
    // v3.14: the app never created its own folder and silently
    // relied on the install zip to make it. Loaded any other way
    // (e.g. straight from an emulator), every write into APP_DIR
    // failed - the updater was the first feature to surface it
    // ("cannot create temp file"). Create it up front.
    mkdir("sdmc:/3ds", 0777);
    mkdir(APP_DIR, 0777);
    mkdir(APP_DIR "/cache", 0777); // thumbnail disk cache (v3.27)
    config_load();
    favs_load();

    draw_all();
    // v3.27 (user request): boot into the Library tab - the app
    // opens on the user's saved songs instead of the search
    // prompt. Search stays one tab away.
    switch_tab(TAB_LIBRARY);

    int exit_now = 0;
    while (aptMainLoop() && !exit_now) {
        if (g_tq_favs_dirty) {
            g_tq_favs_dirty = 0;
            if (g_tab == TAB_LIBRARY)
                thumb_queue_start(THUMB_BANK_FAVS);
        }
        thumb_queue_step();
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_START) break;
        // Touch: player-screen controls, or the tab bar.
        if (k & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            if (g_player_view) {
                idle_player_touch(tp.px, tp.py);
                if (g_ctl.exit_requested) exit_now = 1;
                render();
                continue;
            }
            if (g_has_np && tp.py >= MINI_TOP && tp.py < NAV_TOP) {
                // Mini player: button replays, body opens the
                // player screen.
                if (tp.px >= 260) {
                    play_from_player_view(0);
                    if (g_ctl.exit_requested) exit_now = 1;
                } else {
                    g_player_view = 1;
                }
                render();
                continue;
            }
            if (tp.py >= NAV_TOP) {
                int t = tp.px / 80;
                if (t == TAB_SEARCH)
                    do_search_flow();
                else if (t >= 0 && t < 4)
                    switch_tab(t);
                render();
                continue;
            }
        }
        // Player screen (idle): Y returns to the lists; A replays;
        // L/R play neighbours; X toggles the lyrics view.
        if (g_player_view) {
            if (k & KEY_Y) {
                g_player_view = 0;
            } else if (k & KEY_A) {
                play_from_player_view(0);
                if (g_ctl.exit_requested) exit_now = 1;
            } else if (k & KEY_R) {
                play_from_player_view(1);
                if (g_ctl.exit_requested) exit_now = 1;
            } else if (k & KEY_L) {
                play_from_player_view(-1);
                if (g_ctl.exit_requested) exit_now = 1;
            } else if (k & KEY_X) {
                g_lyrics_view = !g_lyrics_view;
                g_lyrics_scroll = 0;
            } else if (g_lyrics_view && (k & KEY_DOWN)) {
                g_lyrics_scroll++;
            } else if (g_lyrics_view && (k & KEY_UP) &&
                       g_lyrics_scroll > 0) {
                g_lyrics_scroll--;
            }
            render();
            continue;
        }
        if (k & KEY_R) {
            int next = g_tab == TAB_HOME ? TAB_LIBRARY
                       : g_tab == TAB_LIBRARY ? TAB_SETTINGS
                                              : TAB_HOME;
            switch_tab(next);
        } else if (k & KEY_L) {
            int prev = g_tab == TAB_HOME ? TAB_SETTINGS
                       : g_tab == TAB_SETTINGS ? TAB_LIBRARY
                                               : TAB_HOME;
            switch_tab(prev);
        } else if (k & KEY_Y) {
            // Y opens the player screen when a song exists;
            // otherwise it keeps its old search shortcut meaning.
            if (g_has_np)
                g_player_view = 1;
            else
                do_search_flow();
        } else if (g_tab == TAB_SETTINGS) {
            if (k & KEY_DOWN) {
                if (g_set_sel < 3) g_set_sel++;
            } else if (k & KEY_UP) {
                if (g_set_sel > 0) g_set_sel--;
            } else if (k & (KEY_LEFT | KEY_RIGHT | KEY_A)) {
                if (g_set_sel == 2) {
                    if (do_update_flow()) exit_now = 1;
                } else if (g_set_sel == 3) {
                    g_kana_on = !g_kana_on;
                    config_save();
                } else if (g_set_sel == 0) {
                    if (k & KEY_LEFT)
                        g_quality = (g_quality + 2) % 3;
                    else
                        g_quality = (g_quality + 1) % 3;
                    config_save();
                } else {
                    g_thumbs_on = !g_thumbs_on;
                    if (!g_thumbs_on) {
                        thumbs_clear_all();
                        g_tq_bank = -1;
                    }
                    config_save();
                }
            } else if (k & KEY_B) {
                switch_tab(TAB_HOME);
            }
        } else if (k & KEY_A) {
            if (act_count() > 0)
                do_play_flow();
            else if (g_tab == TAB_HOME)
                do_search_flow();
            if (g_ctl.exit_requested) exit_now = 1;
        } else if (k & KEY_X) {
            if (act_count() > 0) {
                result_t *r = act_at(g_selected);
                int rc = fav_toggle(r);
                if (rc == 1)
                    snprintf(g_status, sizeof(g_status),
                             "お気に入りに追加しました");
                else if (rc == 0) {
                    snprintf(g_status, sizeof(g_status),
                             "お気に入りから外しました");
                    if (g_tab == TAB_LIBRARY && g_selected >= act_count() &&
                        g_selected > 0)
                        g_selected = act_count() - 1;
                } else
                    snprintf(g_status, sizeof(g_status),
                             "お気に入りがいっぱいです");
            }
        } else if (k & KEY_B) {
            if (g_tab != TAB_HOME) switch_tab(TAB_HOME);
        } else if (k & KEY_DOWN) {
            if (g_selected + 1 < act_count()) g_selected++;
        } else if (k & KEY_UP) {
            if (g_selected > 0) g_selected--;
        } else if (k & KEY_RIGHT) {
            int n = g_selected + 5;
            if (n >= act_count()) n = act_count() - 1;
            if (n != g_selected && n >= 0) g_selected = n;
        } else if (k & KEY_LEFT) {
            int n = g_selected - 5;
            if (n < 0) n = 0;
            if (n != g_selected) g_selected = n;
        }
        render();
    }

    curl_global_cleanup();
    socExit();
    gfx_teardown();
    return 0;
}
