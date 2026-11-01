// net.h - HTTP + YouTube Music search + stream resolution for ytmusic3ds.
#ifndef NET_H
#define NET_H

#include <stddef.h>

#define MAX_RESULTS 20
#define TITLE_LEN 192
#define SUB_LEN 160
#define VID_LEN 16

typedef struct {
    char title[TITLE_LEN];
    char sub[SUB_LEN]; // artist / album / duration line when available
    char id[VID_LEN];  // videoId
} result_t;

extern result_t g_results[MAX_RESULTS];
extern int g_result_count;

// Search YouTube Music (InnerTube /search, WEB_REMIX context, song filter).
// Fills g_results/g_result_count. Returns 0 on success, -1 on failure
// (human-readable reason is printed to the currently selected console).
int yt_search(const char *query);
// Home recommendations (v3.35): InnerTube /browse with the
// FEmusic_home feed (FEmusic_charts as fallback), same anonymous
// WEB_REMIX context as search. Fills g_results/g_result_count
// with playable songs. Returns 0 on success, -1 on failure.
int yt_home(void);
// UI pump hook (v3.29): while any net.c request is in flight
// this is called repeatedly; it should keep input and drawing
// alive and return 1 to abort the request. Registered by
// main.c; NULL (the default) keeps the old blocking behaviour.
void net_set_pump(int (*fn)(void));

// Resolve a playable audio stream URL for video_id.
//
// Primary route: direct InnerTube /player on m.youtube.com. Two client
// contexts are tried, both with a fresh visitorData and
// signatureTimestamp "0" so the response carries plain `url`s (no
// signatureCipher):
//   1. VISIONOS (FourthTube's alternative shape) - v3.3's primary.
//      Its URLs are NOT length-limited: full-song downloads verified
//      from the build VM (see report.md section 7).
//   2. The v3 "recipe" (ANDROID context + iPhone device fields,
//      FourthTube's default). Its URLs turned out to be capped to a
//      ~512 KiB prefix window (the vprv preview token): the v3.2
//      hardware test and every VM probe were cut at exactly 512 KiB
//      no matter the downloader, so this is a last resort only.
// Fallback: public Invidious instances (see below).
// Either way an Opus/WebM format is picked (the only YouTube audio
// format the 3DS portlibs can decode: libopus is present, no AAC
// decoder exists). Preference order: itag 250 (~80 kbps) -> 249
// (~64 kbps) -> 251 (~160 kbps), favouring low bandwidth/memory.
//
// On success returns 0 and fills url_out, *itag_out, *length_sec_out,
// and sets g_route to the route that succeeded. Returns -1 when both
// routes failed (g_route is then ROUTE_NONE).
int resolve_stream(const char *video_id, char *url_out, size_t url_cap,
                   int *itag_out, long *length_sec_out);

// Sound-quality preference (v3.5 settings screen): changes the
// itag preference order used by both resolvers.
//   0 = low      (249 -> 250 -> 251, ~64 kbps first)
//   1 = standard (250 -> 249 -> 251, ~80 kbps first) [default]
//   2 = high     (251 -> 250 -> 249, ~160 kbps first)
extern int g_quality;

// Which resolution route produced the current stream URL.
#define ROUTE_NONE 0
#define ROUTE_DIRECT 1    // direct InnerTube /player
#define ROUTE_INVIDIOUS 2 // public Invidious instance API
extern int g_route;

// For ROUTE_DIRECT: which /player client context produced the URL.
// 1 = VISIONOS (primary), 2 = recipe ANDROID+iPhone (last resort).
extern int g_direct_ctx;

// The direct route on its own. Also used by the player to re-issue a
// fresh URL when a download is cut mid-way.
int resolve_stream_direct(const char *video_id, char *url_out,
                          size_t url_cap, int *itag_out,
                          long *length_sec_out);

// Built-in Invidious instance list, in preference order. The UI walks
// it one instance at a time so each failure is visible on screen.
int invidious_instance_count(void);
const char *invidious_instance_host(int idx);

// Invidious route via one specific instance. http_out (may be NULL)
// receives the API HTTP status, or 0 on transport failure, so the UI
// can show why an instance was skipped.
int resolve_stream_invidious_at(int idx, const char *video_id,
                                char *url_out, size_t url_cap,
                                int *itag_out, long *length_sec_out,
                                long *http_out);

// For ROUTE_INVIDIOUS: index of the instance that produced the URL
// (-1 = none).
extern int g_route_instance;

// Invidious route via the first working instance in the list.
int resolve_stream_invidious(const char *video_id, char *url_out,
                             size_t url_cap, int *itag_out,
                             long *length_sec_out);

#endif // NET_H
