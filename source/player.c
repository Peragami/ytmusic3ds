// player.c - see player.h.
//
// Pipeline: curl (whole stream into RAM, capped) -> webm.c demux ->
// libopus decode -> ndsp wave-buffer queue (channel 0, 48 kHz stereo
// PCM16). Sized for the Old 3DS: the preferred stream is ~80 kbps Opus
// (about 0.6 MB per minute), downloads are capped at 32 MB, and the ndsp
// queue holds 4 x 8192-frame buffers (~0.7 s of audio, 128 KB of linear
// memory). Decoding is single-threaded; the UI hook runs between buffer
// fills, which is also where stop input is polled.

#include "player.h"

#include <3ds.h>
#include <curl/curl.h>
#include <opus/opus.h>

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "net.h"
#include "webm.h"

#define DL_LIMIT (32u * 1024 * 1024)
#define NBUF 4
#define BUF_FRAMES 8192 // per channel, at 48 kHz (~170 ms)

// ---------- download ----------

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    int overflow;
    play_ctl_t *ctl;
    play_hook_t hook;
    void *user;
    size_t last_hook_at;
    // Ranged-download state (all routes since v3.3):
    int ranged;      // use Range chunks + header tracking
    long cur_code;   // last HTTP status line seen
    int accept_body; // current response is 200/206 -> keep its bytes
    long range_total; // total size from Content-Range, -1 = unknown
} dl_ctx_t;

static size_t dl_write(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    dl_ctx_t *ctx = (dl_ctx_t *)userdata;
    if (ctx->ctl->stop_requested) return 0;
    // In ranged mode, error responses (403 pages etc.) must not pollute
    // the resume buffer: silently swallow their bodies.
    if (ctx->ranged && !ctx->accept_body) return total;
    if (ctx->len + total > DL_LIMIT) {
        ctx->overflow = 1;
        return 0;
    }
    if (ctx->len + total > ctx->cap) {
        size_t new_cap = (ctx->len + total) * 2;
        if (new_cap < 1u << 20) new_cap = 1u << 20;
        uint8_t *nb = realloc(ctx->data, new_cap);
        if (!nb) {
            ctx->overflow = 1;
            return 0;
        }
        ctx->data = nb;
        ctx->cap = new_cap;
    }
    memcpy(ctx->data + ctx->len, ptr, total);
    ctx->len += total;
    ctx->ctl->dl_bytes = ctx->len;
    // The ranged path has no xferinfo callback; drive the UI hook here.
    // v3.38: every 16 KB (was 64 KB) - on slow links the hook's
    // input scans were sparse enough that short Y/B taps fell
    // entirely between scans and looked dead during downloads.
    if (ctx->ranged && ctx->hook &&
        ctx->len - ctx->last_hook_at >= 16 * 1024) {
        ctx->last_hook_at = ctx->len;
        ctx->hook(ctx->user);
    }
    return total;
}

// Header callback for the ranged path: tracks the running HTTP status
// (redirects included) and the total size from Content-Range.
static size_t dl_header(void *ptr, size_t size, size_t nmemb,
                        void *userdata) {
    size_t total = size * nmemb;
    dl_ctx_t *ctx = (dl_ctx_t *)userdata;
    char line[256];
    size_t n = total < sizeof(line) - 1 ? total : sizeof(line) - 1;
    memcpy(line, ptr, n);
    line[n] = '\0';
    if (strncmp(line, "HTTP/", 5) == 0) {
        const char *sp = strchr(line, ' ');
        long code = sp ? atol(sp + 1) : 0;
        ctx->cur_code = code;
        ctx->accept_body = (code == 200 || code == 206);
    } else if (strncasecmp(line, "Content-Range:", 14) == 0) {
        const char *slash = strrchr(line, '/');
        if (slash) {
            long t = atol(slash + 1);
            if (t > 0) ctx->range_total = t;
        }
    }
    return total;
}

// (v3.3 removed the old plain-GET download(): it had no Range, no
// resume and no retry, and its failure message carried no HTTP code -
// it was the Invidious fallback's weak point in the v3.2 hardware
// test. All routes now use download_media() below.)

// ---------- ranged download over ONE persistent connection ----------
//
// v3.3 downloader, used by every route. Background: googlevideo URLs
// refuse a plain GET (403), and some issued tokens only authorize a
// prefix window of the file (see net.c: the old ANDROID+iPhone recipe
// tokens cut at exactly 512 KiB no matter what - persistent
// connection, fresh connections, any chunk size; the VISIONOS tokens
// used since v3.3 download completely). Chunking is still required:
// with itag 249 a single 512 KiB range request is refused with 403
// even at offset 0, while 256 KiB works (measured on the build VM,
// 2026-10-31; see report.md). So: fetch in 256 KiB Range chunks.
//
// All chunks of one download go through ONE curl easy handle created
// once per download (HTTP/1.1 pinned, no fresh-connect), so libcurl
// reuses the underlying connection across chunks - the same shape as
// FourthTube's persistent session downloader, and the direct answer
// to the v3.2 failure mode (a new connection per chunk). After each
// successful chunk the effective (post-redirect) URL is adopted back
// as the working URL, mirroring FourthTube storing redirected_url.
// Whenever a chunk fails, the stream URL is re-issued (from the same
// route/instance that produced it) and the download resumes from the
// byte offset already in the buffer; partial bytes of a 206 chunk
// stay valid. At most DL_MAX_RERESOLVE re-issues per download. Every
// failure records ctl->fail_* diagnostics for the UI error log.
// Returns 0 = complete, 1 = stopped, -1 = failed, -2 = overflow.
#define DL_CHUNK (256u * 1024)
#define DL_MAX_RERESOLVE 5

// Where a cut download re-issues its URL from.
enum {
    RR_NONE = 0,     // no re-issue source
    RR_DIRECT = 1,   // resolve_stream_direct()
    RR_INVIDIOUS = 2 // resolve_stream_invidious_at(instance_idx)
};

static int download_media(const char *url0, int rr_kind,
                          const char *video_id, int instance_idx,
                          dl_ctx_t *ctx) {
    static char url[2048];
    static char fresh[2048];
    strncpy(url, url0, sizeof(url) - 1);
    url[sizeof(url) - 1] = '\0';
    long total = -1;
    int reresolves = 0;
    int ret = -1;
    ctx->ctl->dl_bytes = 0;
    ctx->ctl->dl_total = 0;

    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    // One handle for the whole download: set everything once. Per
    // chunk only the URL (after a re-issue/redirect) and Range change.
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, dl_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, ctx);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (Linux; Android 11; Pixel 3a) "
                     "AppleWebKit/537.36 (KHTML, like Gecko) "
                     "Chrome/83.0.4103.101 Mobile Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION,
                     (long)CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    while (1) {
        if (ctx->ctl->stop_requested) {
            ret = 1;
            break;
        }
        if (ctx->overflow) {
            ctx->ctl->fail_stage = FAILSTAGE_DOWNLOAD;
            ctx->ctl->fail_offset = ctx->len;
            ret = -2;
            break;
        }
        if (total >= 0 && (long)ctx->len >= total) {
            ret = 0;
            break;
        }
        size_t from = ctx->len;
        size_t to = from + DL_CHUNK - 1;
        if (total >= 0 && (long)to >= total) to = (size_t)total - 1;

        char range[64];
        snprintf(range, sizeof(range), "%zu-%zu", from, to);
        ctx->cur_code = 0;
        ctx->accept_body = 0;
        ctx->range_total = -1;
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_RANGE, range);
        size_t before = ctx->len;
        CURLcode res = curl_easy_perform(curl);
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        if (code == 0) code = ctx->cur_code;

        if (ctx->ctl->stop_requested) {
            ret = 1;
            break;
        }
        if (ctx->overflow) {
            ctx->ctl->fail_stage = FAILSTAGE_DOWNLOAD;
            ctx->ctl->fail_http = code;
            ctx->ctl->fail_curl = (int)res;
            ctx->ctl->fail_offset = ctx->len;
            ret = -2;
            break;
        }
        if (ctx->range_total > 0) {
            total = ctx->range_total;
            ctx->ctl->dl_total = (size_t)total;
        }
        size_t got = ctx->len - before;
        if (res == CURLE_OK && code == 206 && got > 0) {
            // Adopt the post-redirect URL (FourthTube does the same);
            // later chunks then skip the redirect hop entirely.
            char *eff = NULL;
            if (curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL,
                                  &eff) == CURLE_OK &&
                eff && strcmp(eff, url) != 0) {
                strncpy(url, eff, sizeof(url) - 1);
                url[sizeof(url) - 1] = '\0';
            }
            // Short final chunk with no Content-Range total: that was EOF.
            if (total < 0 && got < (to - from + 1)) {
                ret = 0;
                break;
            }
            continue;
        }
        if (res == CURLE_OK && code == 200 && from == 0 && got > 0) {
            ret = 0; // server ignored Range and sent the whole body
            break;
        }
        if (code == 416 && total >= 0 && (long)ctx->len >= total) {
            ret = 0;
            break;
        }

        // Chunk failed (403 mid-download is the expected case, but any
        // cut lands here). Record diagnostics, then re-issue the URL
        // from the same route/instance and resume from ctx->len.
        ctx->ctl->fail_stage = FAILSTAGE_DOWNLOAD;
        ctx->ctl->fail_http = code;
        ctx->ctl->fail_curl = (int)res;
        ctx->ctl->fail_offset = ctx->len;
        if (rr_kind == RR_NONE || reresolves >= DL_MAX_RERESOLVE) {
            ret = -1;
            break;
        }
        reresolves++;
        snprintf(ctx->ctl->message, sizeof(ctx->ctl->message),
                 "Stream cut (HTTP %ld) - re-resolving %d/%d...", code,
                 reresolves, DL_MAX_RERESOLVE);
        if (ctx->hook) ctx->hook(ctx->user);
        int itag = 0;
        long ls = 0;
        int rrc;
        if (rr_kind == RR_DIRECT)
            rrc = resolve_stream_direct(video_id, fresh, sizeof(fresh),
                                        &itag, &ls);
        else
            rrc = resolve_stream_invidious_at(instance_idx, video_id,
                                              fresh, sizeof(fresh),
                                              &itag, &ls, NULL);
        if (rrc != 0) {
            ret = -1;
            break;
        }
        strncpy(url, fresh, sizeof(url) - 1);
        url[sizeof(url) - 1] = '\0';
    }
    curl_easy_cleanup(curl);
    return ret;
}

// ---------- decode + ndsp ----------

typedef struct {
    webm_t webm;
    OpusDecoder *dec;
    int src_channels;
    int eos;              // no more packets
    uint32_t skip;        // pre-skip samples still to discard
    uint64_t frames_out;  // frames handed to ndsp (for elapsed time)
    // Carry-over of decoded samples that did not fit the previous buffer.
    int16_t pending[5760 * 2];
    size_t pending_len; // frames decoded into pending
    size_t pending_off; // frames already consumed
} decode_ctx_t;

// NOTE (v3.2): decode_ctx_t is ~25 KB, almost all of it the `pending`
// scratch below. It used to be a stack local in play_stream_url_ex,
// giving that function a 27.6 KB stack frame - 84% of libctru's
// entire default 32 KB main stack, before curl is even called. It is
// heap-allocated at the play site now.

// Fill dst (interleaved stereo) with up to max_frames frames.
// Returns the number of frames written (0 only at end of stream).
static size_t decode_fill(decode_ctx_t *dc, int16_t *dst, size_t max_frames) {
    size_t written = 0;
    while (written < max_frames) {
        if (dc->pending_off >= dc->pending_len) {
            if (dc->eos) break;
            const uint8_t *pkt;
            size_t plen;
            int64_t ts;
            int r = webm_next_packet(&dc->webm, &pkt, &plen, &ts);
            if (r <= 0) { // 0 = EOF, <0 = corrupt region: stop cleanly
                dc->eos = 1;
                break;
            }
            int n = opus_decode(dc->dec, pkt, (opus_int32)plen, dc->pending,
                                5760, 0);
            if (n < 0) continue; // skip undecodable packet
            dc->pending_len = (size_t)n;
            dc->pending_off = 0;
            if (dc->skip > 0) {
                size_t s = dc->skip < dc->pending_len ? dc->skip
                                                      : dc->pending_len;
                dc->pending_off = s;
                dc->skip -= (uint32_t)s;
            }
            if (dc->pending_off >= dc->pending_len) continue;
        }
        size_t avail = dc->pending_len - dc->pending_off;
        size_t room = max_frames - written;
        size_t take = avail < room ? avail : room;
        for (size_t i = 0; i < take; i++) {
            size_t si = (dc->pending_off + i) * dc->src_channels;
            int16_t l = dc->pending[si];
            int16_t rr = dc->src_channels == 2 ? dc->pending[si + 1] : l;
            dst[(written + i) * 2] = l;
            dst[(written + i) * 2 + 1] = rr;
        }
        dc->pending_off += take;
        written += take;
        dc->frames_out += take;
    }
    return written;
}

static int play_common(const char *url, unsigned total_sec_hint,
                       play_ctl_t *ctl, play_hook_t hook, void *user,
                       int rr_kind, const char *video_id,
                       int instance_idx) {
    int ret = -1;
    ctl->phase = PHASE_DOWNLOAD;
    ctl->total_sec = total_sec_hint;
    ctl->elapsed_sec = 0;
    ctl->dl_bytes = ctl->dl_total = 0;
    ctl->fail_stage = FAILSTAGE_NONE;
    ctl->fail_http = 0;
    ctl->fail_curl = 0;
    ctl->fail_offset = 0;
    snprintf(ctl->message, sizeof(ctl->message), "Downloading stream...");

    dl_ctx_t dl;
    memset(&dl, 0, sizeof(dl));
    dl.ctl = ctl;
    dl.hook = hook;
    dl.user = user;
    dl.ranged = 1;
    int dr = download_media(url, rr_kind, video_id, instance_idx, &dl);
    if (dr == 1) {
        free(dl.data);
        return 1;
    }
    if (dr != 0) {
        if (dr == -2) {
            snprintf(ctl->message, sizeof(ctl->message),
                     "Stream too large (>32 MB).");
            free(dl.data);
            return -1;
        }
        snprintf(ctl->message, sizeof(ctl->message),
                 rr_kind == RR_DIRECT
                     ? "Direct download failed (stream kept cutting "
                       "out)."
                 : rr_kind == RR_INVIDIOUS
                     ? "Invidious download failed (stream kept cutting "
                       "out)."
                     : "Download failed (network/HTTP).");
        free(dl.data);
        // Let the caller fall back to another route/instance when the
        // download stage is what failed.
        return rr_kind != RR_NONE ? PLAY_DL_FAIL : -1;
    }

    webm_t webm;
    int wr = webm_open(&webm, dl.data, dl.len);
    if (wr != 0) {
        ctl->fail_stage = FAILSTAGE_MEDIA;
        snprintf(ctl->message, sizeof(ctl->message),
                 "Not a WebM/Opus stream (open rc=%d).", wr);
        free(dl.data);
        return -1;
    }

    int oerr = 0;
    OpusDecoder *dec = opus_decoder_create(48000, webm.channels, &oerr);
    if (!dec) {
        ctl->fail_stage = FAILSTAGE_MEDIA;
        snprintf(ctl->message, sizeof(ctl->message),
                 "opus_decoder_create failed (%d).", oerr);
        free(dl.data);
        return -1;
    }

    if (ndspInit() != 0) {
        ctl->fail_stage = FAILSTAGE_MEDIA;
        snprintf(ctl->message, sizeof(ctl->message),
                 "ndspInit failed - DSP firmware dump is required "
                 "(run DSP1 once from Homebrew Launcher).");
        opus_decoder_destroy(dec);
        free(dl.data);
        return -1;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_NONE);
    ndspChnSetRate(0, 48000.0f);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = {0};
    mix[0] = mix[1] = 1.0f;
    ndspChnSetMix(0, mix);

    ndspWaveBuf bufs[NBUF];
    memset(bufs, 0, sizeof(bufs));
    int alloc_ok = 1;
    for (int i = 0; i < NBUF; i++) {
        bufs[i].data_pcm16 =
            (int16_t *)linearAlloc(BUF_FRAMES * 2 * sizeof(int16_t));
        if (!bufs[i].data_pcm16) alloc_ok = 0;
    }

    decode_ctx_t *dc = NULL;
    if (!alloc_ok) {
        snprintf(ctl->message, sizeof(ctl->message),
                 "linearAlloc failed (out of memory).");
        ret = -1;
    } else if ((dc = (decode_ctx_t *)malloc(sizeof(decode_ctx_t))) ==
               NULL) {
        snprintf(ctl->message, sizeof(ctl->message),
                 "Out of memory (decode context).");
        ret = -1;
    } else {
        memset(dc, 0, sizeof(*dc));
        dc->webm = webm;
        dc->dec = dec;
        dc->src_channels = webm.channels;
        dc->skip = webm.pre_skip;

        ctl->phase = PHASE_PLAY;
        snprintf(ctl->message, sizeof(ctl->message), "Playing");
        ctl->paused = 0;
        ret = 0;
        int stream_done = 0;
        while (1) {
            if (ctl->stop_requested) {
                ret = 1;
                break;
            }
            // v3.6: pause/resume via ndsp channel pause. While
            // paused we stop feeding buffers; frames_out (and thus
            // the elapsed clock) freezes on its own.
            if (ctl->pause_flip) {
                ctl->pause_flip = 0;
                ctl->paused = !ctl->paused;
                ndspChnSetPaused(0, ctl->paused != 0);
                snprintf(ctl->message, sizeof(ctl->message),
                         ctl->paused ? "Paused" : "Playing");
            }
            if (ctl->paused) {
                if (hook) hook(user);
                if (ctl->stop_requested) {
                    ret = 1;
                    break;
                }
                svcSleepThread(10000000); // 10 ms
                continue;
            }
            int any_active = 0;
            int filled = 0;
            for (int i = 0; i < NBUF; i++) {
                u32 st = bufs[i].status;
                if (st == NDSP_WBUF_QUEUED || st == NDSP_WBUF_PLAYING) {
                    any_active = 1;
                    continue;
                }
                // FREE or DONE: (re)fill
                if (!stream_done) {
                    size_t got = decode_fill(dc, bufs[i].data_pcm16,
                                             BUF_FRAMES);
                    if (got > 0) {
                        bufs[i].nsamples = (u32)got;
                        DSP_FlushDataCache(bufs[i].data_pcm16,
                                           got * 2 * sizeof(int16_t));
                        ndspChnWaveBufAdd(0, &bufs[i]);
                        any_active = 1;
                        filled = 1;
                    } else {
                        stream_done = 1;
                    }
                }
            }
            ctl->elapsed_sec = (unsigned)(dc->frames_out / 48000);
            if (hook) hook(user);
            if (stream_done && !any_active) break;
            if (!filled) svcSleepThread(5000000); // 5 ms
        }
        ndspChnSetPaused(0, false);
        ctl->paused = 0;
        ndspChnWaveBufClear(0);
    }
    free(dc);

    for (int i = 0; i < NBUF; i++)
        if (bufs[i].data_pcm16) linearFree(bufs[i].data_pcm16);
    ndspExit();
    opus_decoder_destroy(dec);
    free(dl.data);
    ctl->phase = PHASE_DONE;
    return ret;
}

int play_stream_url(const char *url, unsigned total_sec_hint,
                    play_ctl_t *ctl, play_hook_t hook, void *user) {
    return play_common(url, total_sec_hint, ctl, hook, user, RR_NONE,
                       NULL, -1);
}

int play_stream_url_ex(const char *url, unsigned total_sec_hint,
                       play_ctl_t *ctl, play_hook_t hook, void *user,
                       const char *video_id) {
    if (!video_id || !video_id[0])
        return play_common(url, total_sec_hint, ctl, hook, user,
                           RR_NONE, NULL, -1);
    return play_common(url, total_sec_hint, ctl, hook, user, RR_DIRECT,
                       video_id, -1);
}

int play_stream_url_inv(const char *url, unsigned total_sec_hint,
                        play_ctl_t *ctl, play_hook_t hook, void *user,
                        const char *video_id, int instance_idx) {
    return play_common(url, total_sec_hint, ctl, hook, user,
                       RR_INVIDIOUS, video_id, instance_idx);
}
