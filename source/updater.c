// updater.c - see updater.h.

#include "updater.h"

#include <curl/curl.h>
#include <jansson.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"

#define MAX_MANIFEST (64 * 1024)

// ---------- version compare ----------
static int parse_ver(const char *s, int *maj, int *min) {
    *maj = 0;
    *min = 0;
    if (!s) return -1;
    if (*s == 'v' || *s == 'V') s++;
    char *end;
    long a = strtol(s, &end, 10);
    if (end == s) return -1;
    long b = 0;
    if (*end == '.') b = strtol(end + 1, NULL, 10);
    *maj = (int)a;
    *min = (int)b;
    return 0;
}

int updater_version_newer(const char *remote, const char *current) {
    int rma, rmi, cma, cmi;
    if (parse_ver(remote, &rma, &rmi) != 0) return 0;
    if (parse_ver(current, &cma, &cmi) != 0) return 0;
    if (rma != cma) return rma > cma;
    return rmi > cmi;
}

// ---------- manifest fetch ----------
typedef struct {
    char *data;
    size_t len;
    int overflow;
} buf_t;

static size_t buf_write(void *ptr, size_t size, size_t nmemb, void *ud) {
    size_t total = size * nmemb;
    buf_t *b = (buf_t *)ud;
    if (b->len + total > MAX_MANIFEST) {
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

int updater_fetch_manifest(const char *manifest_url, upd_info_t *info,
                           char *err, size_t errcap) {
    memset(info, 0, sizeof(*info));
    buf_t b;
    memset(&b, 0, sizeof(b));
    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(err, errcap, "curl初期化エラー");
        return -1;
    }
    curl_easy_setopt(curl, CURLOPT_URL, manifest_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buf_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "ytmusic3ds/3.11 (Old 3DS homebrew client)");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || code != 200 || b.overflow || !b.data) {
        snprintf(err, errcap, "更新情報を取得できませんでした (HTTP %ld)",
                 code);
        free(b.data);
        return -1;
    }
    json_error_t je;
    json_t *root = json_loads(b.data, 0, &je);
    free(b.data);
    if (!root) {
        snprintf(err, errcap, "更新情報の形式が不正です");
        return -1;
    }
    const char *ver = json_string_value(json_object_get(root, "version"));
    const char *url = json_string_value(json_object_get(root, "url"));
    const char *sha = json_string_value(json_object_get(root, "sha256"));
    json_t *jsize = json_object_get(root, "size");
    if (!ver || !url) {
        snprintf(err, errcap, "更新情報に必須項目がありません");
        json_decref(root);
        return -1;
    }
    strncpy(info->version, ver, sizeof(info->version) - 1);
    strncpy(info->url, url, sizeof(info->url) - 1);
    if (sha) strncpy(info->sha256, sha, sizeof(info->sha256) - 1);
    if (jsize && json_is_integer(jsize))
        info->size = (long)json_integer_value(jsize);
    json_decref(root);
    return 0;
}

// ---------- download + swap ----------
typedef struct {
    FILE *f;
    upd_progress_fn cb;
    long total;
    long got;
    int last_pct;
} dl_t;

static int dl_progress(void *ud, curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    dl_t *d = (dl_t *)ud;
    d->got = (long)dlnow;
    if (dltotal > 0) d->total = (long)dltotal;
    if (d->cb) {
        int pct = d->total > 0 ? (int)(d->got * 100 / d->total) : -1;
        if (pct != d->last_pct) {
            d->last_pct = pct;
            d->cb(pct, "ダウンロード中...");
        }
    }
    return 0;
}

static int sha256_file_hex(const char *path, char out[65]) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    sha256_ctx ctx;
    sha256_init(&ctx);
    uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        sha256_update(&ctx, buf, n);
    fclose(f);
    uint8_t hash[32];
    sha256_final(&ctx, hash);
    sha256_hex(hash, out);
    return 0;
}

int updater_apply(const upd_info_t *info, const char *self_path,
                  const char *tmp_path, upd_progress_fn cb, char *err,
                  size_t errcap) {
    if (cb) cb(-1, "ダウンロード中...");
    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        snprintf(err, errcap, "一時ファイルを作成できません");
        return -1;
    }
    dl_t d;
    memset(&d, 0, sizeof(d));
    d.f = f;
    d.cb = cb;
    d.last_pct = -2;
    CURL *curl = curl_easy_init();
    if (!curl) {
        fclose(f);
        remove(tmp_path);
        snprintf(err, errcap, "curl初期化エラー");
        return -1;
    }
    curl_easy_setopt(curl, CURLOPT_URL, info->url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "ytmusic3ds/3.11 (Old 3DS homebrew client)");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, dl_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &d);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    fclose(f);
    if (res != CURLE_OK || code != 200) {
        remove(tmp_path);
        snprintf(err, errcap, "ダウンロードに失敗しました (HTTP %ld)",
                 code);
        return -1;
    }
    if (cb) cb(-1, "検証中...");
    if (info->size > 0) {
        FILE *vf = fopen(tmp_path, "rb");
        long sz = -1;
        if (vf) {
            fseek(vf, 0, SEEK_END);
            sz = ftell(vf);
            fclose(vf);
        }
        if (sz != info->size) {
            remove(tmp_path);
            snprintf(err, errcap,
                     "サイズが一致しません（破損の可能性）");
            return -1;
        }
    }
    if (info->sha256[0]) {
        char hex[65];
        if (sha256_file_hex(tmp_path, hex) != 0 ||
            strcasecmp(hex, info->sha256) != 0) {
            remove(tmp_path);
            snprintf(err, errcap,
                     "ハッシュが一致しません（破損の可能性）");
            return -1;
        }
    }
    // Swap: self -> self.bak, tmp -> self, drop .bak. If the
    // second rename fails, roll back from .bak.
    char bak[560];
    snprintf(bak, sizeof(bak), "%s.bak", self_path);
    remove(bak);
    if (rename(self_path, bak) != 0) {
        remove(tmp_path);
        snprintf(err, errcap, "現在のファイルを退避できません");
        return -1;
    }
    if (rename(tmp_path, self_path) != 0) {
        rename(bak, self_path);
        remove(tmp_path);
        snprintf(err, errcap, "新ファイルへの差し替えに失敗しました");
        return -1;
    }
    remove(bak);
    if (cb) cb(100, "完了");
    return 0;
}
