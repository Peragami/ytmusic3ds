// thumb.c - see thumb.h.

#include "thumb.h"

#include <3ds.h>
#include <curl/curl.h>
#include <jpeglib.h>

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_JPEG_BYTES (512 * 1024)

// Per-bank texture edge: list art is 64px from mqdefault; the
// now-playing bank gets 128px from hqdefault for the big artwork.
static const int bank_dim[3] = {64, 64, 128};
static const int bank_cap[3] = {THUMB_MAX_RESULTS, THUMB_MAX_FAVS, 1};

int thumb_dim(int bank) {
    if (bank < 0 || bank > 2) return 64;
    return bank_dim[bank];
}
static C3D_Tex g_tex[3][THUMB_MAX_FAVS];
static Tex3DS_SubTexture g_sub[3][THUMB_MAX_FAVS];
static C2D_Image g_img[3][THUMB_MAX_FAVS];
static char g_has[3][THUMB_MAX_FAVS];
static char g_np_id[16] = "";

// ---------- download ----------

typedef struct {
    uint8_t *data;
    size_t len;
    int overflow;
} membuf_t;

static size_t mem_write(void *ptr, size_t size, size_t nmemb, void *ud) {
    size_t total = size * nmemb;
    membuf_t *m = (membuf_t *)ud;
    if (m->len + total > MAX_JPEG_BYTES) {
        m->overflow = 1;
        return 0;
    }
    uint8_t *nb = realloc(m->data, m->len + total);
    if (!nb) {
        m->overflow = 1;
        return 0;
    }
    m->data = nb;
    memcpy(m->data + m->len, ptr, total);
    m->len += total;
    return total;
}

static int download_jpeg(const char *video_id, int hq, membuf_t *out) {
    char url[96];
    snprintf(url, sizeof(url), "https://i.ytimg.com/vi/%s/%s.jpg",
             video_id, hq ? "hqdefault" : "mqdefault");
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mem_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || code != 200 || out->overflow || !out->data ||
        out->len < 1024)
        return -1;
    return 0;
}

// ---------- JPEG decode (libjpeg, longjmp-safe) ----------

struct jpg_err_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf jb;
};

static void jpg_error_exit(j_common_ptr cinfo) {
    struct jpg_err_mgr *e = (struct jpg_err_mgr *)cinfo->err;
    longjmp(e->jb, 1);
}

// Decode a JPEG in memory to an RGB buffer (malloc'd; caller frees).
// Dimensions are validated before any row is read. Half-scale decode
// keeps the working buffer small (mqdefault 320x180 -> 160x90).
static int decode_jpeg_rgb(const uint8_t *data, size_t len,
                           uint8_t **rgb_out, int *w_out, int *h_out) {
    struct jpeg_decompress_struct cinfo;
    struct jpg_err_mgr err;
    cinfo.err = jpeg_std_error(&err.pub);
    err.pub.error_exit = jpg_error_exit;
    if (setjmp(err.jb)) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data, len);
    jpeg_read_header(&cinfo, TRUE);
    if (cinfo.image_width < 16 || cinfo.image_height < 16 ||
        cinfo.image_width > 2048 || cinfo.image_height > 2048) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }
    cinfo.scale_num = 1;
    cinfo.scale_denom = 2;
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);
    int w = (int)cinfo.output_width;
    int h = (int)cinfo.output_height;
    if (cinfo.output_components != 3 || w < 8 || h < 8) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }
    uint8_t *rgb = (uint8_t *)malloc((size_t)w * h * 3);
    if (!rgb) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = rgb + (size_t)cinfo.output_scanline * w * 3;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    *rgb_out = rgb;
    *w_out = w;
    *h_out = h;
    return 0;
}

// ---------- texture upload ----------

// PICA textures are stored as 8x8 tiles in Morton (Z) order.
static u32 morton_offset(u32 x, u32 y, u32 width) {
    u32 i = 0;
    u32 mx = x & 7, my = y & 7;
    for (int b = 0; b < 3; b++) {
        i |= ((mx >> b) & 1) << (2 * b);
        i |= ((my >> b) & 1) << (2 * b + 1);
    }
    u32 tile = (x >> 3) + (y >> 3) * (width >> 3);
    return (tile << 6) + i;
}

// v3.7 letterbox trim: YouTube thumbnails often carry solid or
// heavily blurred bars around the actual artwork, which showed up
// as margins versus the app. Detect bar rows/columns (nearly
// uniform colour close to the corner colour, with almost no
// texture) from each edge - at most 35% per side - then take the
// centre square of what remains.
static void corner_color(const uint8_t *rgb, int w, int h, int out[3]) {
    long sum[3] = {0, 0, 0};
    int n = 0;
    const int pts[4][2] = {{0, 0}, {w - 4, 0}, {0, h - 4}, {w - 4, h - 4}};
    for (int c = 0; c < 4; c++) {
        for (int dy = 0; dy < 4; dy++) {
            for (int dx = 0; dx < 4; dx++) {
                const uint8_t *p =
                    rgb + ((size_t)(pts[c][1] + dy) * w + pts[c][0] + dx) * 3;
                sum[0] += p[0];
                sum[1] += p[1];
                sum[2] += p[2];
                n++;
            }
        }
    }
    out[0] = (int)(sum[0] / n);
    out[1] = (int)(sum[1] / n);
    out[2] = (int)(sum[2] / n);
}

static int row_is_bar(const uint8_t *rgb, int w, int y,
                      const int cc[3]) {
    int close = 0, total = 0;
    long energy = 0;
    int prev[3] = {-1, -1, -1};
    for (int x = 0; x < w; x += 4) {
        const uint8_t *p = rgb + ((size_t)y * w + x) * 3;
        int d = abs(p[0] - cc[0]) + abs(p[1] - cc[1]) + abs(p[2] - cc[2]);
        if (d <= 60) close++;
        if (prev[0] >= 0)
            energy += abs(p[0] - prev[0]) + abs(p[1] - prev[1]) +
                      abs(p[2] - prev[2]);
        prev[0] = p[0];
        prev[1] = p[1];
        prev[2] = p[2];
        total++;
    }
    if (total == 0) return 0;
    // Bar = almost everything near the corner colour AND flat.
    return close * 100 >= total * 92 && energy / total <= 18;
}

static int col_is_bar(const uint8_t *rgb, int w, int h, int x,
                      const int cc[3]) {
    int close = 0, total = 0;
    long energy = 0;
    int prev[3] = {-1, -1, -1};
    for (int y = 0; y < h; y += 4) {
        const uint8_t *p = rgb + ((size_t)y * w + x) * 3;
        int d = abs(p[0] - cc[0]) + abs(p[1] - cc[1]) + abs(p[2] - cc[2]);
        if (d <= 60) close++;
        if (prev[0] >= 0)
            energy += abs(p[0] - prev[0]) + abs(p[1] - prev[1]) +
                      abs(p[2] - prev[2]);
        prev[0] = p[0];
        prev[1] = p[1];
        prev[2] = p[2];
        total++;
    }
    if (total == 0) return 0;
    return close * 100 >= total * 92 && energy / total <= 18;
}

static void art_crop(const uint8_t *rgb, int w, int h, int *ox, int *oy,
                     int *side) {
    int cc[3];
    corner_color(rgb, w, h, cc);
    int top = 0, bottom = h - 1, left = 0, right = w - 1;
    int max_ty = h * 35 / 100, max_tx = w * 35 / 100;
    while (top < max_ty && row_is_bar(rgb, w, top, cc)) top++;
    while (bottom > h - 1 - max_ty && bottom > top &&
           row_is_bar(rgb, w, bottom, cc))
        bottom--;
    while (left < max_tx && col_is_bar(rgb, w, h, left, cc)) left++;
    while (right > w - 1 - max_tx && right > left &&
           col_is_bar(rgb, w, h, right, cc))
        right--;
    int cw = right - left + 1;
    int ch = bottom - top + 1;
    if (cw < 24 || ch < 24) { // implausible trim; keep the plain crop
        top = 0;
        left = 0;
        cw = w;
        ch = h;
    }
    int s = cw < ch ? cw : ch;
    *ox = left + (cw - s) / 2;
    *oy = top + (ch - s) / 2;
    *side = s;
}

// Crop the decoded RGB to the artwork square (letterbox-trimmed)
// and sample it into a dim x dim RGBA8 texture in tiled order.
static int upload_texture(int bank, int idx, const uint8_t *rgb, int w,
                          int h) {
    int dim = bank_dim[bank];
    C3D_Tex *tex = &g_tex[bank][idx];
    if (!C3D_TexInit(tex, dim, dim, GPU_RGBA8)) return -1;
    u32 *px = (u32 *)tex->data;
    int side, x0, y0;
    art_crop(rgb, w, h, &x0, &y0, &side);
    for (int y = 0; y < dim; y++) {
        int sy = y0 + (y * side) / dim;
        for (int x = 0; x < dim; x++) {
            int sx = x0 + (x * side) / dim;
            const uint8_t *p = rgb + ((size_t)sy * w + sx) * 3;
            u32 r = p[0], g = p[1], b = p[2];
            // GPU_RGBA8 texels are stored A,B,G,R in memory
            // (v3.10 fix: the old packing put R first, which the
            // GPU read as alpha - red became transparency and the
            // channels rotated).
            px[morton_offset(x, y, dim)] =
                (r << 24) | (g << 16) | (b << 8) | 255u;
        }
    }
    C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexFlush(tex);
    Tex3DS_SubTexture *sub = &g_sub[bank][idx];
    sub->width = dim;
    sub->height = dim;
    sub->left = 0.0f;
    sub->top = 1.0f;
    sub->right = 1.0f;
    sub->bottom = 0.0f;
    g_img[bank][idx].tex = tex;
    g_img[bank][idx].subtex = sub;
    g_has[bank][idx] = 1;
    return 0;
}

// ---------- public API ----------

static void clear_slot(int bank, int idx) {
    if (bank < 0 || bank > 2 || idx < 0 || idx >= bank_cap[bank]) return;
    if (g_has[bank][idx]) {
        C3D_TexDelete(&g_tex[bank][idx]);
        g_has[bank][idx] = 0;
    }
}

void thumbs_clear_bank(int bank) {
    if (bank < 0 || bank > 2) return;
    for (int i = 0; i < bank_cap[bank]; i++) clear_slot(bank, i);
    if (bank == THUMB_BANK_NP) g_np_id[0] = '\0';
}

void thumbs_clear_all(void) {
    for (int b = 0; b < 3; b++) thumbs_clear_bank(b);
}

int thumb_fetch(int bank, int idx, const char *video_id) {
    if (bank < 0 || bank > 2 || idx < 0 || idx >= bank_cap[bank] ||
        !video_id || !video_id[0])
        return -1;
    if (bank == THUMB_BANK_NP && g_has[bank][0] &&
        strncmp(g_np_id, video_id, sizeof(g_np_id) - 1) == 0)
        return 0; // already showing this song's thumbnail
    clear_slot(bank, idx);
    membuf_t mb;
    memset(&mb, 0, sizeof(mb));
    int rc = -1;
    uint8_t *rgb = NULL;
    int w = 0, h = 0;
    // The now-playing bank wants the larger hqdefault image; fall
    // back to mqdefault when a video has no usable hq image.
    int dl = download_jpeg(video_id, bank == THUMB_BANK_NP, &mb);
    if (dl != 0 && bank == THUMB_BANK_NP) {
        free(mb.data);
        memset(&mb, 0, sizeof(mb));
        dl = download_jpeg(video_id, 0, &mb);
    }
    if (dl == 0 &&
        decode_jpeg_rgb(mb.data, mb.len, &rgb, &w, &h) == 0 &&
        upload_texture(bank, idx, rgb, w, h) == 0) {
        rc = 0;
        if (bank == THUMB_BANK_NP) {
            strncpy(g_np_id, video_id, sizeof(g_np_id) - 1);
            g_np_id[sizeof(g_np_id) - 1] = '\0';
        }
    }
    free(rgb);
    free(mb.data);
    return rc;
}

C2D_Image *thumb_get(int bank, int idx) {
    if (bank < 0 || bank > 2 || idx < 0 || idx >= bank_cap[bank])
        return NULL;
    if (!g_has[bank][idx]) return NULL;
    return &g_img[bank][idx];
}
