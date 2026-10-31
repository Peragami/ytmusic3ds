// updater.h - self-update for ytmusic3ds (v3.11).
//
// The app fetches a small JSON manifest:
//   {"version":"3.12","url":"https://.../ytmusic3ds.3dsx",
//    "sha256":"<hex>","size":1234567}
// from the URL stored in config.txt (update_url=...). When the
// manifest version is newer, the new .3dsx is downloaded next to
// the app, verified (size + SHA-256), and swapped into place of
// the running file (old file kept as .bak until the swap lands).
// The user then restarts the app manually.
#ifndef UPDATER_H
#define UPDATER_H

#include <stddef.h>

typedef struct {
    char version[16];
    char url[512];
    char sha256[65];
    long size;
} upd_info_t;

// Progress callback: pct 0-100 (or -1 when unknown), phase text.
typedef void (*upd_progress_fn)(int pct, const char *phase);

// 0 on success (info filled), -1 on failure (err filled).
int updater_fetch_manifest(const char *manifest_url, upd_info_t *info,
                           char *err, size_t errcap);

// 1 when remote ("3.12") is newer than current ("3.11").
int updater_version_newer(const char *remote, const char *current);

// Download + verify + swap into self_path. 0 on success.
int updater_apply(const upd_info_t *info, const char *self_path,
                  const char *tmp_path, upd_progress_fn cb, char *err,
                  size_t errcap);

#endif
