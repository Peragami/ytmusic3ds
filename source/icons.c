// icons.c - see icons.h.
//
// v3.8: icons are drawn WITHOUT image tinting. In v3.7 the tint
// path rendered every icon as a solid square of the tint colour
// (the glyph alpha was lost - seen in Azahar, where thumbnails
// drawn untinted were fine). So each icon now gets one texture
// per colour the UI actually uses, with the colour baked into
// the texels and the glyph shape kept purely in the alpha
// channel; drawing uses the plain untinted path like thumbnails.

#include "icons.h"

#include <string.h>

// Colour variants, matched to the palette in main.c.
#define ICON_VARIANTS 4
static const u8 variant_rgb[ICON_VARIANTS][3] = {
    {255, 255, 255}, // 0 white (COL_WHITE)
    {170, 170, 170}, // 1 dim   (COL_DIM)
    {255, 0, 0},     // 2 accent(COL_ACCENT)
    {0, 0, 0},       // 3 bg    (COL_BG, glyph on the white button)
};

static C3D_Tex g_tex[ICON_COUNT][ICON_VARIANTS];
static Tex3DS_SubTexture g_sub[ICON_COUNT];
static C2D_Image g_img[ICON_COUNT][ICON_VARIANTS];
static int g_ready = 0;

// PICA tiled texture order (same layout thumb.c uses).
static u32 morton_offset(u32 x, u32 y, u32 width) {
    u32 i = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) |
            ((x & 4) << 2) | ((y & 4) << 3);
    u32 coarse_x = x >> 3;
    u32 coarse_y = y >> 3;
    u32 tile = coarse_x + coarse_y * (width >> 3);
    return (tile << 6) + i;
}

void icons_init(void) {
    if (g_ready) return;
    for (int id = 0; id < ICON_COUNT; id++) {
        Tex3DS_SubTexture *sub = &g_sub[id];
        sub->width = ICON_W;
        sub->height = ICON_H;
        sub->left = 0.0f;
        sub->top = 1.0f;
        sub->right = 1.0f;
        sub->bottom = 0.0f;
        const unsigned char *mask = icon_masks[id];
        for (int v = 0; v < ICON_VARIANTS; v++) {
            if (!C3D_TexInit(&g_tex[id][v], ICON_W, ICON_H, GPU_RGBA8))
                continue;
            u32 *px = (u32 *)g_tex[id][v].data;
            u32 r = variant_rgb[v][0], g = variant_rgb[v][1],
                b = variant_rgb[v][2];
            for (int y = 0; y < ICON_H; y++) {
                for (int x = 0; x < ICON_W; x++) {
                    u32 a = mask[y * ICON_W + x];
                    // GPU_RGBA8 texels are stored A,B,G,R in
                    // memory (v3.10 fix - the old order made the
                    // GPU read the red channel as alpha and the
                    // mask as red, producing cyan squares).
                    px[morton_offset(x, y, ICON_W)] =
                        (r << 24) | (g << 16) | (b << 8) | a;
                }
            }
            C3D_TexSetFilter(&g_tex[id][v], GPU_LINEAR, GPU_LINEAR);
            C3D_TexFlush(&g_tex[id][v]);
            g_img[id][v].tex = &g_tex[id][v];
            g_img[id][v].subtex = sub;
        }
    }
    g_ready = 1;
}

static int variant_for(u32 color) {
    // C2D_Color32 packs r | g<<8 | b<<16 | a<<24.
    if (color == C2D_Color32(170, 170, 170, 255)) return 1;
    if (color == C2D_Color32(255, 0, 0, 255)) return 2;
    if (color == C2D_Color32(0, 0, 0, 255)) return 3;
    return 0; // white and anything else
}

void draw_icon(int id, float x, float y, float size, u32 color) {
    if (!g_ready || id < 0 || id >= ICON_COUNT) return;
    C2D_DrawImageAt(g_img[id][variant_for(color)], x, y, 0.5f, NULL,
                    size / ICON_W, size / ICON_H);
}
