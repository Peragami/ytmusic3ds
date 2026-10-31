// webm.h - minimal WebM/EBML demuxer for A_OPUS audio streams.
//
// Scope (deliberately small, matched to YouTube's audio-only WebM output):
//   - Parses EBML header, Segment, Info (TimestampScale), Tracks (finds the
//     A_OPUS track, reads channels + OpusHead pre-skip), and Clusters.
//   - Iterates SimpleBlocks (BlockGroups are skipped) and yields raw Opus
//     packets, handling all three lacing modes (none / Xiph / fixed / EBML).
//   - Works on a fully-downloaded in-memory buffer (no I/O, no allocation
//     except a small internal frame table).
// This is NOT a general Matroska parser: unknown elements are skipped by
// size, cues/seekheads are ignored, and only the first Opus audio track is
// surfaced.

#ifndef WEBM_H
#define WEBM_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t len;

    // filled by webm_open()
    uint64_t timestamp_scale; // nanoseconds per timestamp unit (default 1000000)
    int track_number;         // Opus audio track number, -1 if none
    int channels;             // from OpusHead (1 or 2)
    uint16_t pre_skip;        // samples at 48 kHz to discard at stream start

    // iteration state (internal)
    size_t pos;        // cursor of next element to read (segment scope)
    size_t seg_end;    // end of segment content
    size_t cluster_end;
    uint64_t cluster_ts; // cluster timestamp, in timestamp units
    int in_cluster;
    // current SimpleBlock lacing state
    const uint8_t *blk_frames[256];
    size_t blk_sizes[256];
    int blk_nframes;
    int blk_index;
    int64_t blk_ts_ms;
} webm_t;

// Parse headers up to the first Cluster. Returns 0 on success,
// -1 if the buffer is not WebM, -2 if no Opus audio track was found.
int webm_open(webm_t *w, const uint8_t *data, size_t len);

// Fetch the next Opus packet. On success returns 1 and sets *pkt/*pkt_len
// (pointing into the original buffer) and *ts_ms (block timestamp,
// milliseconds; may be -1 when unknown). Returns 0 at end of stream,
// -1 on a parse error.
int webm_next_packet(webm_t *w, const uint8_t **pkt, size_t *pkt_len,
                     int64_t *ts_ms);

#endif // WEBM_H
