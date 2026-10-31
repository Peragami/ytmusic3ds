// store.c - favourites / config / dislikes persistence
// and the active-list accessors. Split out of main.c in
// v3.17; logic unchanged.

#include "store.h"

#include <stdlib.h>

// ---------- favourites ----------
result_t g_favs[THUMB_MAX_FAVS];
int g_fav_count = 0;

int fav_find(const char *id) {
    for (int i = 0; i < g_fav_count; i++)
        if (strcmp(g_favs[i].id, id) == 0) return i;
    return -1;
}

static void sanitize_field(char *s) {
    for (; *s; s++)
        if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' ';
}

void favs_save(void) {
    FILE *f = fopen(FAV_PATH, "w");
    if (!f) return;
    for (int i = 0; i < g_fav_count; i++) {
        char id[VID_LEN], t[TITLE_LEN], s[SUB_LEN];
        strncpy(id, g_favs[i].id, sizeof(id) - 1);
        id[sizeof(id) - 1] = '\0';
        strncpy(t, g_favs[i].title, sizeof(t) - 1);
        t[sizeof(t) - 1] = '\0';
        strncpy(s, g_favs[i].sub, sizeof(s) - 1);
        s[sizeof(s) - 1] = '\0';
        sanitize_field(t);
        sanitize_field(s);
        fprintf(f, "%s\t%s\t%s\n", id, t, s);
    }
    fclose(f);
}

void favs_load(void) {
    g_fav_count = 0;
    FILE *f = fopen(FAV_PATH, "r");
    if (!f) f = fopen(LEGACY_FAV_PATH, "r");
    if (!f) return;
    char line[VID_LEN + TITLE_LEN + SUB_LEN + 8];
    while (g_fav_count < THUMB_MAX_FAVS && fgets(line, sizeof(line), f)) {
        char *t1 = strchr(line, '\t');
        if (!t1) continue;
        char *t2 = strchr(t1 + 1, '\t');
        if (!t2) continue;
        *t1 = '\0';
        *t2 = '\0';
        char *nl = strchr(t2 + 1, '\n');
        if (nl) *nl = '\0';
        result_t *r = &g_favs[g_fav_count];
        memset(r, 0, sizeof(*r));
        strncpy(r->id, line, VID_LEN - 1);
        strncpy(r->title, t1 + 1, TITLE_LEN - 1);
        strncpy(r->sub, t2 + 1, SUB_LEN - 1);
        if (r->id[0] && r->title[0]) g_fav_count++;
    }
    fclose(f);
}

// Toggle favourite for a song; returns 1 = added, 0 = removed,
// -1 = library full. Thumbnail bank indices shift on removal, so
// the whole favourites bank is dropped and refetched lazily.
int fav_toggle(const result_t *song) {
    int idx = fav_find(song->id);
    if (idx >= 0) {
        for (int i = idx; i + 1 < g_fav_count; i++)
            g_favs[i] = g_favs[i + 1];
        g_fav_count--;
        favs_save();
        thumbs_clear_bank(THUMB_BANK_FAVS);
        g_tq_favs_dirty = 1;
        if (g_tq_bank == THUMB_BANK_FAVS) g_tq_bank = -1;
        return 0;
    }
    if (g_fav_count >= THUMB_MAX_FAVS) return -1;
    g_favs[g_fav_count++] = *song;
    favs_save();
    thumbs_clear_bank(THUMB_BANK_FAVS);
    g_tq_favs_dirty = 1;
    if (g_tq_bank == THUMB_BANK_FAVS) g_tq_bank = -1;
    return 1;
}

// ---------- config ----------
// v3.11: update配信URL (空なら未設定) と自分の.3dsxパス
char g_update_url[384] = "";
char g_self_path[320] = "";
char g_update_msg[96] = "";

void config_save(void) {
    FILE *f = fopen(CFG_PATH, "w");
    if (!f) return;
    fprintf(f, "quality=%d\nthumbs=%d\nshuffle=%d\nrepeat=%d\n",
            g_quality, g_thumbs_on, g_shuffle, g_repeat);
    fprintf(f, "kana=%d\n", g_kana_on);
    if (g_update_url[0]) fprintf(f, "update_url=%s\n", g_update_url);
    fclose(f);
}

void config_load(void) {
    // Default first; a config value (own or legacy) overrides it.
    strncpy(g_update_url, DEFAULT_UPDATE_URL,
            sizeof(g_update_url) - 1);
    g_update_url[sizeof(g_update_url) - 1] = '\0';
    FILE *f = fopen(CFG_PATH, "r");
    if (!f) f = fopen(LEGACY_CFG_PATH, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        int v;
        if (strncmp(line, "update_url=", 11) == 0) {
            strncpy(g_update_url, line + 11, sizeof(g_update_url) - 1);
            g_update_url[sizeof(g_update_url) - 1] = '\0';
            g_update_url[strcspn(g_update_url, "\r\n")] = '\0';
        } else if (sscanf(line, "quality=%d", &v) == 1 && v >= 0 && v <= 2)
            g_quality = v;
        else if (sscanf(line, "thumbs=%d", &v) == 1)
            g_thumbs_on = v ? 1 : 0;
        else if (sscanf(line, "shuffle=%d", &v) == 1)
            g_shuffle = v ? 1 : 0;
        else if (sscanf(line, "kana=%d", &v) == 1)
            g_kana_on = v ? 1 : 0;
        else if (sscanf(line, "repeat=%d", &v) == 1 && v >= 0 && v <= 2)
            g_repeat = v;
    }
    fclose(f);
}

// ---------- dislikes (Bad button) ----------
int dislike_has(const char *id) {
    FILE *f = fopen(DISLIKE_PATH, "r");
    if (!f) return 0;
    char line[32];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, id) == 0) {
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

void dislike_add(const char *id) {
    if (dislike_has(id)) return;
    FILE *f = fopen(DISLIKE_PATH, "a");
    if (!f) return;
    fprintf(f, "%s\n", id);
    fclose(f);
}

// ---------- active list (results or favourites) ----------
int list_tab(void) { return g_playback_view ? g_playing_tab : g_tab; }

int act_count(void) {
    return list_tab() == TAB_LIBRARY ? g_fav_count : g_result_count;
}

result_t *act_at(int i) {
    if (list_tab() == TAB_LIBRARY) return &g_favs[i];
    return &g_results[i];
}

int act_bank(void) {
    return list_tab() == TAB_LIBRARY ? THUMB_BANK_FAVS : THUMB_BANK_RESULTS;
}
