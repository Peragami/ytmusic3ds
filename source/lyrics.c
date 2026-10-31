// lyrics.c - see lyrics.h.
//
// v3.9 robustness pass: LRCLIB intermittently answers 503
// ("The server is busy, please retry in a moment" - verified from
// the host), which v3.6-v3.8 surfaced as a plain "not found".
// http_get now retries 5xx up to 3 times with a short wait, the
// fetch tries more query shapes (exact get, search narrowed by
// artist, plain search, cleaned-title variants), and the outcome
// reason is kept in a debug string the UI can show.

#include "lyrics.h"

#include <3ds.h>
#include <curl/curl.h>
#include <jansson.h>

#include <stdlib.h>
#include <string.h>

#define MAX_RESP (3 * 1024 * 1024)
#define MAX_LYRICS_BYTES 20000
#define MAX_LINES 400

static char *g_text = NULL; // lyrics with '\n' replaced by '\0'
static const char *g_lines[MAX_LINES];
static int g_nlines = 0;
static char g_for_id[16] = "";
static int g_found = 0;
static char g_debug[80] = "";

// Attempt outcome codes (aggregated for the debug string).
#define OUT_FOUND 0
#define OUT_NOMATCH 1 // server answered, nothing usable
#define OUT_BUSY 2    // 5xx even after retries
#define OUT_NET 3     // transport error

// ---------- HTTP ----------
typedef struct {
    char *data;
    size_t len;
    int overflow;
} buf_t;

static size_t buf_write(void *ptr, size_t size, size_t nmemb, void *ud) {
    size_t total = size * nmemb;
    buf_t *b = (buf_t *)ud;
    if (b->len + total > MAX_RESP) {
        b->overflow = 1;
        return 0;
    }
    char *nb = realloc(b->data, b->len + total + 1);
    if (!nb) {
        b->overflow = 1;
        return 0;
    }
    b->data = nb;
    memcpy(b->data + b->len, ptr, total);
    b->len += total;
    b->data[b->len] = '\0';
    return total;
}

static long g_last_http = 0;

// GET with retries on 5xx. Returns 0 only on HTTP 200 with a body.
static int http_get(const char *url, buf_t *out) {
    for (int attempt = 0; attempt < 3; attempt++) {
        free(out->data);
        memset(out, 0, sizeof(*out));
        CURL *curl = curl_easy_init();
        if (!curl) return OUT_NET;
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buf_write);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
                         "ytmusic3ds/3.9 (Old 3DS homebrew client)");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        CURLcode res = curl_easy_perform(curl);
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        curl_easy_cleanup(curl);
        g_last_http = code;
        if (res == CURLE_OK && code == 200 && !out->overflow && out->data)
            return OUT_FOUND;
        if (code >= 500 && code < 600) {
            if (attempt < 2) {
                svcSleepThread(1200000000ULL); // "retry in a moment"
                continue;
            }
            return OUT_BUSY;
        }
        if (res != CURLE_OK) return OUT_NET;
        return OUT_NOMATCH; // 404 etc: a definitive answer
    }
    return OUT_BUSY;
}

// Percent-encode a UTF-8 string for a query value.
static void url_encode(const char *in, char *out, size_t cap) {
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' ||
            *p == '.' || *p == '~') {
            if (o + 1 >= cap) break;
            out[o++] = (char)*p;
        } else {
            if (o + 3 >= cap) break;
            out[o++] = '%';
            out[o++] = hex[*p >> 4];
            out[o++] = hex[*p & 15];
        }
    }
    out[o] = '\0';
}

// ---------- storage ----------
static void store_lyrics(const char *text) {
    free(g_text);
    g_text = NULL;
    g_nlines = 0;
    g_found = 0;
    if (!text || !text[0]) return;
    size_t len = strlen(text);
    if (len > MAX_LYRICS_BYTES) len = MAX_LYRICS_BYTES;
    g_text = (char *)malloc(len + 1);
    if (!g_text) return;
    memcpy(g_text, text, len);
    g_text[len] = '\0';
    g_lines[g_nlines++] = g_text;
    for (size_t i = 0; i < len && g_nlines < MAX_LINES; i++) {
        if (g_text[i] == '\n') {
            g_text[i] = '\0';
            if (i + 1 < len) g_lines[g_nlines++] = g_text + i + 1;
        }
        if (g_text[i] == '\r') g_text[i] = '\0';
    }
    g_found = 1;
}

void lyrics_reset(void) {
    free(g_text);
    g_text = NULL;
    g_nlines = 0;
    g_found = 0;
    g_for_id[0] = '\0';
    g_debug[0] = '\0';
}

int lyrics_line_count(void) { return g_found ? g_nlines : 0; }

const char *lyrics_line(int i) {
    if (!g_found || i < 0 || i >= g_nlines) return "";
    return g_lines[i];
}

const char *lyrics_debug(void) { return g_debug; }

// ---------- fetch attempts ----------
static int try_get(const char *title, const char *artist) {
    char et[512], ea[512];
    url_encode(title, et, sizeof(et));
    url_encode(artist, ea, sizeof(ea));
    char url[1200];
    snprintf(url, sizeof(url),
             "https://lrclib.net/api/get?track_name=%s&artist_name=%s",
             et, ea);
    buf_t b;
    memset(&b, 0, sizeof(b));
    int rc = http_get(url, &b);
    if (rc == OUT_FOUND) {
        rc = OUT_NOMATCH;
        json_error_t je;
        json_t *root = json_loads(b.data, 0, &je);
        if (root) {
            const char *pl =
                json_string_value(json_object_get(root, "plainLyrics"));
            if (pl && pl[0]) {
                store_lyrics(pl);
                rc = OUT_FOUND;
            }
            json_decref(root);
        }
    }
    free(b.data);
    return rc;
}

// with_artist != 0 narrows the search with artist_name.
static int try_search(const char *title, const char *artist,
                      int with_artist) {
    char et[512], ea[512];
    url_encode(title, et, sizeof(et));
    char url[1300];
    if (with_artist && artist && artist[0]) {
        url_encode(artist, ea, sizeof(ea));
        snprintf(url, sizeof(url),
                 "https://lrclib.net/api/search?track_name=%s&artist_name=%s",
                 et, ea);
    } else {
        snprintf(url, sizeof(url),
                 "https://lrclib.net/api/search?track_name=%s", et);
    }
    buf_t b;
    memset(&b, 0, sizeof(b));
    int rc = http_get(url, &b);
    if (rc == OUT_FOUND) {
        rc = OUT_NOMATCH;
        json_error_t je;
        json_t *root = json_loads(b.data, 0, &je);
        if (root && json_is_array(root)) {
            const char *fallback = NULL;
            size_t i;
            json_t *e;
            json_array_foreach(root, i, e) {
                const char *pl = json_string_value(
                    json_object_get(e, "plainLyrics"));
                if (!pl || !pl[0]) continue;
                const char *an = json_string_value(
                    json_object_get(e, "artistName"));
                if (artist && artist[0] && an &&
                    strstr(an, artist) != NULL) {
                    store_lyrics(pl);
                    rc = OUT_FOUND;
                    break;
                }
                if (!fallback) fallback = pl;
            }
            if (rc != OUT_FOUND && fallback) {
                store_lyrics(fallback);
                rc = OUT_FOUND;
            }
            json_decref(root);
        }
    }
    free(b.data);
    return rc;
}

// Strip decorations that hurt exact matching: bracketed segments
// and "feat." tails.
static void clean_title(const char *in, char *out, size_t cap) {
    strncpy(out, in, cap - 1);
    out[cap - 1] = '\0';
    char *cut;
    if ((cut = strstr(out, "【")) != NULL) *cut = '\0';
    if ((cut = strstr(out, " feat")) != NULL) *cut = '\0';
    if ((cut = strstr(out, " (")) != NULL) *cut = '\0';
    if ((cut = strstr(out, "（")) != NULL) *cut = '\0';
    size_t n = strlen(out);
    while (n > 0 && out[n - 1] == ' ') out[--n] = '\0';
}

int lyrics_fetch(const char *video_id, const char *title,
                 const char *artist) {
    if (video_id && g_for_id[0] &&
        strncmp(g_for_id, video_id, sizeof(g_for_id) - 1) == 0)
        return g_found ? 0 : -1;
    lyrics_reset();
    if (video_id) {
        strncpy(g_for_id, video_id, sizeof(g_for_id) - 1);
        g_for_id[sizeof(g_for_id) - 1] = '\0';
    }
    if (!title || !title[0]) {
        snprintf(g_debug, sizeof(g_debug), "タイトル不明");
        return -1;
    }
    int saw_busy = 0, saw_answer = 0;
    int rc;
    // If the server reports busy even after retries, every other
    // query shape will almost surely fail the same way - stop the
    // chain early so the download is not stalled for long.
#define TRY(expr)                      \
    {                                  \
        rc = (expr);                   \
        if (rc == OUT_FOUND) return 0; \
        if (rc == OUT_BUSY) {          \
            saw_busy = 1;              \
            break;                     \
        }                              \
        if (rc == OUT_NOMATCH)         \
            saw_answer = 1;            \
    }
    do {
        if (artist && artist[0]) TRY(try_get(title, artist));
        if (artist && artist[0]) TRY(try_search(title, artist, 1));
        TRY(try_search(title, artist, 0));
        char cleaned[256];
        clean_title(title, cleaned, sizeof(cleaned));
        if (strcmp(cleaned, title) != 0 && cleaned[0]) {
            if (artist && artist[0]) TRY(try_get(cleaned, artist));
            TRY(try_search(cleaned, artist, 0));
        }
    } while (0);
#undef TRY
    if (saw_busy)
        snprintf(g_debug, sizeof(g_debug),
                 "歌詞サーバーが混雑しています (HTTP %ld)",
                 g_last_http);
    else if (!saw_answer)
        snprintf(g_debug, sizeof(g_debug), "ネットワークエラー");
    else
        snprintf(g_debug, sizeof(g_debug),
                 "歌詞データベースに登録がありません");
    return -1;
}

// ---------- like count (v3.16) ----------
// YouTube no longer publishes like counts; the Return YouTube
// Dislike project's public API reconstructs an estimate from
// archived data and its own users' votes. One small JSON GET.
int likes_fetch(const char *video_id, long *likes_out) {
    if (!video_id || !video_id[0]) return -1;
    char url[160];
    snprintf(url, sizeof(url),
             "https://returnyoutubedislikeapi.com/votes?videoId=%s",
             video_id);
    buf_t b;
    memset(&b, 0, sizeof(b));
    int rc = http_get(url, &b);
    if (rc != 0 || !b.data) {
        free(b.data);
        return -1;
    }
    json_error_t je;
    json_t *root = json_loads(b.data, 0, &je);
    free(b.data);
    if (!root) return -1;
    json_t *jl = json_object_get(root, "likes");
    int ok = json_is_integer(jl);
    if (ok) *likes_out = (long)json_integer_value(jl);
    json_decref(root);
    return ok ? 0 : -1;
}
