// lyrics.h - plain lyrics lookup via the free LRCLIB API
// (https://lrclib.net, no key required).
//
// Fetch strategy for a song: first /api/get with track+artist
// (exact-ish, small response); if that misses, /api/search by
// track title and take the first entry carrying plainLyrics
// (preferring one whose artist matches the hint). Results are
// cached per videoId in memory only.
//
// Coverage is best-effort: many songs (especially ones whose
// YouTube title differs from the release title) will not be found,
// in which case the UI simply says so.
#ifndef LYRICS_H
#define LYRICS_H

// Forget any cached lyrics.
void lyrics_reset(void);

// Fetch lyrics for a song. artist may be "" when unknown.
// Returns 0 when lyrics were found, -1 otherwise.
int lyrics_fetch(const char *video_id, const char *title,
                 const char *artist);

// v3.43 worker-thread split: fetch without touching the store
// (on success *text_out is a malloc'd copy), then store the text
// on the main thread with lyrics_store_for().
int lyrics_fetch_text(const char *title, const char *artist,
                      char **text_out, char **sync_out);
void lyrics_store_for(const char *video_id, const char *text,
                      const char *sync_raw);

// Synced (LRC) lyrics, v3.45: when the stored lyrics came with
// timestamps, display line i starts at lyrics_sync entry i.
int lyrics_sync_count(void);
// Index of the line being sung at elapsed_ms, or -1.
int lyrics_sync_at(int elapsed_ms);

// Line access for display (valid after a successful fetch).
int lyrics_line_count(void);
const char *lyrics_line(int i);

// Short human-readable reason for the last fetch outcome
// (server busy / not registered / network error), for the UI.
const char *lyrics_debug(void);

// Like count for the player screen (v3.16), from the Return
// YouTube Dislike API's public votes endpoint (an estimate:
// YouTube no longer publishes like counts, this service
// reconstructs them). Returns 0 and sets *likes_out on success.
int likes_fetch(const char *video_id, long *likes_out);

#endif // LYRICS_H
