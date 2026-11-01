// store.h - on-SD persistence: favourites, config, dislikes,
// plus the active-list accessors (results vs favourites).
#ifndef STORE_H
#define STORE_H

#include "app.h"

// Crash-safe file writes (v3.33): data is written to
// "<path>.tmp" and rename()d over the target only when
// complete, via a short .bak rotation, so a sudden power
// loss mid-write leaves the previous file intact.
FILE *atomic_open(const char *path, char *tmp_out, size_t tmp_cap);
int atomic_commit(FILE *f, const char *tmp_path, const char *path);
void atomic_abort(FILE *f, const char *tmp_path);
// Boot-time recovery: restore a .bak whose rename was
// interrupted, drop stale .tmp files (incl. in cache/).
void store_recover(void);

int fav_find(const char *id);
int fav_toggle(const result_t *song);
void favs_load(void);
void favs_save(void);
void config_load(void);
void config_save(void);
int dislike_has(const char *id);
void dislike_add(const char *id);
int like_has(const char *id);
int like_toggle(const char *id);
int list_tab(void);
int play_count(void);
result_t *play_at(int i);
int act_count(void);
result_t *act_at(int i);
int act_bank(void);

#endif // STORE_H
