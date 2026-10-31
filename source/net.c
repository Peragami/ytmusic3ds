// net.c - see net.h. Uses portlibs curl (+mbedtls) and jansson.

#include "net.h"

#include <3ds.h> // osGetTime (visitor cache, v3.29)
#include <curl/curl.h>
#include <jansson.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Standard public client key for the YouTube Music web client.
// (Deliberately not repeated in report.md.)
static const char API_KEY[] = "AIzaSyC9XL3ZjWddXya6X74dJoCTL-WEYFDNX30";

// Standard public client key for the regular YouTube web client,
// used with the m.youtube.com InnerTube endpoint (FourthTube recipe).
// (Deliberately not repeated in ANALYSIS.md.)
static const char WEB_API_KEY[] = "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8";

// Invidious instances tried in order for stream resolution.
// Probe from the build VM on 2026-10-31 (v3.3):
//   - yt.chocolatemoo53.com: fully working (HTTP 200, Opus formats) -
//     and the official api.invidious.io registry currently lists it as
//     the ONLY api-enabled public instance. Its stream URLs are
//     VISIONOS-client URLs and download completely.
//   - inv.nadeko.net (403) and invidious.nerdvpn.de (401): the hosts
//     answer but refuse the VM's datacenter egress IP. A home
//     connection may be treated differently, and the v3.3 error
//     surface reports each instance's HTTP code, so they stay listed;
//     dead hosts (404/502/unreachable) were dropped.
static const char *INVIDIOUS_INSTANCES[] = {
    "yt.chocolatemoo53.com",
    "inv.nadeko.net",
    "invidious.nerdvpn.de",
    NULL,
};

int invidious_instance_count(void) {
    int n = 0;
    while (INVIDIOUS_INSTANCES[n]) n++;
    return n;
}

const char *invidious_instance_host(int idx) {
    if (idx < 0 || idx >= invidious_instance_count()) return "";
    return INVIDIOUS_INSTANCES[idx];
}

// ---------- growable buffer ----------
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} buf_t;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    buf_t *buf = (buf_t *)userdata;
    if (buf->len + total + 1 > buf->cap) {
        size_t new_cap = (buf->len + total + 1) * 2;
        char *nb = realloc(buf->data, new_cap);
        if (!nb) return 0;
        buf->data = nb;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

// Perform an HTTP(S) request; body != NULL -> POST with that body.
// On success (any HTTP code) returns 0 and sets *http_out; response in buf.
// UI pump hook (v3.29, user report): a plain curl_easy_perform
// froze the whole app for each request's round trip - about a
// second at song select, where resolve needs two of them.
// Requests now run on a multi handle and the pump (registered
// from main.c) keeps input + redraw alive while waiting; it
// can also abort the request (B button = cancel).
static int (*g_pump)(void) = NULL;
void net_set_pump(int (*fn)(void)) { g_pump = fn; }

static int http_request(const char *url, const char *body, buf_t *buf,
                        long *http_out) {
    buf->data = NULL;
    buf->len = buf->cap = 0;
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (body) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (Linux; Android 11; Pixel 3a) "
                     "AppleWebKit/537.36 (KHTML, like Gecko) "
                     "Chrome/83.0.4103.101 Mobile Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // Prototype shortcut: no CA bundle configured on device yet.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    CURLcode res = CURLE_FAILED_INIT;
    long code = 0;
    int aborted = 0;
    CURLM *mh = curl_multi_init();
    if (mh && curl_multi_add_handle(mh, curl) == CURLM_OK) {
        int running = 1;
        while (running) {
            curl_multi_perform(mh, &running);
            if (g_pump && g_pump()) {
                aborted = 1;
                break;
            }
            if (running) curl_multi_wait(mh, NULL, 0, 30, NULL);
        }
        if (!aborted) {
            int nmsg = 0;
            CURLMsg *msg = curl_multi_info_read(mh, &nmsg);
            if (msg) res = msg->data.result;
            if (res == CURLE_OK)
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        }
        curl_multi_remove_handle(mh, curl);
        curl_multi_cleanup(mh);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (http_out) *http_out = code;
    if (aborted) {
        free(buf->data);
        buf->data = NULL;
        return -1;
    }
    if (res != CURLE_OK) {
        printf("request failed: %s\n", curl_easy_strerror(res));
        free(buf->data);
        buf->data = NULL;
        return -1;
    }
    return 0;
}

// ---------- search ----------

result_t g_results[MAX_RESULTS];
int g_result_count = 0;
int g_route = ROUTE_NONE;
int g_direct_ctx = 0;
int g_route_instance = -1;
int g_quality = 1; // v3.5 settings: 0 low / 1 standard / 2 high

// Concatenate the run texts of one flexColumn (index idx) of an item.
static void extract_column(json_t *item, int idx, char *out, size_t out_size) {
    out[0] = '\0';
    json_t *cols = json_object_get(item, "flexColumns");
    if (!json_is_array(cols) || json_array_size(cols) <= (size_t)idx) return;
    json_t *col = json_object_get(
        json_array_get(cols, idx),
        "musicResponsiveListItemFlexColumnRenderer");
    json_t *runs = json_object_get(json_object_get(col, "text"), "runs");
    if (!json_is_array(runs)) return;
    size_t i;
    json_t *run;
    json_array_foreach(runs, i, run) {
        const char *t = json_string_value(json_object_get(run, "text"));
        if (t) {
            size_t room = out_size - strlen(out) - 1;
            if (room > 0) strncat(out, t, room);
        }
    }
}

// Recursive walk of the search JSON. Two v3.2 stack fixes live here:
// the per-level title buffer used to sit in every recursion frame
// (~200 B x depth); it is a single static scratch buffer now (each
// level finishes copying it into g_results before recursing), and the
// walk is depth-capped so a pathological response cannot grow the
// stack without bound.
#define COLLECT_MAX_DEPTH 40

static void collect(json_t *node, int depth) {
    if (g_result_count >= MAX_RESULTS || depth > COLLECT_MAX_DEPTH) return;
    if (json_is_object(node)) {
        json_t *item = json_object_get(node, "musicResponsiveListItemRenderer");
        if (item) {
            json_t *pid = json_object_get(item, "playlistItemData");
            const char *vid =
                json_string_value(json_object_get(pid, "videoId"));
            static char title[TITLE_LEN];
            extract_column(item, 0, title, sizeof(title));
            if (vid && title[0]) {
                result_t *r = &g_results[g_result_count];
                strncpy(r->title, title, TITLE_LEN - 1);
                r->title[TITLE_LEN - 1] = '\0';
                extract_column(item, 1, r->sub, SUB_LEN);
                strncpy(r->id, vid, VID_LEN - 1);
                r->id[VID_LEN - 1] = '\0';
                g_result_count++;
            }
        }
        const char *key;
        json_t *value;
        json_object_foreach(node, key, value) collect(value, depth + 1);
    } else if (json_is_array(node)) {
        size_t i;
        json_t *value;
        json_array_foreach(node, i, value) collect(value, depth + 1);
    }
}

// Escape a string for embedding in a JSON string literal.
// Output must have room for 6x input + 1; returns bytes written.
static size_t json_escape(const char *in, char *out, size_t out_cap) {
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        char tmp[8];
        const char *rep = NULL;
        if (*p == '"') rep = "\\\"";
        else if (*p == '\\') rep = "\\\\";
        else if (*p < 0x20) {
            snprintf(tmp, sizeof(tmp), "\\u%04x", *p);
            rep = tmp;
        }
        if (rep) {
            size_t l = strlen(rep);
            if (o + l + 1 > out_cap) break;
            memcpy(out + o, rep, l);
            o += l;
        } else {
            if (o + 2 > out_cap) break;
            out[o++] = (char)*p;
        }
    }
    out[o] = '\0';
    return o;
}

int yt_search(const char *query) {
    char url[256];
    snprintf(url, sizeof(url),
             "https://music.youtube.com/youtubei/v1/search?key=%s"
             "&prettyPrint=false",
             API_KEY);

    // v3.2: request scratch lives on the heap, not the stack - this
    // function sits above deep curl/mbedTLS and jansson call chains.
    char *esc = (char *)malloc(512);
    char *body = (char *)malloc(1024);
    if (!esc || !body) {
        free(esc);
        free(body);
        return -1;
    }
    json_escape(query, esc, 512);
    snprintf(body, 1024,
             "{\"context\":{\"client\":{\"clientName\":\"WEB_REMIX\","
             "\"clientVersion\":\"1.20241010.01.00\",\"hl\":\"ja\","
             "\"gl\":\"JP\"}},\"query\":\"%s\","
             "\"params\":\"EgWKAQIIAWoKEAoQAxAEEAUQCg==\"}",
             esc);
    free(esc);

    buf_t buf;
    long http = 0;
    int req_rc = http_request(url, body, &buf, &http);
    free(body);
    if (req_rc != 0) return -1;
    printf("HTTP %ld, %u bytes\n", http, (unsigned)buf.len);
    if (http != 200) {
        free(buf.data);
        return -1;
    }
    json_error_t jerr;
    json_t *root = json_loads(buf.data, 0, &jerr);
    free(buf.data);
    if (!root) {
        printf("JSON parse error: %s\n", jerr.text);
        return -1;
    }
    g_result_count = 0;
    collect(root, 0);
    json_decref(root);
    return g_result_count > 0 ? 0 : -1;
}

// ---------- direct stream resolution (InnerTube /player) ----------
//
// FourthTube (GPLv3) obtains playable streams by POSTing to the regular
// InnerTube /player endpoint on m.youtube.com with a spoofed client
// context, a fresh visitorData (scraped from the sw.js_data
// service-worker blob) and signatureTimestamp "0", so the response
// carries plain `url`s (no signatureCipher) for the adaptive formats.
// It ships several context shapes; v3.3 uses two of them:
//   - VISIONOS (var_player_response 2): v3.3's primary. VM probes on
//     2026-10-31 showed its URLs download COMPLETELY (3 songs x itags
//     249/250/251), while the shapes below were all cut at exactly
//     524,288 bytes - the issued token only authorizes a ~512 KiB
//     prefix window of the file (the URL carries vprv=1). The cut is
//     not connection- or IP-related: it reproduces with one persistent
//     connection, with fresh connections, with the &range= parameter
//     form, and at any chunk size; a fresh token's first request past
//     the window is refused too, so re-resolution can never resume
//     past it. See report.md section 7 for the probe data.
//   - The ANDROID+iPhone recipe (var_player_response 0, v3's primary):
//     kept as a last resort in case VISIONOS playability breaks.
// Re-implemented here from that understanding; see
// ../fourthtube-analysis/ANALYSIS.md for the full writeup.

static int fetch_visitor_data(char *out, size_t out_cap);

// v3.29: visitorData is reused across resolves for 30
// minutes - fetching it fresh on every song select cost a
// whole extra round trip, a big part of the ~1 s stall the
// user reported. resolve_stream_direct drops the cache when
// a context shape fails, so the next shape starts fresh.
static char g_visitor_cache[1024];
static u64 g_visitor_at = 0;

static void visitor_data_cached(char *out, size_t cap) {
    u64 now = osGetTime();
    if (!g_visitor_cache[0] ||
        now - g_visitor_at > 1800ULL * 1000ULL) {
        g_visitor_cache[0] = '\0';
        if (fetch_visitor_data(g_visitor_cache,
                               sizeof(g_visitor_cache)) == 0)
            g_visitor_at = now;
    }
    snprintf(out, cap, "%s", g_visitor_cache);
}

static void visitor_cache_drop(void) { g_visitor_cache[0] = '\0'; }

// Fetch a fresh visitorData token (FourthTube: GET youtube.com/sw.js_data,
// value at JSON path [0][2][0][0][13] after the )]}' prefix).
// Returns 0 on success.
static int fetch_visitor_data(char *out, size_t out_cap) {
    buf_t buf;
    long http = 0;
    if (http_request("https://www.youtube.com/sw.js_data", NULL, &buf,
                     &http) != 0 ||
        http != 200 || !buf.data)
        return -1;
    const char *p = buf.data;
    if (strncmp(p, ")]}'", 4) == 0) {
        p += 4;
        while (*p == '\n' || *p == '\r') p++;
    }
    json_error_t jerr;
    json_t *root = json_loads(p, 0, &jerr);
    free(buf.data);
    if (!root) return -1;
    // [0][2][0][0][13]
    json_t *n = json_array_get(root, 0);
    n = json_array_get(n, 2);
    n = json_array_get(n, 0);
    n = json_array_get(n, 0);
    n = json_array_get(n, 13);
    const char *vd = json_string_value(n);
    int ok = -1;
    if (vd) {
        strncpy(out, vd, out_cap - 1);
        out[out_cap - 1] = '\0';
        ok = 0;
    }
    json_decref(root);
    return ok;
}

// ctx: 1 = VISIONOS (primary), 2 = recipe ANDROID+iPhone.
static int resolve_stream_direct_ctx(const char *video_id, int ctx,
                                     char *url_out, size_t url_cap,
                                     int *itag_out,
                                     long *length_sec_out) {
    static const int pref_by_quality[3][4] = {
        {249, 250, 251, 0}, // low
        {250, 249, 251, 0}, // standard
        {251, 250, 249, 0}, // high
    };
    const int *pref_itags =
        pref_by_quality[(g_quality >= 0 && g_quality <= 2) ? g_quality
                                                           : 1];

    // v3.2 stack fix: this function's v3.1 frame was 6,736 bytes, most
    // of it these three stack buffers (visitor 1 KB + escaped
    // visitorData 2 KB + request body 3 KB), held live across the
    // curl/mbedTLS call below. On real hardware the main stack ran
    // out at the memset zero-initialising `visitor` on entry (data
    // abort, write to SP-0xC). All three live in one heap block now.
    char *scratch = (char *)malloc(1024 + 2048 + 3072);
    if (!scratch) return -1;
    char *visitor = scratch;            // 1024 bytes
    char *esc_vd = scratch + 1024;      // 2048 bytes
    char *body = scratch + 1024 + 2048; // 3072 bytes
    visitor[0] = '\0';
    visitor_data_cached(visitor, 1024); // optional but helps
    json_escape(visitor, esc_vd, 2048);

    char url[256];
    snprintf(url, sizeof(url),
             "https://m.youtube.com/youtubei/v1/player?key=%s"
             "&prettyPrint=false",
             WEB_API_KEY);

    // NOTE: the trailing \" inside userAgent is intentional - it is part
    // of FourthTube's verified-working request shapes (both contexts).
    if (ctx == 1) {
        // VISIONOS, verbatim from FourthTube's video.cpp
        // (var_player_response == 2). Verified from the build VM on
        // 2026-10-31 with this exact body: playability OK and the
        // issued URL downloads completely (no 512 KiB window).
        snprintf(body, 3072,
                 "{\"videoId\":\"%s\",\"context\":{\"client\":{"
                 "\"hl\":\"en\",\"gl\":\"US\","
                 "\"clientName\":\"VISIONOS\","
                 "\"clientVersion\":\"0.1\",\"deviceMake\":\"Apple\","
                 "\"deviceModel\":\"iPhone16,2\",\"osName\":\"iPhone\","
                 "\"userAgent\":\"com.google.ios.youtube/20.10.4 "
                 "(iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)\\\"\","
                 "\"osVersion\":\"2.2.22N842\",\"visitorData\":\"%s\"}},"
                 "\"playbackContext\":{\"contentPlaybackContext\":{"
                 "\"signatureTimestamp\":\"0\"}}}",
                 video_id, esc_vd);
    } else {
        // The v3 recipe: ANDROID context with iPhone device fields.
        snprintf(body, 3072,
                 "{\"videoId\":\"%s\",\"context\":{\"client\":{"
                 "\"hl\":\"en\",\"gl\":\"US\",\"clientName\":\"ANDROID\","
                 "\"clientVersion\":\"20.10.38\",\"deviceMake\":\"Apple\","
                 "\"deviceModel\":\"iPhone9,1\",\"osName\":\"iPhone\","
                 "\"userAgent\":\"com.google.ios.youtube/19.01.1 "
                 "(iPhone9,1; U; CPU iOS 15_0_0 like Mac OS X;)\\\"\","
                 "\"osVersion\":\"15.0.0.19A346\",\"visitorData\":\"%s\"}},"
                 "\"playbackContext\":{\"contentPlaybackContext\":{"
                 "\"signatureTimestamp\":\"0\"}}}",
                 video_id, esc_vd);
    }

    buf_t buf;
    long http = 0;
    int req_rc = http_request(url, body, &buf, &http);
    free(scratch); // body/visitor scratch not needed past the request
    if (req_rc != 0) return -1;
    if (http != 200) {
        free(buf.data);
        return -1;
    }
    json_error_t jerr;
    json_t *root = json_loads(buf.data, 0, &jerr);
    free(buf.data);
    if (!root) return -1;

    int found = -1;
    json_t *ps = json_object_get(
        json_object_get(root, "playabilityStatus"), "status");
    if (ps && json_is_string(ps) &&
        strcmp(json_string_value(ps), "OK") == 0) {
        json_t *vd = json_object_get(root, "videoDetails");
        if (length_sec_out) {
            json_t *ls = json_object_get(vd, "lengthSeconds");
            const char *lss = json_string_value(ls);
            *length_sec_out =
                lss ? atol(lss) : (long)json_integer_value(ls);
        }
        json_t *fmts = json_object_get(
            json_object_get(root, "streamingData"), "adaptiveFormats");
        if (json_is_array(fmts)) {
            for (int p = 0; pref_itags[p] && found != 0; p++) {
                size_t i;
                json_t *f;
                json_array_foreach(fmts, i, f) {
                    json_t *u = json_object_get(f, "url");
                    const char *mime =
                        json_string_value(json_object_get(f, "mimeType"));
                    if (u && mime &&
                        (int)json_integer_value(
                            json_object_get(f, "itag")) == pref_itags[p] &&
                        strstr(mime, "audio/webm") &&
                        strstr(mime, "opus")) {
                        strncpy(url_out, json_string_value(u), url_cap - 1);
                        url_out[url_cap - 1] = '\0';
                        if (itag_out) *itag_out = pref_itags[p];
                        found = 0;
                        break;
                    }
                }
            }
        }
    }
    json_decref(root);
    return found;
}

int resolve_stream_direct(const char *video_id, char *url_out,
                          size_t url_cap, int *itag_out,
                          long *length_sec_out) {
    g_direct_ctx = 0;
    if (resolve_stream_direct_ctx(video_id, 1, url_out, url_cap,
                                  itag_out, length_sec_out) == 0) {
        g_direct_ctx = 1; // VISIONOS
        return 0;
    }
    printf("direct VISIONOS resolve failed, trying recipe context\n");
    visitor_cache_drop(); // fresh token for the other shape
    if (resolve_stream_direct_ctx(video_id, 2, url_out, url_cap,
                                  itag_out, length_sec_out) == 0) {
        g_direct_ctx = 2; // recipe ANDROID+iPhone
        return 0;
    }
    return -1;
}

// ---------- stream resolution via Invidious ----------

int resolve_stream_invidious_at(int idx, const char *video_id,
                                char *url_out, size_t url_cap,
                                int *itag_out, long *length_sec_out,
                                long *http_out) {
    static const char *pref_by_quality[3][4] = {
        {"249", "250", "251", NULL}, // low
        {"250", "249", "251", NULL}, // standard
        {"251", "250", "249", NULL}, // high
    };
    const char **pref_itags =
        pref_by_quality[(g_quality >= 0 && g_quality <= 2) ? g_quality
                                                           : 1];
    if (http_out) *http_out = 0;
    if (idx < 0 || idx >= invidious_instance_count()) return -1;
    char api_url[256];
    snprintf(api_url, sizeof(api_url), "https://%s/api/v1/videos/%s",
             INVIDIOUS_INSTANCES[idx], video_id);
    buf_t buf;
    long http = 0;
    int req_rc = http_request(api_url, NULL, &buf, &http);
    if (http_out) *http_out = http;
    if (req_rc != 0 || http != 200) {
        free(buf.data);
        return -1;
    }
    json_error_t jerr;
    json_t *root = json_loads(buf.data, 0, &jerr);
    free(buf.data);
    if (!root) return -1;
    if (length_sec_out)
        *length_sec_out =
            (long)json_integer_value(json_object_get(root, "lengthSeconds"));
    json_t *fmts = json_object_get(root, "adaptiveFormats");
    int found = 0;
    if (json_is_array(fmts)) {
        for (int p = 0; pref_itags[p] && !found; p++) {
            size_t i;
            json_t *f;
            json_array_foreach(fmts, i, f) {
                const char *itag =
                    json_string_value(json_object_get(f, "itag"));
                const char *type =
                    json_string_value(json_object_get(f, "type"));
                const char *u =
                    json_string_value(json_object_get(f, "url"));
                if (itag && type && u &&
                    strcmp(itag, pref_itags[p]) == 0 &&
                    strstr(type, "opus") != NULL) {
                    strncpy(url_out, u, url_cap - 1);
                    url_out[url_cap - 1] = '\0';
                    if (itag_out) *itag_out = atoi(itag);
                    found = 1;
                    break;
                }
            }
        }
    }
    json_decref(root);
    return found ? 0 : -1;
}

int resolve_stream_invidious(const char *video_id, char *url_out,
                             size_t url_cap, int *itag_out,
                             long *length_sec_out) {
    g_route_instance = -1;
    for (int inst = 0; INVIDIOUS_INSTANCES[inst]; inst++) {
        if (resolve_stream_invidious_at(inst, video_id, url_out, url_cap,
                                        itag_out, length_sec_out,
                                        NULL) == 0) {
            g_route_instance = inst;
            return 0;
        }
    }
    return -1;
}

// ---------- stream resolution: direct first, Invidious fallback ----------

int resolve_stream(const char *video_id, char *url_out, size_t url_cap,
                   int *itag_out, long *length_sec_out) {
    g_route = ROUTE_NONE;
    if (resolve_stream_direct(video_id, url_out, url_cap, itag_out,
                              length_sec_out) == 0) {
        g_route = ROUTE_DIRECT;
        return 0;
    }
    printf("direct resolve failed, trying Invidious\n");
    if (resolve_stream_invidious(video_id, url_out, url_cap, itag_out,
                                 length_sec_out) == 0) {
        g_route = ROUTE_INVIDIOUS;
        return 0;
    }
    return -1;
}
