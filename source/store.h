// store.h - on-SD persistence: favourites, config, dislikes,
// plus the active-list accessors (results vs favourites).
#ifndef STORE_H
#define STORE_H

#include "app.h"

int fav_find(const char *id);
int fav_toggle(const result_t *song);
void favs_load(void);
void favs_save(void);
void config_load(void);
void config_save(void);
int dislike_has(const char *id);
void dislike_add(const char *id);
int list_tab(void);
int act_count(void);
result_t *act_at(int i);
int act_bank(void);

#endif // STORE_H
