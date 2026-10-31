// ui.c - all rendering: text core, scenes for the list,
// settings, player and lyrics screens, the mini player
// and the tab bar, plus graphics setup. Split out of
// main.c in v3.17; logic unchanged.

#include "ui.h"

#include "icons.h"
#include "lyrics.h"
#include "store.h"

// ---------- citro2d text (system font: full Japanese) ----------
C3D_RenderTarget *g_top_tgt = NULL;
C3D_RenderTarget *g_bot_tgt = NULL;
C2D_Font g_font = NULL;
C2D_TextBuf g_textbuf = NULL;

// YouTube Music palette (v3.7: matched to the app; v3.20:
// pure black background and nav bar like the app - elevated
// surfaces #212121, #AAAAAA secondary text, white progress;
// red survives only as the brand accent).
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

void txt(float x, float y, float scale, u32 color, const char *str) {
    if (!str || !str[0]) return;
    C2D_Text t;
    C2D_TextFontParse(&t, g_font, g_textbuf, str);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

void utf8_truncate(const char *in, char *out, size_t out_size,
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

// Rounded rectangle (v3.20: the app's cards and selected rows
// have soft corners - centre + edge rects + 4 corner circles).
static void draw_rrect(float x, float y, float w, float h, float r,
                       u32 col) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    C2D_DrawRectSolid(x + r, y, 0.32f, w - 2 * r, h, col);
    C2D_DrawRectSolid(x, y + r, 0.32f, r, h - 2 * r, col);
    C2D_DrawRectSolid(x + w - r, y + r, 0.32f, r, h - 2 * r, col);
    C2D_DrawCircleSolid(x + r, y + r, 0.32f, r, col);
    C2D_DrawCircleSolid(x + w - r, y + r, 0.32f, r, col);
    C2D_DrawCircleSolid(x + r, y + h - r, 0.32f, r, col);
    C2D_DrawCircleSolid(x + w - r, y + h - r, 0.32f, r, col);
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
    // Placeholder rounding follows the texture rounding:
    // gentle on the big jacket (v3.26), chunkier on list
    // thumbnails.
    draw_rrect(x, y, size, size,
               size * (size >= 100 ? 0.05f : 0.14f),
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
// v3.20: chunkier rows and bigger chips, closer to the app.
#define ROW_H 34.0f
#define ROWS_VISIBLE 4
#define LIST_TOP 90.0f

static void draw_list_scene(void) {
    draw_logo(10, 8);
    txt(356, 14, 0.45f, COL_FAINT, "v" APP_VER);
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
    draw_pill(10, 38, 150, 24, COL_SURFACE);
    txt(24, 43, 0.48f, COL_WHITE, chip);
    char cnt[24];
    snprintf(cnt, sizeof(cnt), "%d 曲", act_count());
    draw_pill(168, 38, 74, 24, COL_SURFACE);
    txt(182, 43, 0.48f, COL_WHITE, cnt);
    draw_pill(250, 38, 114, 24, COL_SURFACE);
    txt(264, 43, 0.48f, COL_WHITE,
        g_quality == 0 ? "音質: 低" : g_quality == 2 ? "音質: 高" : "音質: 標準");
    // Section title.
    if (list_tab() == TAB_LIBRARY)
        txt(12, 66, 0.7f, COL_WHITE, "ライブラリ");
    else if (g_query[0]) {
        char qt[80];
        char st[96];
        utf8_truncate(g_query, qt, sizeof(qt), 66);
        snprintf(st, sizeof(st), "「%s」の結果", qt);
        txt(12, 66, 0.7f, COL_WHITE, st);
    } else
        txt(12, 66, 0.7f, COL_WHITE, "おすすめ");
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
            draw_rrect(6, y, 388, ROW_H - 4, 7, COL_SURFACE);
        draw_thumb(act_bank(), i, 12, y + 1, 28, is_playing);
        if (is_playing) {
            C2D_DrawRectSolid(46, y + 13, 0.4f, 3, 8, COL_WHITE);
            C2D_DrawRectSolid(51, y + 8, 0.4f, 3, 13, COL_WHITE);
            C2D_DrawRectSolid(56, y + 15, 0.4f, 3, 6, COL_WHITE);
        }
        char t[128];
        utf8_truncate(act_at(i)->title, t, sizeof(t), 76);
        txt(66, y + 1, 0.52f, is_playing ? COL_ACCENT : COL_WHITE, t);
        if (act_at(i)->sub[0]) {
            char s[128];
            utf8_truncate(act_at(i)->sub, s, sizeof(s), 84);
            txt(66, y + 18, 0.4f, COL_DIM, s);
        }
        if (fav_find(act_at(i)->id) >= 0)
            draw_icon(ICON_BOOKMARK, 374, y + 6, 18, COL_WHITE);
    }
    if (act_count() > ROWS_VISIBLE) {
        char pg[32];
        snprintf(pg, sizeof(pg), "%d / %d", g_selected + 1, act_count());
        txt(348, 228, 0.42f, COL_FAINT, pg);
    }
    // Incremental thumbnail fetch progress (v3.8).
    if (g_tq_bank >= 0 && g_tq_bank == act_bank()) {
        int total = act_count();
        int have = 0;
        for (int i = 0; i < total; i++)
            if (thumb_get(g_tq_bank, i)) have++;
        char pr[96];
        if (thumb_worker_ok())
            snprintf(pr, sizeof(pr), "サムネイル取得中 %d/%d", have,
                     total);
        else
            snprintf(pr, sizeof(pr),
                     "サムネイル取得中 %d/%d（スレッド未動作）", have,
                     total);
        txt(12, 228, 0.42f, COL_FAINT, pr);
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
        draw_rrect(6, y - 4, 388, 34, 8, COL_SURFACE);
    txt(14, y, 0.55f, COL_WHITE, "音質");
    txt(190, y, 0.55f, COL_ACCENT, quality_name());
    y += 37;
    if (g_set_sel == 1)
        draw_rrect(6, y - 4, 388, 34, 8, COL_SURFACE);
    txt(14, y, 0.55f, COL_WHITE, "サムネイル表示");
    txt(190, y, 0.55f, COL_ACCENT, g_thumbs_on ? "オン" : "オフ");
    y += 37;
    if (g_set_sel == 2)
        draw_rrect(6, y - 4, 388, 34, 8, COL_SURFACE);
    txt(14, y, 0.55f, COL_WHITE, "アップデート");
    txt(190, y, 0.55f, COL_ACCENT,
        g_update_msg[0] ? g_update_msg : "A：確認する");
    y += 37;
    if (g_set_sel == 3)
        draw_rrect(6, y - 4, 388, 34, 8, COL_SURFACE);
    txt(14, y, 0.55f, COL_WHITE, "フリック入力");
    txt(190, y, 0.55f, COL_ACCENT, g_kana_on ? "オン" : "オフ");
    y += 40;
    txt(14, y, 0.46f, COL_DIM, "十字キー上下: 項目   左右/A: 変更");
    y += 20;
    txt(14, y, 0.46f, COL_FAINT,
        "オフにすると本体のキーボードを使います");
}

// ---------- v3.6 player screen ----------
int g_lyrics_state = 0; // 0 not tried / 1 found / 2 not found

static void fmt_time(char *out, size_t cap, unsigned sec) {
    snprintf(out, cap, "%u:%02u", sec / 60, sec % 60);
}

// Japanese-style compact count for the like pill: 9999 以下は
// そのまま、以上は 万 / 億 with one decimal when it helps.
static void fmt_count(char *out, size_t cap, long n) {
    if (n < 10000) {
        snprintf(out, cap, "%ld", n);
        return;
    }
    double v = n >= 100000000L ? n / 1e8 : n / 1e4;
    const char *unit = n >= 100000000L ? "億" : "万";
    if (v >= 100 || v == (double)(long)v)
        snprintf(out, cap, "%ld%s", (long)v, unit);
    else
        snprintf(out, cap, "%.1f%s", v, unit);
}

static float text_width(const char *str, float scale);

// Player pill layout (v3.25, user report: with a short like
// count the fixed divider position left a wide empty gap
// between Like and Bad). Like the real app, the combined pill
// hugs its content: the divider sits just after the count, Bad
// right after the divider, and the Lyrics / Save pills flow
// after the combined pill. Outputs: divider x, combined pill
// right edge, lyrics pill x, save pill x.
static void pill_layout(int *div_x, int *pill_end, int *lyrics_x,
                        int *save_x) {
    int dx = 46; // no count known: divider right after the icon
    if (g_np_likes >= 0) {
        char lc[24];
        fmt_count(lc, sizeof(lc), g_np_likes);
        dx = 40 + (int)(text_width(lc, 0.5f) + 0.5f) + 12;
    }
    *div_x = dx;
    *pill_end = dx + 11 + 18 + 13; // pad + bad icon + pad
    *lyrics_x = *pill_end + 8;
    *save_x = *lyrics_x + 72 + 8;
}

// Touch zones on the player screen's bottom half. Layout constants
// are shared by the drawing code and the hit tests:
//   pills row y 54..90: combined like|bad pill (divider from
//   pill_layout), then the lyrics pill, then save (zone 9)
//   controls y 138..202: five 64px zones (shuffle/prev/play/next/repeat)
int player_zone(int px, int py) {
    if (py >= 54 && py < 90) {
        int dx, pe, lx, sx;
        pill_layout(&dx, &pe, &lx, &sx);
        if (px < dx) return 1;  // like
        if (px < lx) return 2;  // bad
        if (px < sx) return 3;  // lyrics
        return 9;               // save
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
    // v3.31 (user request): jacket a little bigger, 208 -> 224.
    if (!thumb_get(THUMB_BANK_NP, 0) && g_playing >= 0) {
        int bank = g_playing_tab == TAB_LIBRARY ? THUMB_BANK_FAVS
                                                : THUMB_BANK_RESULTS;
        draw_thumb(bank, g_playing, 88, 8, 224, 1);
    } else {
        draw_thumb(THUMB_BANK_NP, 0, 88, 8, 224, 1);
    }
    // v3.26 (user request): no "Y: 一覧に戻る" hint on the top
    // screen - the jacket view stays clean like the real app.
    // The lyrics badge moved beside the jacket (v3.31): at 224
    // the old spot under the jacket no longer exists.
    if (g_lyrics_state == 1)
        txt(320, 12, 0.42f, COL_DIM, "歌詞あり");
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
    txt(8, 4, 0.68f, COL_WHITE, t);
    if (g_np_song.sub[0]) {
        char s[128];
        utf8_truncate(g_np_song.sub, s, sizeof(s), 62);
        txt(8, 31, 0.48f, COL_DIM, s);
    }
    // Pills (v3.24, user request - match the real app): Like
    // and Bad share ONE pill split by a divider, the Lyrics
    // pill carries the real app's quote-mark glyph and its
    // label again, and a Save pill sits on the right. Like is
    // independent from the Library now (likes.txt); Save is
    // the favourite toggle, same as the X key.
    int liked = like_has(g_np_song.id);
    int div_x, pill_end, lyrics_x, save_x;
    pill_layout(&div_x, &pill_end, &lyrics_x, &save_x);
    draw_pill(6, 56, pill_end - 6, 26, COL_CHIP);
    // White OUTLINE when not liked, filled WHITE when liked
    // (v3.22, user request) - it never turns red.
    draw_icon(liked ? ICON_THUMB_UP : ICON_THUMB_UP_OFF,
              16, 60, 18, COL_WHITE);
    if (g_np_likes >= 0) {
        char lc[24];
        fmt_count(lc, sizeof(lc), g_np_likes);
        txt(40, 61, 0.5f, COL_WHITE, lc);
    }
    C2D_DrawRectSolid(div_x, 62, 0.42f, 1.5f, 14, COL_DIM);
    // Bad: same treatment (v3.23) - outline / filled white.
    draw_icon(dislike_has(g_np_song.id) ? ICON_THUMB_DOWN
                                        : ICON_THUMB_DOWN_OFF,
              div_x + 11, 60, 18, COL_WHITE);
    draw_pill(lyrics_x, 56, 72, 26, COL_CHIP);
    draw_icon(ICON_LYRICS, lyrics_x + 8, 60, 18,
              g_lyrics_view ? COL_ACCENT : COL_WHITE);
    txt(lyrics_x + 32, 61, 0.5f, COL_WHITE, "歌詞");
    int saved = fav_find(g_np_song.id) >= 0;
    draw_pill(save_x, 56, 66, 26, COL_CHIP);
    draw_icon(saved ? ICON_PLAYLIST_ADD_CHECK : ICON_PLAYLIST_ADD,
              save_x + 7, 60, 18, COL_WHITE);
    txt(save_x + 31, 61, 0.5f, COL_WHITE, "保存");
    // Message line. The routine "Playing" status is not shown
    // (v3.16 user request); real messages still are.
    const char *msg = g_playback_view ? ctl->message : g_status;
    if (strcmp(msg, "Playing") == 0) msg = "";
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
    // Controls row (Material icons only - no text captions,
    // v3.16 user request; state reads from icon colour/glyph).
    float cy = 168;
    u32 c_shuffle = g_shuffle ? COL_ACCENT : COL_DIM;
    draw_icon(ICON_SHUFFLE, 18, cy - 13, 26, c_shuffle);
    draw_icon(ICON_SKIP_PREVIOUS, 81, cy - 15, 30, COL_WHITE);
    // play / pause: big white circle with a dark glyph
    int show_pause = g_playback_view && ctl->phase == PHASE_PLAY &&
                     !ctl->paused;
    C2D_DrawCircleSolid(160, cy, 0.45f, 28, COL_WHITE);
    draw_icon(show_pause ? ICON_PAUSE : ICON_PLAY_ARROW, 143, cy - 17,
              34, COL_BG);
    draw_icon(ICON_SKIP_NEXT, 209, cy - 15, 30, COL_WHITE);
    u32 c_repeat = g_repeat ? COL_ACCENT : COL_DIM;
    draw_icon(g_repeat == 2 ? ICON_REPEAT_ONE : ICON_REPEAT, 275,
              cy - 13, 26, c_repeat);
    txt(8, 222, 0.42f, COL_FAINT, "Y: 一覧に戻る");
    char rl[96];
    snprintf(rl, sizeof(rl), "Route: %s", g_route_label);
    txt(150, 222, 0.38f, COL_FAINT,
        g_playback_view ? rl : "タッチでも操作できます");
}

// ---------- bottom screen: mini player + tab bar (v3.7) ----------
// NAV_TOP / MINI_TOP / PROG_TOP live in ui.h: the touch
// handlers in main.c share the same layout constants.

static const int tab_icons[4] = {ICON_HOME, ICON_SEARCH,
                                 ICON_BOOKMARK_BORDER, ICON_SETTINGS};

// Rendered width of a string at a scale (v3.21: for centring).
static float text_width(const char *str, float scale) {
    C2D_Text t;
    C2D_TextFontParse(&t, g_font, g_textbuf, str);
    float w = 0;
    C2D_TextGetDimensions(&t, scale, scale, &w, NULL);
    return w;
}

static void draw_nav(void) {
    C2D_DrawRectSolid(0, NAV_TOP, 0.3f, 320, 240 - NAV_TOP, COL_NAV);
    static const char *labels[4] = {"ホーム", "検索", "ライブラリ", "設定"};
    int active = g_playback_view ? g_playing_tab : g_tab;
    for (int t = 0; t < 4; t++) {
        float x = t * 80.0f;
        int is_active = (t == active);
        u32 col = is_active ? COL_WHITE : COL_DIM;
        // Like the app: the active tab's icon is the filled
        // variant (only the bookmark has a pair in our set).
        int icon = tab_icons[t];
        if (is_active && t == TAB_LIBRARY) icon = ICON_BOOKMARK;
        draw_icon(icon, x + 28, NAV_TOP + 5, 24, col);
        // Centre the label on the tab (v3.21: the old hand-set
        // offsets left the 2-char labels visibly off-centre).
        float lx = x + 40 - text_width(labels[t], 0.42f) / 2;
        txt(lx, NAV_TOP + 33, 0.42f, col, labels[t]);
    }
}

// The app's mini player: jacket, title, artist, play/pause button,
// sitting directly above the progress strip and the tab bar.
static void draw_mini_player(void) {
    if (!g_has_np) return;
    C2D_DrawRectSolid(0, MINI_TOP, 0.3f, 320, PROG_TOP - MINI_TOP,
                      COL_SURFACE);
    draw_thumb(THUMB_BANK_NP, 0, 8, MINI_TOP + 6, 44, 0);
    char t[96];
    utf8_truncate(g_np_song.title, t, sizeof(t), 44);
    txt(60, MINI_TOP + 8, 0.52f, COL_WHITE, t);
    if (g_np_song.sub[0]) {
        char s[128];
        utf8_truncate(g_np_song.sub, s, sizeof(s), 50);
        txt(60, MINI_TOP + 31, 0.42f, COL_DIM, s);
    }
    int playing_now = g_playback_view && g_ctl.phase == PHASE_PLAY &&
                      !g_ctl.paused;
    draw_icon(playing_now ? ICON_PAUSE : ICON_PLAY_ARROW, 274,
              MINI_TOP + 12, 32, COL_WHITE);
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

// Player-view background (v3.28, user request): like the
// real app, the background is tinted with the jacket's
// average colour, as a vertical gradient fading toward
// black. Drawn as horizontal bands right after the clear,
// behind everything else. No jacket yet = plain black.
static void draw_tint_bg(float w, float top_scale, float bot_scale) {
    int avg[3];
    if (!thumb_np_avg(avg)) return;
    const int bands = 20;
    for (int i = 0; i < bands; i++) {
        float f = (float)i / bands;
        float s = top_scale + (bot_scale - top_scale) * f;
        int r = (int)(avg[0] * s), g = (int)(avg[1] * s),
            b = (int)(avg[2] * s);
        if (r > 120) r = 120;
        if (g > 120) g = 120;
        if (b > 120) b = 120;
        C2D_DrawRectSolid(0, f * 240.0f, 0.05f, w, 240.0f / bands + 1,
                          C2D_Color32(r, g, b, 255));
    }
}

// ---------- frame ----------
void render(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(g_textbuf);
    C2D_TargetClear(g_top_tgt, COL_BG);
    C2D_SceneBegin(g_top_tgt);
    if (g_player_view) {
        draw_tint_bg(400, 0.55f, 0.20f);
        draw_top_player_scene();
    }
    else if (g_tab == TAB_SETTINGS && !g_playback_view)
        draw_settings_scene();
    else
        draw_list_scene();
    C2D_TargetClear(g_bot_tgt, COL_BG);
    C2D_SceneBegin(g_bot_tgt);
    if (g_player_view) {
        draw_tint_bg(320, 0.36f, 0.08f);
        draw_bottom_player();
    }
    else if (g_playback_view)
        draw_bottom_playback(&g_ctl);
    else
        draw_bottom_idle();
    C3D_FrameEnd(0);
}

void draw_all(void) { render(); }

void render_message(const char *l1, const char *l2) {
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



// ---------- gfx ----------
int gfx_setup(void) {
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

void gfx_teardown(void) {
    if (g_textbuf) C2D_TextBufDelete(g_textbuf);
    if (g_font) C2D_FontFree(g_font);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}
