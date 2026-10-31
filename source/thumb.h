// thumb.h - video thumbnail download + JPEG decode + GPU texture.
//
// Thumbnails come from i.ytimg.com (<id>/mqdefault.jpg, 320x180
// JPEG, ~15 KB). They are decoded with libjpeg-turbo (present in the
// devkitPro portlibs), centre-cropped to a square, downscaled to
// 64x64 and uploaded as GPU_RGBA8 textures for citro2d.
//
// Every step is failure-tolerant: any network/decode/alloc failure
// leaves the slot empty and the UI draws its placeholder block
// instead. Nothing in here can abort the app (libjpeg's error_exit
// is rerouted through a setjmp guard).
#ifndef THUMB_H
#define THUMB_H

#include <citro2d.h>

#define THUMB_BANK_RESULTS 0
#define THUMB_BANK_FAVS 1
#define THUMB_BANK_NP 2 // single now-playing slot (idx always 0)

#define THUMB_MAX_RESULTS 20
#define THUMB_MAX_FAVS 40

// Free every texture in a bank (or all banks with thumbs_clear_all).
void thumbs_clear_bank(int bank);
void thumbs_clear_all(void);

// ----- background fetch jobs (v3.18) -----
// Download + decode run on a worker thread so the UI never
// blocks on thumbnails; only the GPU upload happens on the main
// thread, when it commits a finished job. One job at a time.
//
// thumb_job_start: 0 = accepted, 1 = that art is already loaded,
// -1 = busy or bad arguments, -2 = worker thread unavailable
// (the job was NOT queued; the caller should retry later).
// thumb_worker_ok: 1 once the worker thread exists.
// thumb_job_state: 0 = idle, 1 = working, 2 = finished (ready
// to peek + commit/discard).
int thumb_job_start(int bank, int idx, const char *video_id);
int thumb_worker_ok(void);
int thumb_job_state(void);
void thumb_job_peek(int *bank, int *idx, char *id_out, size_t cap);
int thumb_job_commit(void);
void thumb_job_discard(void);

// The C2D image for a slot, or NULL when no thumbnail is loaded.
C2D_Image *thumb_get(int bank, int idx);

// Native texture edge length for a bank (results/favourites: 64,
// now-playing: 128 from the larger hqdefault image). The UI needs
// it to compute draw scale factors.
int thumb_dim(int bank);

#endif // THUMB_H
