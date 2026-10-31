// player.h - stream download + WebM/Opus decode + ndsp output.
#ifndef PLAYER_H
#define PLAYER_H

#include <stddef.h>

enum {
    PHASE_NONE = 0,
    PHASE_DOWNLOAD = 1,
    PHASE_PLAY = 2,
    PHASE_DONE = 3,
};

// Which stage failed, for the UI's per-attempt error log (v3.3).
enum {
    FAILSTAGE_NONE = 0,
    FAILSTAGE_RESOLVE = 1,
    FAILSTAGE_DOWNLOAD = 2,
    FAILSTAGE_MEDIA = 3, // demux / decode / ndsp, after a full download
};

typedef struct {
    volatile int stop_requested; // set by the UI hook (B button)
    volatile int exit_requested; // set by the UI hook when apt says exit
    // v3.4: set by the UI hook together with stop_requested when the
    // user picks a different song during playback/download (A button);
    // the UI layer then re-runs the play flow for the new selection
    // instead of treating it as a plain stop.
    volatile int switch_requested;
    // v3.6 player screen controls:
    volatile int pause_flip; // hook requests a pause/resume toggle
    volatile int skip_dir;   // hook requests skip: +1 next / -1 prev
                             // (set together with stop_requested)
    int paused;              // maintained by the play loop for the UI
    int phase;
    size_t dl_bytes;
    size_t dl_total; // 0 when the server did not send a length
    unsigned elapsed_sec;
    unsigned total_sec;
    char message[160];
    // Failure diagnostics (v3.3): filled in when a play attempt fails,
    // so one photo of the error screen identifies the stage, how far
    // the download got, and the last HTTP status (0 = transport-level
    // failure, see fail_curl for the CURLcode).
    int fail_stage;     // FAILSTAGE_*
    long fail_http;     // last HTTP status seen at the failure
    int fail_curl;      // CURLcode of the failing transfer (0 = none)
    size_t fail_offset; // bytes successfully downloaded before failure
} play_ctl_t;

// Called periodically during download and playback so the UI can redraw
// and poll input. The hook may set ctl->stop_requested / exit_requested.
typedef void (*play_hook_t)(void *user);

// Download the stream at url into RAM, demux it as WebM, decode Opus and
// play through ndsp channel 0.
// Returns 0 = played to the end, 1 = stopped by user, -1 = error
// (ctl->message describes it).
int play_stream_url(const char *url, unsigned total_sec_hint,
                    play_ctl_t *ctl, play_hook_t hook, void *user);

// Returned when the download stage itself failed (as opposed to a
// demux/decode/ndsp error), so the caller can fall back to another
// route/instance. Produced by play_stream_url_ex() and
// play_stream_url_inv() whenever they were given a video_id (i.e. a
// re-resolution source). ctl->fail_* describes the failure.
#define PLAY_DL_FAIL (-3)

// Download engine (v3.3, all routes): HTTP Range chunks of 256 KiB are
// fetched over ONE curl easy handle that is reused for the whole
// download (HTTP/1.1, no per-chunk reconnect), so libcurl reuses the
// underlying connection - the same shape as FourthTube's persistent
// session downloader. If a chunk is cut (HTTP 403 etc.), the stream
// URL is re-issued and the download resumes from the current offset
// (bounded retries); partial bytes already in the buffer stay valid.
//
// Direct-route variant: re-issues via resolve_stream_direct().
// With video_id == NULL this is exactly play_stream_url() (no
// re-issue source; a download failure then returns -1).
int play_stream_url_ex(const char *url, unsigned total_sec_hint,
                       play_ctl_t *ctl, play_hook_t hook, void *user,
                       const char *video_id);

// Invidious-route variant: re-issues via
// resolve_stream_invidious_at(instance_idx, ...) so a resumed download
// stays on the same instance the URL came from.
int play_stream_url_inv(const char *url, unsigned total_sec_hint,
                        play_ctl_t *ctl, play_hook_t hook, void *user,
                        const char *video_id, int instance_idx);

#endif // PLAYER_H
