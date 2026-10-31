// keyboard.c - the flick / toggle kana keyboard.
// Split out of main.c in v3.17; logic unchanged.

#include "keyboard.h"

#include "ui.h"

#include <stdlib.h>

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

int kana_prompt(void) {
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

