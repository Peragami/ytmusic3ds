// thumb.c - see thumb.h.

#include "thumb.h"

#include "store.h" // atomic_open/atomic_commit (v3.33)

#include "app.h" // APP_DIR (disk cache location, v3.27)

#include <3ds.h>
#include <curl/curl.h>
#include <jpeglib.h>

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_JPEG_BYTES (512 * 1024)

// Per-bank texture edge: list art is 64px from mqdefault; the
// now-playing bank gets 256px from hqdefault for the big
// artwork (v3.41, user request: at 128px the 224px on-screen
// jacket was an upscale and looked soft).
static const int bank_dim[3] = {64, 64, 256};
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
// Average colour of the uploaded NP jacket (v3.28).
static int g_np_avg[3] = {0, 0, 0};
static int g_np_avg_ok = 0;

int thumb_np_avg(int out[3]) {
    if (!g_np_avg_ok) return 0;
    out[0] = g_np_avg[0];
    out[1] = g_np_avg[1];
    out[2] = g_np_avg[2];
    return 1;
}

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

// v3.45: one persistent handle for the worker thread. Every
// thumbnail lives on the same host (i.ytimg.com), so reusing
// the connection skips a DNS + TCP + TLS handshake per image -
// on the 3DS that handshake costs a sizeable fraction of a
// second, which is why lists used to fill in slowly. Only the
// worker thread ever touches this handle (the whole fetch path
// runs there since v3.18), so no locking is needed. A transport
// error drops the handle; the next fetch reconnects.
static CURL *g_thumb_curl = NULL;

static int download_jpeg(const char *video_id, int hq, membuf_t *out) {
    char url[96];
    snprintf(url, sizeof(url), "https://i.ytimg.com/vi/%s/%s.jpg",
             video_id, hq ? "hqdefault" : "mqdefault");
    if (!g_thumb_curl) g_thumb_curl = curl_easy_init();
    if (!g_thumb_curl) return -1;
    CURL *curl = g_thumb_curl;
    curl_easy_reset(curl); // options reset; connection cache kept
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mem_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 4L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        g_thumb_curl = NULL;
    }
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
// Dimensions are validated before any row is read. List art is
// decoded at half scale to keep the working buffer small
// (mqdefault 320x180 -> 160x90); the now-playing artwork is
// decoded at full scale since v3.41 (hqdefault 480x360) so the
// 256px texture samples real detail instead of an upscale.
static int decode_jpeg_rgb(const uint8_t *data, size_t len, int full,
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
    cinfo.scale_denom = full ? 1 : 2;
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
// v3.20: corners outside a rounded-rect radius are made
// transparent, so artwork has the app's rounded corners.
static int upload_texture(int bank, int idx, const uint8_t *rgb, int w,
                          int h) {
    int dim = bank_dim[bank];
    C3D_Tex *tex = &g_tex[bank][idx];
    if (!C3D_TexInit(tex, dim, dim, GPU_RGBA8)) return -1;
    u32 *px = (u32 *)tex->data;
    int side, x0, y0;
    art_crop(rgb, w, h, &x0, &y0, &side);
    unsigned long acc_r = 0, acc_g = 0, acc_b = 0, acc_n = 0;
    // v3.26 (user report): the jacket's corners were too
    // round at 12% - the real app barely rounds them. The
    // now-playing bank uses 5%; list thumbnails keep 12%
    // (at 28px on screen that is only a few pixels anyway).
    float rr = dim * (bank == THUMB_BANK_NP ? 0.05f : 0.12f);
    float half = dim * 0.5f;
    for (int y = 0; y < dim; y++) {
        int sy = y0 + (y * side) / dim;
        float qy = fabsf(y + 0.5f - half) - (half - rr);
        for (int x = 0; x < dim; x++) {
            float qx = fabsf(x + 0.5f - half) - (half - rr);
            u32 alpha = 255u;
            if (qx > 0 && qy > 0 && qx * qx + qy * qy > rr * rr)
                alpha = 0;
            if (!alpha) {
                px[morton_offset(x, y, dim)] = 0;
                continue;
            }
            int sx = x0 + (x * side) / dim;
            const uint8_t *p = rgb + ((size_t)sy * w + sx) * 3;
            u32 r = p[0], g = p[1], b = p[2];
            if (bank == THUMB_BANK_NP) {
                acc_r += r;
                acc_g += g;
                acc_b += b;
                acc_n++;
            }
            // GPU_RGBA8 texels are stored A,B,G,R in memory
            // (v3.10 fix: the old packing put R first, which the
            // GPU read as alpha - red became transparency and the
            // channels rotated).
            px[morton_offset(x, y, dim)] =
                (r << 24) | (g << 16) | (b << 8) | alpha;
        }
    }
    if (bank == THUMB_BANK_NP && acc_n > 0) {
        g_np_avg[0] = (int)(acc_r / acc_n);
        g_np_avg[1] = (int)(acc_g / acc_n);
        g_np_avg[2] = (int)(acc_b / acc_n);
        g_np_avg_ok = 1;
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
    if (bank == THUMB_BANK_NP) {
        g_np_id[0] = '\0';
        g_np_avg_ok = 0;
    }
}

void thumbs_clear_all(void) {
    for (int b = 0; b < 3; b++) thumbs_clear_bank(b);
}

// ---------- disk cache (v3.27, user request) ----------
// Downloaded JPEGs are kept in APP_DIR/cache/<id>_<hq|mq>.jpg,
// next to favorites.txt, so list art and jackets reappear
// instantly on later launches without touching the network.
// The worker thread is the only reader/writer.
#define THUMB_CACHE_DIR APP_DIR "/cache"

static void cache_path(char *out, size_t cap, const char *id, int hq) {
    snprintf(out, cap, THUMB_CACHE_DIR "/%s_%s.jpg", id,
             hq ? "hq" : "mq");
}

static int cache_load(const char *id, int hq, membuf_t *out) {
    char path[160];
    cache_path(path, sizeof(path), id, hq);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 1024 || sz > MAX_JPEG_BYTES) {
        fclose(f);
        return -1;
    }
    out->data = (uint8_t *)malloc((size_t)sz);
    if (!out->data) {
        fclose(f);
        return -1;
    }
    if (fread(out->data, 1, (size_t)sz, f) != (size_t)sz) {
        free(out->data);
        out->data = NULL;
        fclose(f);
        return -1;
    }
    fclose(f);
    out->len = (size_t)sz;
    return 0;
}

static void cache_store(const char *id, int hq, const membuf_t *mb) {
    char path[160];
    cache_path(path, sizeof(path), id, hq);
    // v3.33: same crash-safe write as the data files - a torn
    // JPEG must never land under the final name.
    char tmp[200];
    FILE *f = atomic_open(path, tmp, sizeof(tmp));
    if (!f) return;
    fwrite(mb->data, 1, mb->len, f);
    atomic_commit(f, tmp, path);
}

static void cache_remove(const char *id, int hq) {
    char path[160];
    cache_path(path, sizeof(path), id, hq);
    remove(path);
}

static int fetch_jpeg(const char *id, int hq, membuf_t *out,
                      int *from_cache) {
    *from_cache = 0;
    if (cache_load(id, hq, out) == 0) {
        *from_cache = 1;
        return 0;
    }
    int rc = download_jpeg(id, hq, out);
    if (rc == 0) cache_store(id, hq, out);
    return rc;
}

// One fetch+decode attempt at one quality. A cached file
// that fails to decode (torn by a sudden power loss in an
// older version, say) is deleted and downloaded fresh once.
static int fetch_decode_try(const char *video_id, int hq, int full,
                            uint8_t **rgb_out, int *w_out,
                            int *h_out) {
    membuf_t mb;
    memset(&mb, 0, sizeof(mb));
    int from_cache = 0;
    int rc = -1;
    if (fetch_jpeg(video_id, hq, &mb, &from_cache) == 0) {
        if (decode_jpeg_rgb(mb.data, mb.len, full, rgb_out, w_out,
                            h_out) == 0) {
            rc = 0;
        } else if (from_cache) {
            cache_remove(video_id, hq);
            free(mb.data);
            memset(&mb, 0, sizeof(mb));
            if (fetch_jpeg(video_id, hq, &mb, &from_cache) == 0 &&
                decode_jpeg_rgb(mb.data, mb.len, full, rgb_out, w_out,
                                h_out) == 0)
                rc = 0;
        }
    }
    free(mb.data);
    return rc;
}

// Download + decode only (no GPU work). Runs on the worker
// thread; the main thread uploads via thumb_job_commit.
static int fetch_decode(int bank, const char *video_id,
                        uint8_t **rgb_out, int *w_out, int *h_out) {
    // The now-playing bank wants the larger hqdefault image; fall
    // back to mqdefault when a video has no usable hq image.
    // It also decodes at full scale (v3.41); list banks keep the
    // half-scale decode.
    int hq = bank == THUMB_BANK_NP;
    int rc = fetch_decode_try(video_id, hq, hq, rgb_out, w_out, h_out);
    if (rc != 0 && hq)
        rc = fetch_decode_try(video_id, 0, 1, rgb_out, w_out, h_out);
    return rc;
}

// ---------- background worker (v3.18) ----------
// One job at a time. The main thread fills the job and publishes
// state 1; the worker downloads + decodes, then publishes state
// 3 with the RGB buffer. Only the main thread ever touches GPU
// textures (commit) or frees the buffer.

typedef struct {
    volatile int state; // 0 empty / 1 queued / 2 working / 3 done
    int bank, idx;
    char id[16];
    uint8_t *rgb;
    int w, h, rc;
} thumb_job_t;

static thumb_job_t g_job;
static Thread g_worker = NULL;
static u64 g_worker_last_try = 0;

int thumb_worker_ok(void) { return g_worker != NULL; }

static void thumb_worker_main(void *arg) {
    (void)arg;
    for (;;) {
        if (g_job.state != 1) {
            svcSleepThread(8000000LL); // 8 ms
            continue;
        }
        g_job.state = 2;
        uint8_t *rgb = NULL;
        int w = 0, h = 0;
        int rc = fetch_decode(g_job.bank, g_job.id, &rgb, &w, &h);
        g_job.rgb = rgb;
        g_job.w = w;
        g_job.h = h;
        g_job.rc = rc;
        __sync_synchronize();
        g_job.state = 3;
    }
}

// Returns 1 when the worker thread exists. Creation is retried
// (throttled) instead of giving up forever. NOTE: 3DS thread
// priorities run 0x00 (highest) to 0x3F (lowest) - v3.18 passed
// 0x40, outside the valid range, so threadCreate always failed
// and its silent synchronous fallback froze the UI exactly like
// the old code. That fallback is gone in v3.19: no worker means
// no fetch (placeholder art), never a blocked main thread.
static int thumb_worker_ensure(void) {
    if (g_worker) return 1;
    u64 now = osGetTime();
    if (now - g_worker_last_try < 500) return 0;
    g_worker_last_try = now;
    g_worker = threadCreate(thumb_worker_main, NULL, 128 * 1024,
                            0x3F, 0, true);
    return g_worker != NULL;
}

int thumb_job_start(int bank, int idx, const char *video_id) {
    if (bank < 0 || bank > 2 || idx < 0 || idx >= bank_cap[bank] ||
        !video_id || !video_id[0])
        return -1;
    if (bank == THUMB_BANK_NP && g_has[bank][0] &&
        strncmp(g_np_id, video_id, sizeof(g_np_id) - 1) == 0)
        return 1; // already showing this song's artwork
    if (g_job.state != 0) return -1; // busy
    if (!thumb_worker_ensure()) return -2; // no worker (yet)
    g_job.bank = bank;
    g_job.idx = idx;
    strncpy(g_job.id, video_id, sizeof(g_job.id) - 1);
    g_job.id[sizeof(g_job.id) - 1] = '\0';
    g_job.rgb = NULL;
    g_job.w = g_job.h = 0;
    g_job.rc = -1;
    __sync_synchronize();
    g_job.state = 1;
    return 0;
}

int thumb_job_state(void) {
    if (g_job.state == 3) return 2;
    if (g_job.state == 0) return 0;
    return 1;
}

void thumb_job_peek(int *bank, int *idx, char *id_out, size_t cap) {
    if (bank) *bank = g_job.bank;
    if (idx) *idx = g_job.idx;
    if (id_out && cap) {
        strncpy(id_out, g_job.id, cap - 1);
        id_out[cap - 1] = '\0';
    }
}

int thumb_job_commit(void) {
    if (g_job.state != 3) return -1;
    int rc = g_job.rc;
    if (rc == 0) {
        clear_slot(g_job.bank, g_job.idx);
        if (upload_texture(g_job.bank, g_job.idx, g_job.rgb, g_job.w,
                           g_job.h) == 0) {
            if (g_job.bank == THUMB_BANK_NP) {
                strncpy(g_np_id, g_job.id, sizeof(g_np_id) - 1);
                g_np_id[sizeof(g_np_id) - 1] = '\0';
            }
        } else {
            rc = -1;
        }
    }
    free(g_job.rgb);
    g_job.rgb = NULL;
    g_job.state = 0;
    return rc;
}

void thumb_job_discard(void) {
    if (g_job.state != 3) return;
    free(g_job.rgb);
    g_job.rgb = NULL;
    g_job.state = 0;
}

C2D_Image *thumb_get(int bank, int idx) {
    if (bank < 0 || bank > 2 || idx < 0 || idx >= bank_cap[bank])
        return NULL;
    if (!g_has[bank][idx]) return NULL;
    return &g_img[bank][idx];
}
