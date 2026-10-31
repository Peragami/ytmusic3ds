// ui.h - rendering: citro2d targets, text and every scene.
#ifndef UI_H
#define UI_H

#include "app.h"

extern C3D_RenderTarget *g_top_tgt;
extern C3D_RenderTarget *g_bot_tgt;
extern C2D_Font g_font;
extern C2D_TextBuf g_textbuf;

// Bottom-screen layout bands (v3.7): mini player, its
// progress strip, and the tab bar.
#define NAV_TOP 188.0f
#define MINI_TOP 128.0f
#define PROG_TOP 185.0f

void txt(float x, float y, float scale, u32 color, const char *str);
void utf8_truncate(const char *in, char *out, size_t out_size,
                   size_t max_bytes);
void render(void);
void draw_all(void);
void render_message(const char *l1, const char *l2);
int player_zone(int px, int py);
int gfx_setup(void);
void gfx_teardown(void);

#endif // UI_H
