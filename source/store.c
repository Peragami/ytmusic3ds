// store.c - favourites / config / dislikes persistence
// and the active-list accessors. Split out of main.c in
// v3.17; logic unchanged.

#include "store.h"

#include <stdlib.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

// ---------- crash-safe writes (v3.33, user request) ----------
// Every data file (favourites, config, likes, dislikes, the
// thumbnail cache) used to be overwritten in place; if the
// 3DS died mid-write the file was torn and the data was gone.
// Now the new content goes to "<path>.tmp" and is renamed
// over the target only once fully written, rotating the old
// file through "<path>.bak" for one step (rename on this
// filesystem does not replace, so the dance is: current ->
// .bak, .tmp -> current, drop .bak). A crash anywhere in the
// dance leaves either the old or the new file readable, and
// store_recover() at boot tidies up the remains.
FILE *atomic_open(const char *path, char *tmp_out, size_t tmp_cap) {
    snprintf(tmp_out, tmp_cap, "%s.tmp", path);
    return fopen(tmp_out, "w");
}

int atomic_commit(FILE *f, const char *tmp_path, const char *path) {
    if (!f) return -1;
    fflush(f);
    fsync(fileno(f)); // best effort; the rename dance is the net
    if (fclose(f) != 0) {
        remove(tmp_path);
        return -1;
    }
    char bak[224];
    snprintf(bak, sizeof(bak), "%s.bak", path);
    remove(bak);
    rename(path, bak); // fine if the target does not exist yet
    if (rename(tmp_path, path) != 0) {
        rename(bak, path); // roll back
        remove(tmp_path);
        return -1;
    }
    remove(bak);
    return 0;
}

void atomic_abort(FILE *f, const char *tmp_path) {
    if (f) fclose(f);
    remove(tmp_path);
}

static int path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Boot recovery: for each data file, if the current file is
// missing but a .bak survived an interrupted rename, put the
// .bak back; drop stale .bak/.tmp leftovers, including any
// torn cache writes in cache/.
void store_recover(void) {
    static const char *names[] = {"favorites.txt", "config.txt",
                                  "likes.txt", "dislikes.txt"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char p[192], t[200], b[200];
        snprintf(p, sizeof(p), APP_DIR "/%s", names[i]);
        snprintf(t, sizeof(t), "%s.tmp", p);
        snprintf(b, sizeof(b), "%s.bak", p);
        if (!path_exists(p) && path_exists(b)) rename(b, p);
        if (path_exists(p)) remove(b);
        remove(t);
    }
    DIR *d = opendir(APP_DIR "/cache");
    if (d) {
        struct dirent *e;
        char p[256];
        while ((e = readdir(d)) != NULL) {
            size_t n = strlen(e->d_name);
            if (n > 4 && strcmp(e->d_name + n - 4, ".tmp") == 0) {
                snprintf(p, sizeof(p), APP_DIR "/cache/%s", e->d_name);
                remove(p);
            }
        }
        closedir(d);
    }
}

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
    char tmp[200];
    FILE *f = atomic_open(FAV_PATH, tmp, sizeof(tmp));
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
    atomic_commit(f, tmp, FAV_PATH);
}

// Previous versions' data folders, newest first. The first
// run of a new build imports favourites/config from whichever
// of these actually has the file (v3.20: the old single-folder
// fallback could resurrect stale data after several OTA hops).
static const char *legacy_dirs[] = {
    "ytmusic3ds-v3.36", "ytmusic3ds-v3.35", "ytmusic3ds-v3.34", "ytmusic3ds-v3.33", "ytmusic3ds-v3.32", "ytmusic3ds-v3.31", "ytmusic3ds-v3.30", "ytmusic3ds-v3.29", "ytmusic3ds-v3.28", "ytmusic3ds-v3.27", "ytmusic3ds-v3.26", "ytmusic3ds-v3.25", "ytmusic3ds-v3.24", "ytmusic3ds-v3.23", "ytmusic3ds-v3.22", "ytmusic3ds-v3.21", "ytmusic3ds-v3.20", "ytmusic3ds-v3.19", "ytmusic3ds-v3.18", "ytmusic3ds-v3.17",
    "ytmusic3ds-v3.16", "ytmusic3ds-v3.15", "ytmusic3ds-v3.14",
    "ytmusic3ds-v3.13", "ytmusic3ds-v3.12", "ytmusic3ds-v3.11",
};

static FILE *fopen_legacy(const char *name) {
    char path[320];
    for (size_t i = 0; i < sizeof(legacy_dirs) / sizeof(legacy_dirs[0]);
         i++) {
        snprintf(path, sizeof(path), "sdmc:/3ds/%s/%s",
                 legacy_dirs[i], name);
        FILE *f = fopen(path, "r");
        if (f) return f;
    }
    return NULL;
}

void favs_load(void) {
    g_fav_count = 0;
    FILE *f = fopen(FAV_PATH, "r");
    if (!f) f = fopen_legacy("favorites.txt");
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
    char tmp[200];
    FILE *f = atomic_open(CFG_PATH, tmp, sizeof(tmp));
    if (!f) return;
    fprintf(f, "quality=%d\nthumbs=%d\nshuffle=%d\nrepeat=%d\n",
            g_quality, g_thumbs_on, g_shuffle, g_repeat);
    fprintf(f, "kana=%d\n", g_kana_on);
    if (g_update_url[0]) fprintf(f, "update_url=%s\n", g_update_url);
    atomic_commit(f, tmp, CFG_PATH);
}

void config_load(void) {
    // Default first; a config value (own or legacy) overrides it.
    strncpy(g_update_url, DEFAULT_UPDATE_URL,
            sizeof(g_update_url) - 1);
    g_update_url[sizeof(g_update_url) - 1] = '\0';
    FILE *f = fopen(CFG_PATH, "r");
    if (!f) f = fopen_legacy("config.txt");
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

// Shared id-list load/save for likes.txt and dislikes.txt.
static int id_list_load(const char *path, char (*ids)[16], int max) {
    int n = 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[32];
    while (n < max && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        snprintf(ids[n], 16, "%s", line);
        n++;
    }
    fclose(f);
    return n;
}

static void id_list_save(const char *path, char (*ids)[16], int n) {
    char tmp[200];
    FILE *f = atomic_open(path, tmp, sizeof(tmp));
    if (!f) return;
    for (int i = 0; i < n; i++) fprintf(f, "%s\n", ids[i]);
    atomic_commit(f, tmp, path);
}

void dislike_add(const char *id) {
    static char ids[512][16];
    int n = id_list_load(DISLIKE_PATH, ids, 512);
    for (int i = 0; i < n; i++)
        if (strcmp(ids[i], id) == 0) return;
    if (n >= 512) return;
    snprintf(ids[n], 16, "%s", id);
    id_list_save(DISLIKE_PATH, ids, n + 1);
}

// ---------- likes (Like button, v3.24) ----------
// Like the real app, Like is now independent from the Library:
// the Save pill (or the X key) manages favourites, the thumb
// only records the like in likes.txt.
int like_has(const char *id) {
    FILE *f = fopen(LIKE_PATH, "r");
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

// Returns 1 when the song is liked after the call, 0 when not.
int like_toggle(const char *id) {
    static char ids[512][16];
    int n = id_list_load(LIKE_PATH, ids, 512);
    int found = -1;
    for (int i = 0; i < n; i++)
        if (strcmp(ids[i], id) == 0) found = i;
    if (found >= 0) {
        for (int i = found; i + 1 < n; i++)
            memcpy(ids[i], ids[i + 1], 16);
        id_list_save(LIKE_PATH, ids, n - 1);
        return 0;
    }
    if (n >= 512) return 0;
    snprintf(ids[n], 16, "%s", id);
    id_list_save(LIKE_PATH, ids, n + 1);
    return 1;
}

// ---------- active list (results or favourites) ----------
// v3.39: the browsed list always follows g_tab, even during
// playback - the user can open Library / Home / Settings while
// a song plays. The playing song's own list is tracked
// separately (g_playing_tab) and reached via play_count() /
// play_at() below, which drive auto-advance and skips.
int list_tab(void) { return g_tab; }

int play_count(void) {
    return g_playing_tab == TAB_LIBRARY ? g_fav_count : g_result_count;
}

result_t *play_at(int i) {
    if (g_playing_tab == TAB_LIBRARY) return &g_favs[i];
    return &g_results[i];
}

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
