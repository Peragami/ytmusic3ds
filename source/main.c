// ytmusic3ds v3.15 - YouTube Music style client for (Old) 3DS.
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
//   - Like = favourite (same store as the Library tab). Bad =
//     records the song in dislikes.txt and skips to the next song.
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
//     (sdmc:/3ds/ytmusic3ds-v3.15/favorites.txt). X toggles a song
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

#include "icons.h"
#include "lyrics.h"
#include "updater.h"

#define APP_VER "3.15"

// v3.12: the update manifest URL is baked in (config.txt can
// still override it). Distribution lives in the dist/ folder of
// the user's GitHub repository.
#define DEFAULT_UPDATE_URL \
    "https://raw.githubusercontent.com/Peragami/ytmusic3ds/main/dist/version.json"
#include "net.h"
#include "player.h"
#include "thumb.h"

// The resolve/playback paths run deep curl + mbedTLS call chains on
// this thread; give the main stack real headroom (v3.1 crash fix).
u32 __stacksize__ = 256 * 1024;

// ---------- tabs ----------
#define TAB_HOME 0
#define TAB_SEARCH 1 // action only (opens the keyboard)
#define TAB_LIBRARY 2
#define TAB_SETTINGS 3
static int g_tab = TAB_HOME;

// ---------- state ----------
static char g_query[128] = "";
static int g_selected = 0;
static char g_status[160] = "A: search.";
static char g_route_label[96] = "-";

static play_ctl_t g_ctl;
static u64 g_last_draw_ms = 0;
static int g_playback_view = 0;
static int g_playing = -1; // index into the playing list
static int g_playing_tab = TAB_HOME;
static int g_switch_target = -1;
static result_t g_np_song; // now-playing (or last played) song
static int g_has_np = 0;

// ---------- v3.6 player screen ----------
static int g_player_view = 0; // 1 = player screen (artwork+controls)
static int g_lyrics_view = 0; // top screen shows lyrics instead
static int g_lyrics_scroll = 0;
static int g_shuffle = 0;
static int g_repeat = 0; // 0 off / 1 all / 2 one
// v3.9: HQ artwork + lyrics are fetched from the playback hook
// (while the song downloads) instead of before playback, so
// pressing play starts the song immediately.
static int g_deferred_pending = 0;

// ---------- settings (persisted) ----------
static int g_thumbs_on = 1; // g_quality lives in net.c
static int g_kana_on = 1;   // v3.13: flick/toggle keyboard
static int g_set_sel = 0;

// ---------- incremental thumbnail fetching (v3.8) ----------
// Thumbnails are fetched one per main-loop iteration so the UI
// stays responsive while they load (v3.7 fetched them in a
// blocking loop and froze the controls for seconds).
static int g_tq_bank = -1; // bank being fetched, or -1
static int g_tq_next = 0;
static int g_tq_total = 0;
static int g_tq_favs_dirty = 0; // favourites bank was invalidated

#define APP_DIR "sdmc:/3ds/ytmusic3ds-v3.15"
#define FAV_PATH APP_DIR "/favorites.txt"
#define CFG_PATH APP_DIR "/config.txt"
#define DISLIKE_PATH APP_DIR "/dislikes.txt"
// v3.5 stored its data in its own version folder; import from
// there when the v3.6 files do not exist yet.
#define LEGACY_FAV_PATH "sdmc:/3ds/ytmusic3ds-v3.14/favorites.txt"
#define LEGACY_CFG_PATH "sdmc:/3ds/ytmusic3ds-v3.14/config.txt"

// ---------- favourites ----------
static result_t g_favs[THUMB_MAX_FAVS];
static int g_fav_count = 0;

static int fav_find(const char *id) {
    for (int i = 0; i < g_fav_count; i++)
        if (strcmp(g_favs[i].id, id) == 0) return i;
    return -1;
}

static void sanitize_field(char *s) {
    for (; *s; s++)
        if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' ';
}

static void favs_save(void) {
    FILE *f = fopen(FAV_PATH, "w");
    if (!f) return;
    for (int i = 0; i < g_fav_count; i++) {
        char id[VID_LEN], t[TITLE_LEN], s[SUB_LEN];
        strncpy(id, g_favs[i].id, sizeof(id) - 1);
        id[sizeof(id) - 1] = '\0';
        strncpy(t, g_favs[i].title, sizeof(t) - 1);
        t[sizeof(t) - 1] = '\0';
        strncpy(s, g_favs[i].sub, sizeof(s) - 1);
        s[sizeof(s) - 1] = '\0';
        sanitize_field(t);
        sanitize_field(s);
        fprintf(f, "%s\t%s\t%s\n", id, t, s);
    }
    fclose(f);
}

static void favs_load(void) {
    g_fav_count = 0;
    FILE *f = fopen(FAV_PATH, "r");
    if (!f) f = fopen(LEGACY_FAV_PATH, "r");
    if (!f) return;
    char line[VID_LEN + TITLE_LEN + SUB_LEN + 8];
    while (g_fav_count < THUMB_MAX_FAVS && fgets(line, sizeof(line), f)) {
        char *t1 = strchr(line, '\t');
        if (!t1) continue;
        char *t2 = strchr(t1 + 1, '\t');
        if (!t2) continue;
        *t1 = '\0';
        *t2 = '\0';
        char *nl = strchr(t2 + 1, '\n');
        if (nl) *nl = '\0';
        result_t *r = &g_favs[g_fav_count];
        memset(r, 0, sizeof(*r));
        strncpy(r->id, line, VID_LEN - 1);
        strncpy(r->title, t1 + 1, TITLE_LEN - 1);
        strncpy(r->sub, t2 + 1, SUB_LEN - 1);
        if (r->id[0] && r->title[0]) g_fav_count++;
    }
    fclose(f);
}

// Toggle favourite for a song; returns 1 = added, 0 = removed,
// -1 = library full. Thumbnail bank indices shift on removal, so
// the whole favourites bank is dropped and refetched lazily.
static int fav_toggle(const result_t *song) {
    int idx = fav_find(song->id);
    if (idx >= 0) {
        for (int i = idx; i + 1 < g_fav_count; i++)
            g_favs[i] = g_favs[i + 1];
        g_fav_count--;
        favs_save();
        thumbs_clear_bank(THUMB_BANK_FAVS);
        g_tq_favs_dirty = 1;
        if (g_tq_bank == THUMB_BANK_FAVS) g_tq_bank = -1;
        return 0;
    }
    if (g_fav_count >= THUMB_MAX_FAVS) return -1;
    g_favs[g_fav_count++] = *song;
    favs_save();
    thumbs_clear_bank(THUMB_BANK_FAVS);
    g_tq_favs_dirty = 1;
    if (g_tq_bank == THUMB_BANK_FAVS) g_tq_bank = -1;
    return 1;
}

// ---------- config ----------
// v3.11: update配信URL (空なら未設定) と自分の.3dsxパス
static char g_update_url[384] = "";
static char g_self_path[320] = "";
static char g_update_msg[96] = "";

static void config_save(void) {
    FILE *f = fopen(CFG_PATH, "w");
    if (!f) return;
    fprintf(f, "quality=%d\nthumbs=%d\nshuffle=%d\nrepeat=%d\n",
            g_quality, g_thumbs_on, g_shuffle, g_repeat);
    fprintf(f, "kana=%d\n", g_kana_on);
    if (g_update_url[0]) fprintf(f, "update_url=%s\n", g_update_url);
    fclose(f);
}

static void config_load(void) {
    // Default first; a config value (own or legacy) overrides it.
    strncpy(g_update_url, DEFAULT_UPDATE_URL,
            sizeof(g_update_url) - 1);
    g_update_url[sizeof(g_update_url) - 1] = '\0';
    FILE *f = fopen(CFG_PATH, "r");
    if (!f) f = fopen(LEGACY_CFG_PATH, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        int v;
        if (strncmp(line, "update_url=", 11) == 0) {
            strncpy(g_update_url, line + 11, sizeof(g_update_url) - 1);
            g_update_url[sizeof(g_update_url) - 1] = '\0';
            g_update_url[strcspn(g_update_url, "\r\n")] = '\0';
        } else if (sscanf(line, "quality=%d", &v) == 1 && v >= 0 && v <= 2)
            g_quality = v;
        else if (sscanf(line, "thumbs=%d", &v) == 1)
            g_thumbs_on = v ? 1 : 0;
        else if (sscanf(line, "shuffle=%d", &v) == 1)
            g_shuffle = v ? 1 : 0;
        else if (sscanf(line, "kana=%d", &v) == 1)
            g_kana_on = v ? 1 : 0;
        else if (sscanf(line, "repeat=%d", &v) == 1 && v >= 0 && v <= 2)
            g_repeat = v;
    }
    fclose(f);
}

// ---------- dislikes (Bad button) ----------
static int dislike_has(const char *id) {
    FILE *f = fopen(DISLIKE_PATH, "r");
    if (!f) return 0;
    char line[32];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, id) == 0) {
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

static void dislike_add(const char *id) {
    if (dislike_has(id)) return;
    FILE *f = fopen(DISLIKE_PATH, "a");
    if (!f) return;
    fprintf(f, "%s\n", id);
    fclose(f);
}

// ---------- active list (results or favourites) ----------
static int list_tab(void) { return g_playback_view ? g_playing_tab : g_tab; }

static int act_count(void) {
    return list_tab() == TAB_LIBRARY ? g_fav_count : g_result_count;
}

static result_t *act_at(int i) {
    if (list_tab() == TAB_LIBRARY) return &g_favs[i];
    return &g_results[i];
}

static int act_bank(void) {
    return list_tab() == TAB_LIBRARY ? THUMB_BANK_FAVS : THUMB_BANK_RESULTS;
}

// ---------- citro2d text (system font: full Japanese) ----------
static C3D_RenderTarget *g_top_tgt = NULL;
static C3D_RenderTarget *g_bot_tgt = NULL;
static C2D_Font g_font = NULL;
static C2D_TextBuf g_textbuf = NULL;

// YouTube Music palette (v3.7: matched to the app - near-black
// background, #212121 elevated surfaces, #AAAAAA secondary text,
// white progress; red survives only as the brand accent).
#define COL_BG C2D_Color32(3, 3, 3, 255)
#define COL_WHITE C2D_Color32(255, 255, 255, 255)
#define COL_DIM C2D_Color32(170, 170, 170, 255)
#define COL_FAINT C2D_Color32(113, 113, 113, 255)
#define COL_ACCENT C2D_Color32(255, 0, 0, 255)
#define COL_RULE C2D_Color32(45, 45, 45, 255)
#define COL_SURFACE C2D_Color32(33, 33, 33, 255)
#define COL_NAV C2D_Color32(33, 33, 33, 255)
#define COL_CHIP C2D_Color32(42, 42, 42, 255)
#define COL_THUMB C2D_Color32(52, 52, 52, 255)
#define COL_TRACK C2D_Color32(63, 63, 63, 255)

static void txt(float x, float y, float scale, u32 color, const char *str) {
    if (!str || !str[0]) return;
    C2D_Text t;
    C2D_TextFontParse(&t, g_font, g_textbuf, str);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

static void utf8_truncate(const char *in, char *out, size_t out_size,
                          size_t max_bytes) {
    size_t len = strlen(in);
    if (len <= max_bytes) {
        strncpy(out, in, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    size_t cut = max_bytes;
    while (cut > 0 && (in[cut] & 0xC0) == 0x80) cut--;
    if (cut + 3 >= out_size) cut = out_size - 4;
    memcpy(out, in, cut);
    memcpy(out + cut, "..", 2);
    out[cut + 2] = '\0';
}

static void wrap_status(const char *in, char *out, size_t out_size) {
    size_t len = strlen(in);
    if (len < 46 || out_size < len + 2) {
        strncpy(out, in, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    size_t split = 0;
    for (size_t i = 0; i < len && i < 52; i++)
        if (in[i] == ' ') split = i;
    if (split < 16) {
        strncpy(out, in, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    memcpy(out, in, split);
    out[split] = '\n';
    strncpy(out + split + 1, in + split + 1, out_size - split - 2);
    out[out_size - 1] = '\0';
}

// ---------- shared drawing pieces ----------
static void draw_logo(float x, float y) {
    C2D_DrawCircleSolid(x + 13, y + 13, 0.4f, 13, COL_ACCENT);
    C2D_DrawTriangle(x + 10, y + 7, COL_WHITE, x + 10, y + 19, COL_WHITE,
                     x + 19, y + 13, COL_WHITE, 0.45f);
    txt(x + 32, y + 3, 0.7f, COL_WHITE, "Music");
}

// Rounded-ish pill (rect + two end circles).
static void draw_pill(float x, float y, float w, float h, u32 col) {
    C2D_DrawCircleSolid(x + h / 2, y + h / 2, 0.35f, h / 2, col);
    C2D_DrawCircleSolid(x + w - h / 2, y + h / 2, 0.35f, h / 2, col);
    C2D_DrawRectSolid(x + h / 2, y, 0.35f, w - h, h, col);
}

// Thumbnail (or placeholder block) at x,y drawn size x size.
static void draw_thumb(int bank, int idx, float x, float y, float size,
                       int is_playing) {
    C2D_Image *img = thumb_get(bank, idx);
    if (img) {
        float d = (float)thumb_dim(bank);
        C2D_DrawImageAt(*img, x, y, 0.4f, NULL, size / d, size / d);
        return;
    }
    C2D_DrawRectSolid(x, y, 0.35f, size, size,
                      is_playing ? COL_ACCENT : COL_THUMB);
    draw_icon(ICON_MUSIC_NOTE, x + size * 0.22f, y + size * 0.22f,
              size * 0.56f, COL_DIM);
}

static void draw_progress(float x, float y, float w, int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    C2D_DrawRectSolid(x, y, 0.4f, w, 4, COL_TRACK);
    if (pct > 0)
        C2D_DrawRectSolid(x, y, 0.45f, w * pct / 100.0f, 4, COL_WHITE);
    C2D_DrawCircleSolid(x + w * pct / 100.0f, y + 2, 0.46f, 5, COL_WHITE);
}

// ---------- top screen: song list (home / library) ----------
#define ROW_H 32.0f
#define ROWS_VISIBLE 4
#define LIST_TOP 104.0f

static void draw_list_scene(void) {
    draw_logo(10, 8);
    txt(356, 14, 0.45f, COL_FAINT, "v3.15");
    // Chips row: context chip + count chip + quality chip.
    char chip[112];
    if (list_tab() == TAB_LIBRARY)
        snprintf(chip, sizeof(chip), "お気に入り");
    else if (g_query[0]) {
        char qt[80];
        utf8_truncate(g_query, qt, sizeof(qt), 66);
        snprintf(chip, sizeof(chip), "%s", qt);
    } else
        snprintf(chip, sizeof(chip), "おすすめ");
    draw_pill(10, 40, 150, 20, COL_SURFACE);
    txt(22, 43, 0.44f, COL_WHITE, chip);
    char cnt[24];
    snprintf(cnt, sizeof(cnt), "%d 曲", act_count());
    draw_pill(170, 40, 74, 20, COL_SURFACE);
    txt(182, 43, 0.44f, COL_DIM, cnt);
    draw_pill(254, 40, 110, 20, COL_SURFACE);
    txt(266, 43, 0.44f, COL_DIM,
        g_quality == 0 ? "音質: 低" : g_quality == 2 ? "音質: 高" : "音質: 標準");
    // Section title.
    if (list_tab() == TAB_LIBRARY)
        txt(12, 68, 0.72f, COL_WHITE, "ライブラリ");
    else if (g_query[0]) {
        char qt[80];
        char st[96];
        utf8_truncate(g_query, qt, sizeof(qt), 66);
        snprintf(st, sizeof(st), "「%s」の結果", qt);
        txt(12, 68, 0.72f, COL_WHITE, st);
    } else
        txt(12, 68, 0.72f, COL_WHITE, "おすすめ");
    if (act_count() == 0) {
        txt(12, LIST_TOP + 6, 0.55f, COL_WHITE,
            list_tab() == TAB_LIBRARY ? "お気に入りはまだありません"
                                      : "まだ結果がありません");
        txt(12, LIST_TOP + 34, 0.5f, COL_DIM,
            list_tab() == TAB_LIBRARY ? "ホームで X を押すと追加できます"
                                      : "下の「検索」タブか A ボタンで検索");
        return;
    }
    int start = g_selected - 1;
    if (start < 0) start = 0;
    if (start + ROWS_VISIBLE > act_count()) start = act_count() - ROWS_VISIBLE;
    if (start < 0) start = 0;
    for (int row = 0; row < ROWS_VISIBLE; row++) {
        int i = start + row;
        if (i >= act_count()) break;
        float y = LIST_TOP + row * ROW_H;
        int is_sel = (i == g_selected);
        int is_playing =
            (g_playback_view && i == g_playing) ||
            (!g_playback_view && g_has_np &&
             strcmp(act_at(i)->id, g_np_song.id) == 0 &&
             list_tab() == g_playing_tab);
        if (is_sel)
            C2D_DrawRectSolid(6, y, 0.3f, 388, ROW_H - 4, COL_SURFACE);
        draw_thumb(act_bank(), i, 12, y + 1, 26, is_playing);
        if (is_playing) {
            C2D_DrawRectSolid(42, y + 12, 0.4f, 3, 8, COL_WHITE);
            C2D_DrawRectSolid(47, y + 7, 0.4f, 3, 13, COL_WHITE);
            C2D_DrawRectSolid(52, y + 14, 0.4f, 3, 6, COL_WHITE);
        }
        char t[128];
        utf8_truncate(act_at(i)->title, t, sizeof(t), 76);
        txt(60, y + 0, 0.5f, is_playing ? COL_ACCENT : COL_WHITE, t);
        if (act_at(i)->sub[0]) {
            char s[128];
            utf8_truncate(act_at(i)->sub, s, sizeof(s), 84);
            txt(60, y + 15, 0.38f, COL_DIM, s);
        }
        if (fav_find(act_at(i)->id) >= 0)
            draw_icon(ICON_BOOKMARK, 376, y + 5, 16, COL_ACCENT);
    }
    if (act_count() > ROWS_VISIBLE) {
        char pg[32];
        snprintf(pg, sizeof(pg), "%d / %d", g_selected + 1, act_count());
        txt(348, 222, 0.42f, COL_FAINT, pg);
    }
    // Incremental thumbnail fetch progress (v3.8).
    if (g_tq_bank >= 0 && g_tq_bank == act_bank()) {
        int total = act_count();
        int have = 0;
        for (int i = 0; i < total; i++)
            if (thumb_get(g_tq_bank, i)) have++;
        char pr[48];
        snprintf(pr, sizeof(pr), "サムネイル取得中 %d/%d", have, total);
        txt(12, 222, 0.42f, COL_FAINT, pr);
    }
}

// ---------- top screen: settings ----------
static const char *quality_name(void) {
    return g_quality == 0 ? "低（約64kbps）"
           : g_quality == 2 ? "高（約160kbps）"
                            : "標準（約80kbps）";
}

static void draw_settings_scene(void) {
    draw_logo(10, 8);
    txt(12, 44, 0.72f, COL_WHITE, "設定");
    float y = 76;
    if (g_set_sel == 0)
        C2D_DrawRectSolid(6, y - 4, 0.3f, 388, 34, COL_SURFACE);
    txt(14, y, 0.55f, COL_WHITE, "音質");
    txt(190, y, 0.55f, COL_ACCENT, quality_name());
    y += 37;
    if (g_set_sel == 1)
        C2D_DrawRectSolid(6, y - 4, 0.3f, 388, 34, COL_SURFACE);
    txt(14, y, 0.55f, COL_WHITE, "サムネイル表示");
    txt(190, y, 0.55f, COL_ACCENT, g_thumbs_on ? "オン" : "オフ");
    y += 37;
    if (g_set_sel == 2)
        C2D_DrawRectSolid(6, y - 4, 0.3f, 388, 34, COL_SURFACE);
    txt(14, y, 0.55f, COL_WHITE, "アップデート");
    txt(190, y, 0.55f, COL_ACCENT,
        g_update_msg[0] ? g_update_msg : "A：確認する");
    y += 37;
    if (g_set_sel == 3)
        C2D_DrawRectSolid(6, y - 4, 0.3f, 388, 34, COL_SURFACE);
    txt(14, y, 0.55f, COL_WHITE, "フリック入力");
    txt(190, y, 0.55f, COL_ACCENT, g_kana_on ? "オン" : "オフ");
    y += 40;
    txt(14, y, 0.46f, COL_DIM, "十字キー上下: 項目   左右/A: 変更");
    y += 20;
    txt(14, y, 0.46f, COL_FAINT,
        "オフにすると本体のキーボードを使います");
}

// ---------- v3.6 player screen ----------
static int g_lyrics_state = 0; // 0 not tried / 1 found / 2 not found

static void fmt_time(char *out, size_t cap, unsigned sec) {
    snprintf(out, cap, "%u:%02u", sec / 60, sec % 60);
}

// Touch zones on the player screen's bottom half. Layout constants
// are shared by the drawing code and the hit tests:
//   pills row y 54..90:  like x<104, bad x<196, lyrics else
//   controls y 138..202: five 64px zones (shuffle/prev/play/next/repeat)
static int player_zone(int px, int py) {
    if (py >= 54 && py < 90) {
        if (px < 104) return 1; // like
        if (px < 196) return 2; // bad
        if (px < 288) return 3; // lyrics
        return 0;
    }
    if (py >= 138 && py < 202) return 4 + (px / 64); // 4..8
    return 0;
}

static void draw_top_player_scene(void) {
    if (g_lyrics_view) {
        txt(12, 8, 0.62f, COL_WHITE, "歌詞");
        char t[96];
        utf8_truncate(g_np_song.title, t, sizeof(t), 60);
        txt(70, 12, 0.5f, COL_DIM, t);
        int total = lyrics_line_count();
        if (g_lyrics_state == 0) {
            txt(12, 70, 0.55f, COL_WHITE,
                g_playback_view ? "歌詞取得中..."
                                : "歌詞は再生中に取得します");
        } else if (g_lyrics_state == 2 || total == 0) {
            txt(12, 70, 0.55f, COL_WHITE,
                "歌詞が見つかりませんでした");
            txt(12, 98, 0.46f, COL_DIM, lyrics_debug());
        } else {
            int max_scroll = total - 11;
            if (max_scroll < 0) max_scroll = 0;
            if (g_lyrics_scroll > max_scroll)
                g_lyrics_scroll = max_scroll;
            if (g_lyrics_scroll < 0) g_lyrics_scroll = 0;
            for (int i = 0; i < 11; i++) {
                int li = g_lyrics_scroll + i;
                if (li >= total) break;
                char line[128];
                utf8_truncate(lyrics_line(li), line, sizeof(line), 66);
                txt(12, 40 + i * 18, 0.5f,
                    line[0] ? COL_WHITE : COL_WHITE, line);
            }
            char pg[32];
            snprintf(pg, sizeof(pg), "%d/%d", g_lyrics_scroll + 1,
                     total);
            txt(352, 222, 0.42f, COL_FAINT, pg);
        }
        txt(12, 222, 0.42f, COL_FAINT, "十字キー: スクロール");
        return;
    }
    // Big jacket artwork centred on the top screen. Until the HQ
    // image lands (fetched during the download), fall back to the
    // playing song's list thumbnail so the screen is never blank.
    if (!thumb_get(THUMB_BANK_NP, 0) && g_playing >= 0) {
        int bank = g_playing_tab == TAB_LIBRARY ? THUMB_BANK_FAVS
                                                : THUMB_BANK_RESULTS;
        draw_thumb(bank, g_playing, 96, 12, 208, 1);
    } else {
        draw_thumb(THUMB_BANK_NP, 0, 96, 12, 208, 1);
    }
    txt(12, 226, 0.42f, COL_FAINT, "Y: 一覧に戻る");
    if (g_lyrics_state == 1)
        txt(300, 226, 0.42f, COL_DIM, "歌詞あり");
}

static void draw_bottom_player(void) {
    play_ctl_t *ctl = &g_ctl;
    if (!g_has_np) {
        txt(8, 12, 0.55f, COL_WHITE, "再生中の曲はありません");
        txt(8, 44, 0.46f, COL_DIM, "Y: 一覧に戻る");
        return;
    }
    char t[96];
    utf8_truncate(g_np_song.title, t, sizeof(t), 48);
    txt(8, 6, 0.62f, COL_WHITE, t);
    if (g_np_song.sub[0]) {
        char s[128];
        utf8_truncate(g_np_song.sub, s, sizeof(s), 62);
        txt(8, 32, 0.46f, COL_DIM, s);
    }
    // Pills: like / bad / lyrics (Material icons + labels; the
    // active state is shown by the accent-tinted icon).
    int liked = fav_find(g_np_song.id) >= 0;
    draw_pill(8, 56, 92, 26, COL_CHIP);
    draw_icon(ICON_THUMB_UP, 18, 60, 18,
              liked ? COL_ACCENT : COL_WHITE);
    txt(42, 61, 0.5f, COL_WHITE, "いいね");
    draw_pill(108, 56, 84, 26, COL_CHIP);
    draw_icon(ICON_THUMB_DOWN, 118, 60, 18, COL_WHITE);
    txt(142, 61, 0.5f, COL_WHITE, "バッド");
    draw_pill(200, 56, 84, 26, COL_CHIP);
    draw_icon(ICON_MIC, 210, 60, 18,
              g_lyrics_view ? COL_ACCENT : COL_WHITE);
    txt(234, 61, 0.5f, COL_WHITE, "歌詞");
    // Message line.
    const char *msg = g_playback_view ? ctl->message : g_status;
    char m[128];
    utf8_truncate(msg, m, sizeof(m), 72);
    txt(8, 90, 0.42f, COL_DIM, m);
    // Progress + times.
    int pct = 0;
    unsigned e = ctl->elapsed_sec, tot = ctl->total_sec;
    if (g_playback_view && ctl->phase == PHASE_DOWNLOAD) {
        if (ctl->dl_total > 0)
            pct = (int)(ctl->dl_bytes * 100 / ctl->dl_total);
        char dl[64];
        snprintf(dl, sizeof(dl), "ダウンロード中 %d%%  %u/%u KB", pct,
                 (unsigned)(ctl->dl_bytes / 1024),
                 (unsigned)(ctl->dl_total / 1024));
        txt(8, 128, 0.44f, COL_WHITE, dl);
    } else {
        if (tot > 0) pct = (int)(e * 100 / tot);
        char te[16], tt[16];
        fmt_time(te, sizeof(te), e);
        fmt_time(tt, sizeof(tt), tot);
        txt(8, 128, 0.44f, COL_WHITE, te);
        txt(282, 128, 0.44f, COL_DIM, tt);
    }
    draw_progress(8, 112, 304, pct);
    // Controls row (Material icons).
    float cy = 168;
    u32 c_shuffle = g_shuffle ? COL_ACCENT : COL_DIM;
    draw_icon(ICON_SHUFFLE, 18, cy - 13, 26, c_shuffle);
    txt(12, 188, 0.36f, c_shuffle, "シャッフル");
    draw_icon(ICON_SKIP_PREVIOUS, 81, cy - 15, 30, COL_WHITE);
    txt(88, 188, 0.36f, COL_DIM, "前へ");
    // play / pause: big white circle with a dark glyph
    int show_pause = g_playback_view && ctl->phase == PHASE_PLAY &&
                     !ctl->paused;
    C2D_DrawCircleSolid(160, cy, 0.45f, 28, COL_WHITE);
    draw_icon(show_pause ? ICON_PAUSE : ICON_PLAY_ARROW, 143, cy - 17,
              34, COL_BG);
    draw_icon(ICON_SKIP_NEXT, 209, cy - 15, 30, COL_WHITE);
    txt(212, 188, 0.36f, COL_DIM, "次へ");
    u32 c_repeat = g_repeat ? COL_ACCENT : COL_DIM;
    draw_icon(g_repeat == 2 ? ICON_REPEAT_ONE : ICON_REPEAT, 275,
              cy - 13, 26, c_repeat);
    txt(262, 188, 0.36f, c_repeat,
        g_repeat == 2 ? "リピート:1曲"
                      : g_repeat == 1 ? "リピート:全部"
                                      : "リピート");
    txt(8, 222, 0.42f, COL_FAINT, "Y: 一覧に戻る");
    char rl[96];
    snprintf(rl, sizeof(rl), "Route: %s", g_route_label);
    txt(150, 222, 0.38f, COL_FAINT,
        g_playback_view ? rl : "タッチでも操作できます");
}

// ---------- bottom screen: mini player + tab bar (v3.7) ----------
#define NAV_TOP 188.0f
#define MINI_TOP 133.0f
#define PROG_TOP 185.0f

static const int tab_icons[4] = {ICON_HOME, ICON_SEARCH,
                                 ICON_BOOKMARK_BORDER, ICON_SETTINGS};

static void draw_nav(void) {
    C2D_DrawRectSolid(0, NAV_TOP, 0.3f, 320, 240 - NAV_TOP, COL_NAV);
    static const char *labels[4] = {"ホーム", "検索", "ライブラリ", "設定"};
    int active = g_playback_view ? g_playing_tab : g_tab;
    for (int t = 0; t < 4; t++) {
        float x = t * 80.0f;
        int is_active = (t == active);
        u32 col = is_active ? COL_WHITE : COL_DIM;
        draw_icon(tab_icons[t], x + 28, NAV_TOP + 5, 24, col);
        float lx = x + (t == TAB_LIBRARY ? 14 : 22);
        txt(lx, NAV_TOP + 33, 0.42f, col, labels[t]);
    }
}

// The app's mini player: jacket, title, artist, play/pause button,
// sitting directly above the progress strip and the tab bar.
static void draw_mini_player(void) {
    if (!g_has_np) return;
    C2D_DrawRectSolid(0, MINI_TOP, 0.3f, 320, PROG_TOP - MINI_TOP,
                      COL_SURFACE);
    draw_thumb(THUMB_BANK_NP, 0, 8, MINI_TOP + 6, 40, 0);
    char t[96];
    utf8_truncate(g_np_song.title, t, sizeof(t), 44);
    txt(56, MINI_TOP + 7, 0.5f, COL_WHITE, t);
    if (g_np_song.sub[0]) {
        char s[128];
        utf8_truncate(g_np_song.sub, s, sizeof(s), 50);
        txt(56, MINI_TOP + 28, 0.4f, COL_DIM, s);
    }
    int playing_now = g_playback_view && g_ctl.phase == PHASE_PLAY &&
                      !g_ctl.paused;
    draw_icon(playing_now ? ICON_PAUSE : ICON_PLAY_ARROW, 274,
              MINI_TOP + 10, 32, COL_WHITE);
    // Progress strip between the mini player and the tab bar.
    int pct = 0;
    if (g_playback_view && g_ctl.phase == PHASE_DOWNLOAD) {
        if (g_ctl.dl_total > 0)
            pct = (int)(g_ctl.dl_bytes * 100 / g_ctl.dl_total);
    } else if (g_ctl.total_sec > 0) {
        pct = (int)(g_ctl.elapsed_sec * 100 / g_ctl.total_sec);
    }
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    C2D_DrawRectSolid(0, PROG_TOP, 0.32f, 320, NAV_TOP - PROG_TOP,
                      COL_TRACK);
    if (pct > 0)
        C2D_DrawRectSolid(0, PROG_TOP, 0.33f, 320.0f * pct / 100.0f,
                          NAV_TOP - PROG_TOP, COL_WHITE);
}

// ---------- bottom screen: now-playing / details ----------
static void draw_bottom_playback(play_ctl_t *ctl) {
    // Content area above the mini player (v3.7 layout).
    float y = 8;
    txt(8, y, 0.5f, COL_WHITE, "再生中");
    y += 22;
    int bank = g_playing_tab == TAB_LIBRARY ? THUMB_BANK_FAVS
                                            : THUMB_BANK_RESULTS;
    draw_thumb(bank, g_playing, 8, y, 40, 1);
    {
        char t[96];
        utf8_truncate(g_np_song.title, t, sizeof(t), 52);
        txt(56, y + 2, 0.55f, COL_WHITE, t);
        if (g_np_song.sub[0]) {
            char s[128];
            utf8_truncate(g_np_song.sub, s, sizeof(s), 58);
            txt(56, y + 24, 0.42f, COL_DIM, s);
        }
    }
    y += 48;
    char line[160];
    if (ctl->phase == PHASE_DOWNLOAD) {
        if (ctl->dl_total > 0) {
            int pct = (int)(ctl->dl_bytes * 100 / ctl->dl_total);
            snprintf(line, sizeof(line), "ダウンロード中  %d%%   %u/%u KB",
                     pct, (unsigned)(ctl->dl_bytes / 1024),
                     (unsigned)(ctl->dl_total / 1024));
        } else {
            snprintf(line, sizeof(line), "ダウンロード中  %u KB",
                     (unsigned)(ctl->dl_bytes / 1024));
        }
        txt(8, y, 0.46f, COL_WHITE, line);
        y += 18;
    } else if (ctl->phase == PHASE_PLAY) {
        unsigned e = ctl->elapsed_sec, t = ctl->total_sec;
        if (t > 0)
            snprintf(line, sizeof(line), "%u:%02u / %u:%02u", e / 60,
                     e % 60, t / 60, t % 60);
        else
            snprintf(line, sizeof(line), "再生中  %u:%02u", e / 60, e % 60);
        txt(8, y, 0.46f, COL_WHITE, line);
        y += 18;
    }
    char msg[128];
    utf8_truncate(ctl->message, msg, sizeof(msg), 66);
    txt(8, y, 0.42f, COL_DIM, msg);
    y += 16;
    if (g_selected != g_playing && act_count() > 0) {
        char sw[128];
        char t[96];
        utf8_truncate(act_at(g_selected)->title, t, sizeof(t), 40);
        snprintf(sw, sizeof(sw), "A: この曲に切り替え  %s", t);
        txt(8, y, 0.42f, COL_ACCENT, sw);
    } else {
        char rl[112];
        snprintf(rl, sizeof(rl), "Route: %s", g_route_label);
        txt(8, y, 0.4f, COL_FAINT, rl);
    }
    draw_mini_player();
    draw_nav();
}

static void draw_bottom_idle(void) {
    float y = 10;
    if (act_count() > 0 && g_tab != TAB_SETTINGS) {
        result_t *r = act_at(g_selected);
        int playing_this = g_has_np && strcmp(r->id, g_np_song.id) == 0;
        txt(8, y, 0.48f, playing_this ? COL_ACCENT : COL_DIM,
            playing_this ? "前回再生" : "選択中");
        y += 22;
        draw_thumb(act_bank(), g_selected, 8, y, 44, playing_this);
        char t[96];
        utf8_truncate(r->title, t, sizeof(t), 54);
        txt(60, y + 2, 0.55f, COL_WHITE, t);
        if (r->sub[0]) {
            char s[128];
            utf8_truncate(r->sub, s, sizeof(s), 60);
            txt(60, y + 24, 0.42f, COL_DIM, s);
        }
        y += 54;
    } else {
        txt(8, y, 0.5f, COL_DIM,
            g_tab == TAB_SETTINGS ? "設定" : "曲を選んでください");
        y += 32;
    }
    char st[192];
    wrap_status(g_status, st, sizeof(st));
    txt(8, y, 0.46f, COL_WHITE, st);
    y += 30;
    if (g_tab != TAB_SETTINGS && act_count() > 0 && !g_has_np) {
        draw_pill(8, y, 118, 26, COL_ACCENT);
        draw_icon(ICON_PLAY_ARROW, 20, y + 4, 18, COL_WHITE);
        txt(42, y + 5, 0.52f, COL_WHITE, "再生");
        txt(134, y + 6, 0.44f, COL_DIM, "A   X: お気に入り");
    } else if (g_tab != TAB_SETTINGS && act_count() > 0) {
        // With a mini player present it carries the play control.
        txt(8, y, 0.44f, COL_DIM, "A: 再生   X: お気に入り");
    } else if (g_tab == TAB_SETTINGS) {
        txt(8, y, 0.46f, COL_DIM, "変更は自動で保存されます");
    }
    if (g_has_np) {
        draw_mini_player();
    } else {
        txt(8, 168, 0.42f, COL_FAINT,
            "タブはタッチでも切り替えできます（L/Rでも可）");
    }
    draw_nav();
}

// ---------- frame ----------
static void render(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(g_textbuf);
    C2D_TargetClear(g_top_tgt, COL_BG);
    C2D_SceneBegin(g_top_tgt);
    if (g_player_view)
        draw_top_player_scene();
    else if (g_tab == TAB_SETTINGS && !g_playback_view)
        draw_settings_scene();
    else
        draw_list_scene();
    C2D_TargetClear(g_bot_tgt, COL_BG);
    C2D_SceneBegin(g_bot_tgt);
    if (g_player_view)
        draw_bottom_player();
    else if (g_playback_view)
        draw_bottom_playback(&g_ctl);
    else
        draw_bottom_idle();
    C3D_FrameEnd(0);
}

static void draw_all(void) { render(); }

static void render_message(const char *l1, const char *l2) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(g_textbuf);
    C2D_TargetClear(g_top_tgt, COL_BG);
    C2D_SceneBegin(g_top_tgt);
    txt(10, 90, 0.6f, COL_WHITE, l1);
    txt(10, 120, 0.6f, COL_DIM, l2);
    C2D_TargetClear(g_bot_tgt, COL_BG);
    C2D_SceneBegin(g_bot_tgt);
    C3D_FrameEnd(0);
}

static void artist_from_sub(const char *sub, char *out, size_t cap);

// ---------- playback UI hook ----------
// Touch actions on the player screen while playing. Zones come
// from player_zone(); like/bad/lyrics/shuffle/repeat act in place,
// prev/next/pause drive the player through ctl.
static void hook_player_touch(play_ctl_t *ctl, int px, int py) {
    switch (player_zone(px, py)) {
    case 1:
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
    if (g_deferred_pending) {
        g_deferred_pending = 0;
        if (g_thumbs_on) thumb_fetch(THUMB_BANK_NP, 0, g_np_song.id);
        char artist[64];
        artist_from_sub(g_np_song.sub, artist, sizeof(artist));
        g_lyrics_state =
            lyrics_fetch(g_np_song.id, g_np_song.title, artist) == 0
                ? 1
                : 2;
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

// Fetch at most one missing thumbnail per call; the main loop
// calls this every iteration so input stays responsive.
static void thumb_queue_step(void) {
    if (g_tq_bank < 0) return;
    int bank = g_tq_bank;
    while (g_tq_next < g_tq_total) {
        int idx = g_tq_next++;
        if (thumb_get(bank, idx)) continue;
        thumb_fetch(bank, idx,
                    bank == THUMB_BANK_FAVS ? g_favs[idx].id
                                            : g_results[idx].id);
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

// ---------- flick / toggle keyboard (layout v2, v3.15) ----------
// Modelled on the user's smartphone keyboard screenshots:
// a 5x4 grid - utility keys in the left/right columns, kana /
// letter / digit keys in the middle, three modes cycled by the
// bottom-left pill. Tap repeatedly to cycle (toggle input),
// flick up/down/left/right for direct input, with a preview
// popup. The text has a real cursor: the triangle keys move it,
// input inserts at it, delete removes the character before it.
typedef struct {
    const char *ch[10];
    int n;
    const char *label;
    const char *sub; // small caption under the label (may be NULL)
} kkey_t;

// Key tables: ch[0] = tap, ch[1] = flick left, ch[2] = up,
// ch[3] = right, ch[4] = down, rest = toggle-cycle continuation.
static const kkey_t KANA_KEYS[12] = {
    {{"あ", "い", "う", "え", "お"}, 5, "あ", NULL},
    {{"か", "き", "く", "け", "こ"}, 5, "か", NULL},
    {{"さ", "し", "す", "せ", "そ"}, 5, "さ", NULL},
    {{"た", "ち", "つ", "て", "と"}, 5, "た", NULL},
    {{"な", "に", "ぬ", "ね", "の"}, 5, "な", NULL},
    {{"は", "ひ", "ふ", "へ", "ほ"}, 5, "は", NULL},
    {{"ま", "み", "む", "め", "も"}, 5, "ま", NULL},
    {{"や", "（", "ゆ", "）", "よ"}, 5, "や", NULL},
    {{"ら", "り", "る", "れ", "ろ"}, 5, "ら", NULL},
    {{NULL}, 0, "゛゜小", NULL}, // centre bottom-left: transform
    {{"わ", "を", "ん", "ー", "～"}, 5, "わ", NULL},
    {{"、", "。", "？", "！", "・"}, 5, "、", NULL},
};

static const kkey_t ABC_KEYS[12] = {
    {{"@", "-", "_", "/", "1"}, 5, "@", "1"},
    {{"a", "b", "c", "2"}, 4, "ABC", "2"},
    {{"d", "e", "f", "3"}, 4, "DEF", "3"},
    {{"g", "h", "i", "4"}, 4, "GHI", "4"},
    {{"j", "k", "l", "5"}, 4, "JKL", "5"},
    {{"m", "n", "o", "6"}, 4, "MNO", "6"},
    {{"p", "q", "r", "s", "7"}, 5, "PQRS", "7"},
    {{"t", "u", "v", "8"}, 4, "TUV", "8"},
    {{"w", "x", "y", "z", "9"}, 5, "WXYZ", "9"},
    {{NULL}, 0, "a⇔A", NULL}, // centre bottom-left: case
    {{"0", "'", "\"", ":", ";", ","}, 6, "0", NULL},
    {{".", ",", "?", "!", "'"}, 5, ".,", NULL},
};

static const kkey_t NUM_KEYS[12] = {
    {{"1", "☆", "♪", "→"}, 4, "1", "☆♪→"},
    {{"2", "¥", "$", "€"}, 4, "2", "¥$€"},
    {{"3", "%", "゜", "#"}, 4, "3", "%゜#"},
    {{"4", "○", "*", "・"}, 4, "4", "○*・"},
    {{"5", "+", "×", "÷"}, 4, "5", "+×÷"},
    {{"6", "<", "=", ">"}, 4, "6", "<=>"},
    {{"7", "「", "」", ":"}, 4, "7", "「」:"},
    {{"8", "〒", "々", "〆"}, 4, "8", "〒々〆"},
    {{"9", "^", "|", "\\"}, 4, "9", "^|\\"},
    {{"(", ")", "[", "]"}, 4, "()[]", NULL},
    {{"0", "~", "…", "@"}, 4, "0", "~…@"},
    {{".", ",", "-", "/"}, 4, ".,", "-/"},
};

// Dakuten / small-kana transform chains (the transform key
// advances the character before the cursor within its chain).
static const char *const DAKU_CHAINS[][5] = {
    {"あ", "ぁ", NULL}, {"い", "ぃ", NULL}, {"う", "ゔ", "ぅ", NULL},
    {"え", "ぇ", NULL}, {"お", "ぉ", NULL}, {"か", "が", NULL},
    {"き", "ぎ", NULL}, {"く", "ぐ", NULL}, {"け", "げ", NULL},
    {"こ", "ご", NULL}, {"さ", "ざ", NULL}, {"し", "じ", NULL},
    {"す", "ず", NULL}, {"せ", "ぜ", NULL}, {"そ", "ぞ", NULL},
    {"た", "だ", NULL}, {"ち", "ぢ", NULL}, {"つ", "づ", "っ", NULL},
    {"て", "で", NULL}, {"と", "ど", NULL}, {"は", "ば", "ぱ", NULL},
    {"ひ", "び", "ぴ", NULL}, {"ふ", "ぶ", "ぷ", NULL},
    {"へ", "べ", "ぺ", NULL}, {"ほ", "ぼ", "ぽ", NULL},
    {"や", "ゃ", NULL}, {"ゆ", "ゅ", NULL}, {"よ", "ょ", NULL},
    {"わ", "ゎ", NULL},
};
#define DAKU_COUNT (sizeof(DAKU_CHAINS) / sizeof(DAKU_CHAINS[0]))

// Special (non-character) keys.
enum {
    SP_NONE = -1,
    SP_CENTER = 0, // value >= 100: special; centre keys are 0..11
    SP_BACK = 100,
    SP_DEL,
    SP_LEFT,
    SP_RIGHT,
    SP_SYMBOL,
    SP_SPACE,
    SP_MODE,
    SP_CONFIRM,
    SP_TRANSFORM
};

#define KB_X(c) (2 + (c) * 64)
#define KB_Y(r) (46 + (r) * 48)
#define KB_W 60
#define KB_H 44

static float txtw(const char *str, float scale) {
    if (!str || !str[0]) return 0;
    C2D_Text t;
    C2D_TextFontParse(&t, g_font, g_textbuf, str);
    float w = 0, h = 0;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}

static const char *tail_fit(const char *s, float scale, float maxw) {
    while (*s && txtw(s, scale) > maxw) {
        s++;
        while ((*s & 0xC0) == 0x80) s++;
    }
    return s;
}

// ---------- UTF-8 cursor helpers ----------
static int kb_prev_pos(const char *buf, int pos) {
    int s = pos - 1;
    while (s > 0 && (buf[s] & 0xC0) == 0x80) s--;
    return s < 0 ? 0 : s;
}

static int kb_next_pos(const char *buf, int pos) {
    int len = (int)strlen(buf);
    int s = pos + 1;
    while (s < len && (buf[s] & 0xC0) == 0x80) s++;
    return s;
}

// Width of buf[a..b) at a scale (measured on a scratch copy).
static float kb_seg_w(const char *buf, int a, int b, float scale) {
    char tmp[160];
    int n = b - a;
    if (n <= 0) return 0;
    if (n > 159) n = 159;
    memcpy(tmp, buf + a, n);
    tmp[n] = '\0';
    return txtw(tmp, scale);
}

static void kb_insert(char *buf, int *cur, const char *s,
                      size_t cap) {
    size_t bl = strlen(buf), sl = strlen(s);
    if (bl + sl >= cap) return;
    memmove(buf + *cur + sl, buf + *cur, bl - *cur + 1);
    memcpy(buf + *cur, s, sl);
    *cur += (int)sl;
}

static void kb_delete_before(char *buf, int *cur) {
    if (*cur <= 0) return;
    int p = kb_prev_pos(buf, *cur);
    memmove(buf + p, buf + *cur, strlen(buf + *cur) + 1);
    *cur = p;
}

// Replace the character before the cursor with s.
static void kb_replace_before(char *buf, int *cur, const char *s,
                              size_t cap) {
    if (*cur <= 0) {
        kb_insert(buf, cur, s, cap);
        return;
    }
    int save = *cur;
    kb_delete_before(buf, cur);
    kb_insert(buf, cur, s, cap);
    (void)save;
}

static void kb_cycle(char *buf, int *cur, const kkey_t *k,
                     size_t cap) {
    if (*cur <= 0) return;
    int p = kb_prev_pos(buf, *cur);
    int cl = *cur - p;
    char cur8[8];
    if (cl > 7) return;
    memcpy(cur8, buf + p, cl);
    cur8[cl] = '\0';
    for (int i = 0; i < k->n; i++) {
        if (strcmp(cur8, k->ch[i]) == 0) {
            kb_replace_before(buf, cur, k->ch[(i + 1) % k->n], cap);
            return;
        }
    }
    kb_replace_before(buf, cur, k->ch[0], cap);
}

static void kb_transform(char *buf, int *cur, int mode, size_t cap) {
    if (*cur <= 0) return;
    int p = kb_prev_pos(buf, *cur);
    int cl = *cur - p;
    char cur8[8];
    if (cl > 7) return;
    memcpy(cur8, buf + p, cl);
    cur8[cl] = '\0';
    if (mode == 1) { // ABC: toggle case of the letter before cursor
        if (cl == 1 && cur8[0] >= 'a' && cur8[0] <= 'z') {
            cur8[0] -= 'a' - 'A';
            kb_replace_before(buf, cur, cur8, cap);
        } else if (cl == 1 && cur8[0] >= 'A' && cur8[0] <= 'Z') {
            cur8[0] += 'a' - 'A';
            kb_replace_before(buf, cur, cur8, cap);
        }
        return;
    }
    if (mode != 0) return;
    for (size_t c = 0; c < DAKU_COUNT; c++) {
        int n = 0;
        while (DAKU_CHAINS[c][n]) n++;
        for (int i = 0; i < n; i++) {
            if (strcmp(cur8, DAKU_CHAINS[c][i]) == 0) {
                kb_replace_before(buf, cur,
                                  DAKU_CHAINS[c][(i + 1) % n], cap);
                return;
            }
        }
    }
}

// Hit test: returns SP_* for specials or the centre index 0..11.
static int kb_key_at(int x, int y, int *col, int *row) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 5; c++) {
            int kx = KB_X(c), ky = KB_Y(r);
            if (x >= kx && x < kx + KB_W && y >= ky &&
                y < ky + KB_H) {
                *col = c;
                *row = r;
                if (c == 1 || c == 2 || c == 3)
                    return r * 3 + (c - 1);
                if (c == 0 && r == 0) return SP_BACK;
                if (c == 4 && r == 0) return SP_DEL;
                if (c == 0 && r == 1) return SP_LEFT;
                if (c == 4 && r == 1) return SP_RIGHT;
                if (c == 0 && r == 2) return SP_SYMBOL;
                if (c == 4 && r == 2) return SP_SPACE;
                if (c == 0 && r == 3) return SP_MODE;
                return SP_CONFIRM; // c == 4 && r == 3
            }
        }
    }
    return SP_NONE;
}

// Draw buf with a cursor bar, keeping the cursor visible.
static void kb_draw_text(float x, float y, float scale,
                         const char *buf, int cur, float maxw) {
    int len = (int)strlen(buf);
    int start = cur;
    while (start > 0) {
        int p = kb_prev_pos(buf, start);
        if (kb_seg_w(buf, p, cur, scale) > maxw * 0.55f) break;
        start = p;
    }
    int end = cur;
    while (end < len) {
        int n = kb_next_pos(buf, end);
        if (kb_seg_w(buf, start, n, scale) > maxw) break;
        end = n;
    }
    char tmp[160];
    int n = end - start;
    if (n > 159) n = 159;
    memcpy(tmp, buf + start, n);
    tmp[n] = '\0';
    txt(x, y, scale, COL_WHITE, tmp);
    float cx = x + kb_seg_w(buf, start, cur, scale);
    C2D_DrawRectSolid(cx + 1, y + 1, 0.45f, 2.5f, scale * 34,
                      COL_ACCENT);
}

static void kb_key_rect(int col, int row, int active, u32 fill) {
    C2D_DrawRectSolid(KB_X(col), KB_Y(row), 0.3f, KB_W, KB_H,
                      active ? C2D_Color32(58, 58, 58, 255) : fill);
}

static void kb_center_label(int col, int row, const char *s,
                            float scale, float dy) {
    float w = txtw(s, scale);
    txt(KB_X(col) + (KB_W - w) / 2, KB_Y(row) + dy, scale, COL_WHITE,
        s);
}

static void kana_render(const char *buf, int cur, int mode,
                        int active_id, int active_col, int active_row,
                        int dir, int pending) {
    const kkey_t *keys = mode == 0   ? KANA_KEYS
                         : mode == 1 ? ABC_KEYS
                                     : NUM_KEYS;
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(g_textbuf);
    // ---- top screen ----
    C2D_TargetClear(g_top_tgt, COL_BG);
    C2D_SceneBegin(g_top_tgt);
    txt(12, 14, 0.7f, COL_WHITE, "検索");
    if (buf[0])
        kb_draw_text(12, 62, 0.78f, buf, cur, 372);
    else
        txt(12, 62, 0.6f, COL_FAINT, "キーワードを入力");
    txt(12, 128, 0.48f, COL_DIM,
        "タップ連打： トグル　フリック： 上下左右");
    txt(12, 154, 0.48f, COL_DIM,
        "◀▶： カーソル移動　B： もどる　START： 検索");
    // ---- bottom screen ----
    C2D_TargetClear(g_bot_tgt, COL_BG);
    C2D_SceneBegin(g_bot_tgt);
    C2D_DrawRectSolid(0, 0, 0.3f, 320, 40, COL_SURFACE);
    kb_draw_text(8, 8, 0.55f, buf, cur, 300);
    (void)pending;
    // utility keys
    kb_key_rect(0, 0, active_id == SP_BACK, COL_SURFACE);
    kb_center_label(0, 0, "戻る", 0.5f, 11);
    kb_key_rect(4, 0, active_id == SP_DEL, COL_SURFACE);
    kb_center_label(4, 0, "削除", 0.5f, 11);
    kb_key_rect(0, 1, active_id == SP_LEFT, COL_SURFACE);
    C2D_DrawTriangle(KB_X(0) + 38, KB_Y(1) + 11, COL_WHITE,
                     KB_X(0) + 38, KB_Y(1) + 33, COL_WHITE,
                     KB_X(0) + 22, KB_Y(1) + 22, COL_WHITE, 0.4f);
    kb_key_rect(4, 1, active_id == SP_RIGHT, COL_SURFACE);
    C2D_DrawTriangle(KB_X(4) + 22, KB_Y(1) + 11, COL_WHITE,
                     KB_X(4) + 22, KB_Y(1) + 33, COL_WHITE,
                     KB_X(4) + 38, KB_Y(1) + 22, COL_WHITE, 0.4f);
    kb_key_rect(0, 2, active_id == SP_SYMBOL, COL_SURFACE);
    kb_center_label(0, 2, "記号", 0.5f, 11);
    kb_key_rect(4, 2, active_id == SP_SPACE, COL_SURFACE);
    kb_center_label(4, 2, "空白", 0.5f, 11);
    // mode pill (bottom-left): あ / a / 1, current one lit
    kb_key_rect(0, 3, active_id == SP_MODE, COL_SURFACE);
    {
        static const char *ML[3] = {"あ", "a", "1"};
        for (int m = 0; m < 3; m++) {
            txt(KB_X(0) + 8 + m * 17, KB_Y(3) + 12, 0.55f,
                m == mode ? COL_WHITE : COL_FAINT, ML[m]);
        }
    }
    kb_key_rect(4, 3, active_id == SP_CONFIRM, COL_ACCENT);
    kb_center_label(4, 3, "検索", 0.55f, 10);
    // centre keys
    for (int i = 0; i < 12; i++) {
        int c = (i % 3) + 1, r = i / 3;
        int is_tr = (i == 9 && mode != 2);
        kb_key_rect(c, r, active_id == i, COL_SURFACE);
        float sc = strlen(keys[i].label) > 3 ? 0.44f
                   : strlen(keys[i].label) > 1 ? 0.52f
                                              : 0.68f;
        kb_center_label(c, r, keys[i].label, sc, keys[i].sub ? 5 : 9);
        if (keys[i].sub) {
            float w = txtw(keys[i].sub, 0.32f);
            txt(KB_X(c) + (KB_W - w) / 2, KB_Y(r) + 28, 0.32f,
                COL_DIM, keys[i].sub);
        }
        (void)is_tr;
    }
    // flick preview popup
    if (active_id >= 0 && active_id < 12) {
        const kkey_t *k = &keys[active_id];
        if (k->n > 0) {
            const char *pc = (dir >= 1 && dir < k->n) ? k->ch[dir]
                                                      : k->ch[0];
            int kx = KB_X(active_col), ky = KB_Y(active_row);
            int px = kx + KB_W / 2 - 26;
            int py = ky - 50 >= 42 ? ky - 50 : ky + KB_H + 4;
            C2D_DrawRectSolid(px, py, 0.45f, 52, 44,
                              C2D_Color32(58, 58, 58, 255));
            float w = txtw(pc, 0.95f);
            txt(px + (52 - w) / 2, py + 4, 0.95f, COL_WHITE, pc);
        }
    }
    C3D_FrameEnd(0);
}

static int kana_prompt(void) {
    char buf[sizeof(g_query)];
    strncpy(buf, g_query, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int cur = (int)strlen(buf);
    int mode = 0;     // 0 kana / 1 ABC / 2 numbers+symbols
    int pending = -1; // centre key whose char can still be cycled
    int t_active = 0, t_id = SP_NONE, t_col = 0, t_row = 0;
    int t_sx = 0, t_sy = 0, t_dir = 0;
    int held_id = SP_NONE;
    u64 held_since = 0, last_repeat = 0;
    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown();
        u32 kh = hidKeysHeld();
        if (kd & KEY_B) return -1;
        if ((kd & KEY_START) && buf[0]) {
            strncpy(g_query, buf, sizeof(g_query) - 1);
            g_query[sizeof(g_query) - 1] = '\0';
            return 0;
        }
        if (kd & KEY_X) {
            kb_delete_before(buf, &cur);
            pending = -1;
        }
        touchPosition tp;
        hidTouchRead(&tp);
        int touching = (kh & KEY_TOUCH) != 0;
        u64 now = osGetTime();
        // --- special-key actions (on press, some auto-repeat) ---
        // handled inline below via do_special lambda-like block
        if (touching && !t_active) {
            t_active = 1;
            t_sx = tp.px;
            t_sy = tp.py;
            t_dir = 0;
            t_id = kb_key_at(tp.px, tp.py, &t_col, &t_row);
            if (t_id >= SP_BACK) {
                held_id = t_id;
                held_since = now;
                last_repeat = now;
                if (t_id == SP_BACK) return -1;
                if (t_id == SP_CONFIRM) {
                    if (buf[0]) {
                        strncpy(g_query, buf,
                                sizeof(g_query) - 1);
                        g_query[sizeof(g_query) - 1] = '\0';
                        return 0;
                    }
                } else if (t_id == SP_DEL) {
                    kb_delete_before(buf, &cur);
                    pending = -1;
                } else if (t_id == SP_LEFT) {
                    if (cur > 0) cur = kb_prev_pos(buf, cur);
                    pending = -1;
                } else if (t_id == SP_RIGHT) {
                    if (buf[cur]) cur = kb_next_pos(buf, cur);
                    pending = -1;
                } else if (t_id == SP_SPACE) {
                    kb_insert(buf, &cur, " ", sizeof(buf));
                    pending = -1;
                } else if (t_id == SP_MODE) {
                    mode = (mode + 1) % 3;
                    pending = -1;
                } else if (t_id == SP_SYMBOL) {
                    mode = mode == 2 ? 0 : 2;
                    pending = -1;
                }
            } else if (t_id == 9 && mode != 2) {
                // centre bottom-left is the transform key in
                // kana/ABC modes: act on press.
                kb_transform(buf, &cur, mode, sizeof(buf));
                pending = -1;
                held_id = SP_TRANSFORM;
                held_since = now;
                last_repeat = now;
            }
        } else if (touching && t_active) {
            if (t_id >= 0 && t_id < 12) {
                int dx = tp.px - t_sx, dy = tp.py - t_sy;
                if (dx * dx + dy * dy < 14 * 14)
                    t_dir = 0;
                else if (abs(dx) > abs(dy))
                    t_dir = dx > 0 ? 3 : 1;
                else
                    t_dir = dy > 0 ? 4 : 2;
            }
            // auto-repeat for delete / cursor keys
            if ((held_id == SP_DEL || held_id == SP_LEFT ||
                 held_id == SP_RIGHT) &&
                now - held_since > 350 && now - last_repeat > 110) {
                last_repeat = now;
                if (held_id == SP_DEL)
                    kb_delete_before(buf, &cur);
                else if (held_id == SP_LEFT && cur > 0)
                    cur = kb_prev_pos(buf, cur);
                else if (held_id == SP_RIGHT && buf[cur])
                    cur = kb_next_pos(buf, cur);
                pending = -1;
            }
        } else if (!touching && t_active) {
            t_active = 0;
            if (t_id >= 0 && t_id < 12 && !(t_id == 9 && mode != 2)) {
                const kkey_t *keys = mode == 0   ? KANA_KEYS
                                     : mode == 1 ? ABC_KEYS
                                                 : NUM_KEYS;
                const kkey_t *k = &keys[t_id];
                if (t_dir == 0) {
                    if (pending == t_id)
                        kb_cycle(buf, &cur, k, sizeof(buf));
                    else
                        kb_insert(buf, &cur, k->ch[0],
                                  sizeof(buf));
                    pending = t_id;
                } else {
                    const char *c =
                        t_dir < k->n ? k->ch[t_dir] : k->ch[0];
                    if (pending == t_id)
                        kb_replace_before(buf, &cur, c,
                                          sizeof(buf));
                    else
                        kb_insert(buf, &cur, c, sizeof(buf));
                    pending = -1;
                }
            }
            t_id = SP_NONE;
            held_id = SP_NONE;
            t_dir = 0;
        }
        kana_render(buf, cur, mode, t_active ? t_id : SP_NONE,
                    t_col, t_row, t_dir, pending);
    }
    return -1;
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

static void do_search_flow(void) {
    if (prompt_query() != 0) return;
    snprintf(g_status, sizeof(g_status), "Searching...");
    draw_all();
    thumbs_clear_bank(THUMB_BANK_RESULTS);
    if (yt_search(g_query) == 0) {
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
    if (resolve_stream(r->id, url, sizeof(url), &itag, &length_sec) != 0) {
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

// ---------- gfx ----------
static int gfx_setup(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    g_top_tgt = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bot_tgt = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_font = C2D_FontLoadSystem(CFG_REGION_JPN);
    g_textbuf = C2D_TextBufNew(16384);
    icons_init();
    return (g_top_tgt && g_bot_tgt && g_textbuf) ? 0 : -1;
}

static void gfx_teardown(void) {
    if (g_textbuf) C2D_TextBufDelete(g_textbuf);
    if (g_font) C2D_FontFree(g_font);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
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
        int rc = fav_toggle(&g_np_song);
        snprintf(g_status, sizeof(g_status),
                 rc == 1 ? "お気に入りに追加しました"
                 : rc == 0 ? "お気に入りから外しました"
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
                            : APP_DIR "/ytmusic3ds-v3.15.3dsx");
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
    srand((unsigned)osGetTime());
    // v3.14: the app never created its own folder and silently
    // relied on the install zip to make it. Loaded any other way
    // (e.g. straight from an emulator), every write into APP_DIR
    // failed - the updater was the first feature to surface it
    // ("cannot create temp file"). Create it up front.
    mkdir("sdmc:/3ds", 0777);
    mkdir(APP_DIR, 0777);
    config_load();
    favs_load();

    draw_all();
    do_search_flow();

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
