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

// Line access for display (valid after a successful fetch).
int lyrics_line_count(void);
const char *lyrics_line(int i);

// Short human-readable reason for the last fetch outcome
// (server busy / not registered / network error), for the UI.
const char *lyrics_debug(void);

#endif // LYRICS_H
