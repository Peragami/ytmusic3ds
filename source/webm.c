// webm.c - see webm.h for scope. EBML element ids used here:
#define ID_EBML        0x1A45DFA3u
#define ID_SEGMENT     0x18538067u
#define ID_INFO        0x1549A966u
#define ID_TSC_SCALE   0x2AD7B1u
#define ID_TRACKS      0x1654AE6Bu
#define ID_TRACKENTRY  0xAEu
#define ID_TRACKNUMBER 0xD7u
#define ID_TRACKTYPE   0x83u
#define ID_CODECID     0x86u
#define ID_CODECPRIV   0x63A2u
#define ID_AUDIO       0xE1u
#define ID_CHANNELS    0x9Fu
#define ID_CLUSTER     0x1F43B675u
#define ID_CLUSTERTS   0xE7u
#define ID_SIMPLEBLOCK 0xA3u
#define ID_BLOCKGROUP  0xA0u

#include "webm.h"

#include <string.h>

// ---------- low-level EBML readers (bounds-checked) ----------

// Read an element ID (leading-bit length marker, 1-4 bytes). Returns the id
// and advances *p past it; 0 on failure.
static uint32_t read_id(const uint8_t *d, size_t len, size_t *p) {
    if (*p >= len) return 0;
    uint8_t b = d[*p];
    if (b == 0) return 0;
    int n = 1;
    if (!(b & 0x80)) n = (b & 0x40) ? 2 : (b & 0x20) ? 3 : (b & 0x10) ? 4 : 0;
    if (n == 0 || *p + (size_t)n > len) return 0;
    uint32_t id = 0;
    for (int i = 0; i < n; i++) id = (id << 8) | d[*p + i];
    *p += n;
    return id;
}

// Read a data size vint (marker bit stripped). Sets *unknown for the
// all-ones "unknown size" encoding. Returns 0 on failure.
static int read_size(const uint8_t *d, size_t len, size_t *p, uint64_t *out,
                     int *unknown) {
    if (*p >= len) return 0;
    uint8_t b = d[*p];
    if (b == 0) return 0;
    int n = 1;
    uint8_t mask = 0x80;
    while (n <= 8 && !(b & mask)) { mask >>= 1; n++; }
    if (n > 8 || *p + (size_t)n > len) return 0;
    uint64_t v = b & (mask - 1);
    *unknown = (v == (uint64_t)(mask - 1));
    for (int i = 1; i < n; i++) {
        v = (v << 8) | d[*p + i];
        if (d[*p + i] != 0xFF) *unknown = 0;
    }
    if (*unknown) v = UINT64_MAX;
    *p += n;
    *out = v;
    return 1;
}

// Read an unsigned integer element body (1-8 bytes).
static uint64_t read_uint(const uint8_t *d, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n && i < 8; i++) v = (v << 8) | d[i];
    return v;
}

// Read a vint inside a block payload (lacing). keep_marker=0 strips the
// length bit (for sizes); returns bytes consumed or 0.
static int read_vint_raw(const uint8_t *d, size_t len, uint64_t *out,
                         int strip) {
    if (len < 1 || d[0] == 0) return 0;
    uint8_t b = d[0];
    int n = 1;
    uint8_t mask = 0x80;
    while (n <= 8 && !(b & mask)) { mask >>= 1; n++; }
    if (n > 8 || len < (size_t)n) return 0;
    uint64_t v = strip ? (b & (mask - 1)) : b;
    for (int i = 1; i < n; i++) v = (v << 8) | d[i];
    *out = v;
    return n;
}

// End offset of an element body: parent_end for unknown-size
// elements, body + sz otherwise, overflow-safe (v3.42: with a
// live window the declared sizes can legitimately point far past
// the bytes downloaded so far, and a corrupt size must not wrap).
static size_t elem_end(size_t body, uint64_t sz, int unk,
                       size_t parent_end) {
    if (unk) return parent_end;
    if (sz > (uint64_t)(SIZE_MAX - body)) return SIZE_MAX;
    return body + (size_t)sz;
}

// ---------- header parsing ----------

// Parse the children of a Tracks element; record the first A_OPUS track.
static void parse_tracks(webm_t *w, size_t start, size_t end) {
    const uint8_t *d = w->data;
    size_t p = start;
    while (p < end) {
        size_t save = p;
        uint32_t id = read_id(d, end, &p);
        uint64_t sz;
        int unk;
        if (!id || !read_size(d, end, &p, &sz, &unk)) break;
        size_t body = p;
        size_t bend = unk ? end : p + (size_t)sz;
        if (bend > end) bend = end;
        if (id == ID_TRACKENTRY) {
            int tnum = -1, ttype = -1, is_opus = 0;
            int channels = 2;
            uint16_t pre_skip = 0;
            size_t q = body;
            while (q < bend) {
                uint32_t cid = read_id(d, bend, &q);
                uint64_t csz;
                int cunk;
                if (!cid || !read_size(d, bend, &q, &csz, &cunk)) break;
                size_t cbody = q;
                size_t cend = cunk ? bend : q + (size_t)csz;
                if (cend > bend) cend = bend;
                if (cid == ID_TRACKNUMBER)
                    tnum = (int)read_uint(d + cbody, cend - cbody);
                else if (cid == ID_TRACKTYPE)
                    ttype = (int)read_uint(d + cbody, cend - cbody);
                else if (cid == ID_CODECID) {
                    if (cend - cbody == 6 &&
                        memcmp(d + cbody, "A_OPUS", 6) == 0)
                        is_opus = 1;
                } else if (cid == ID_CODECPRIV) {
                    // OpusHead: magic(8) ver(1) ch(1) pre-skip u16le(2) ...
                    if (cend - cbody >= 12 &&
                        memcmp(d + cbody, "OpusHead", 8) == 0) {
                        channels = d[cbody + 9];
                        pre_skip = (uint16_t)(d[cbody + 10] |
                                              (d[cbody + 11] << 8));
                    }
                } else if (cid == ID_AUDIO) {
                    size_t r = cbody;
                    while (r < cend) {
                        uint32_t aid = read_id(d, cend, &r);
                        uint64_t asz;
                        int aunk;
                        if (!aid || !read_size(d, cend, &r, &asz, &aunk)) break;
                        size_t abody = r;
                        size_t aend = aunk ? cend : r + (size_t)asz;
                        if (aend > cend) aend = cend;
                        if (aid == ID_CHANNELS)
                            channels = (int)read_uint(d + abody, aend - abody);
                        r = aend;
                    }
                }
                q = cend;
            }
            if (is_opus && ttype == 2 /*audio*/ && w->track_number < 0) {
                w->track_number = tnum;
                w->channels = channels > 0 ? channels : 2;
                w->pre_skip = pre_skip;
            }
        }
        p = bend;
        if (p <= save) break; // safety
    }
}

static void parse_info(webm_t *w, size_t start, size_t end) {
    const uint8_t *d = w->data;
    size_t p = start;
    while (p < end) {
        size_t save = p;
        uint32_t id = read_id(d, end, &p);
        uint64_t sz;
        int unk;
        if (!id || !read_size(d, end, &p, &sz, &unk)) break;
        size_t bend = unk ? end : p + (size_t)sz;
        if (bend > end) bend = end;
        if (id == ID_TSC_SCALE) {
            uint64_t v = read_uint(d + p, bend - p);
            if (v > 0) w->timestamp_scale = v;
        }
        p = bend;
        if (p <= save) break;
    }
}

// final != 0: the buffer is complete (avail == its full length)
// and failures are real errors, exactly the pre-v3.42 behaviour.
// final == 0 (streaming): only avail bytes are readable; running
// out of them returns 1 ("need more data") instead of an error.
static int webm_open_impl(webm_t *w, const uint8_t *data, size_t avail,
                          int final) {
    memset(w, 0, sizeof(*w));
    w->data = data;
    w->len = avail;
    w->avail = avail;
    w->final = final;
    w->timestamp_scale = 1000000;
    w->track_number = -1;
    w->channels = 2;

    size_t p = 0;
    uint32_t id = read_id(data, avail, &p);
    if (id != ID_EBML) {
        // With fewer than 4 bytes we cannot tell yet.
        if (!final && avail < 4) return 1;
        return -1;
    }
    uint64_t sz;
    int unk;
    if (!read_size(data, avail, &p, &sz, &unk))
        return final ? -1 : 1;
    p += (size_t)sz; // skip EBML header body
    if (p >= avail) return final ? -1 : 1;

    id = read_id(data, avail, &p);
    if (id != ID_SEGMENT) return final ? -1 : 1;
    if (!read_size(data, avail, &p, &sz, &unk))
        return final ? -1 : 1;
    w->pos = p;
    w->seg_end = unk ? (final ? avail : SIZE_MAX)
                     : elem_end(p, sz, 0, 0);
    if (final && w->seg_end > avail) w->seg_end = avail;

    // Walk segment-level elements until the first Cluster, collecting
    // Info/Tracks on the way.
    while (w->pos < w->seg_end) {
        if (w->pos >= avail) return final ? -1 : 1;
        size_t save = w->pos;
        size_t hdr = w->pos;
        uint32_t eid = read_id(data, avail, &w->pos);
        uint64_t esz;
        int eunk;
        if (!eid || !read_size(data, avail, &w->pos, &esz, &eunk))
            return final ? -1 : 1;
        size_t body = w->pos;
        size_t bend = elem_end(body, esz, eunk, w->seg_end);
        if (eid == ID_CLUSTER) {
            // Rewind so webm_next_packet re-reads this cluster header.
            w->pos = hdr;
            break;
        }
        if (bend > avail) {
            if (!final) return 1; // body still in flight
            bend = avail;
        }
        if (bend > w->seg_end) bend = w->seg_end;
        if (eid == ID_INFO) {
            parse_info(w, body, bend);
        } else if (eid == ID_TRACKS) {
            parse_tracks(w, body, bend);
        }
        w->pos = bend;
        if (w->pos <= save) return -1;
    }
    if (w->track_number < 0) return -2;
    return 0;
}

int webm_open(webm_t *w, const uint8_t *data, size_t len) {
    return webm_open_impl(w, data, len, 1);
}

int webm_open_live(webm_t *w, const uint8_t *data, size_t avail,
                   int final) {
    return webm_open_impl(w, data, avail, final);
}

void webm_update(webm_t *w, size_t avail, int final) {
    w->avail = avail;
    w->final = final;
}

// ---------- packet iteration ----------

// Parse a SimpleBlock body into the frame table. Returns 0 on success.
static int parse_block(webm_t *w, size_t body, size_t end, uint64_t cluster_ts) {
    const uint8_t *d = w->data;
    size_t p = body;
    uint64_t tnum;
    int n = read_vint_raw(d + p, end - p, &tnum, 1);
    if (!n) return -1;
    p += n;
    if ((int)tnum != w->track_number) return 1; // not our track: skip block
    if (end - p < 3) return -1;
    int16_t rel = (int16_t)((d[p] << 8) | d[p + 1]);
    uint8_t flags = d[p + 2];
    p += 3;
    uint64_t abs_ts = cluster_ts + (uint64_t)(rel < 0 ? 0 : rel);
    w->blk_ts_ms = (int64_t)((abs_ts * w->timestamp_scale) / 1000000u);

    int lacing = (flags >> 1) & 0x3;
    w->blk_nframes = 1;
    w->blk_index = 0;
    if (lacing == 0) {
        w->blk_frames[0] = d + p;
        w->blk_sizes[0] = end - p;
        return 0;
    }
    if (end - p < 1) return -1;
    int nframes = d[p] + 1;
    p++;
    if (nframes > 256) return -1;
    size_t sizes[256];
    if (lacing == 1) { // Xiph lacing
        size_t acc = 0;
        for (int i = 0; i < nframes - 1; i++) {
            size_t s = 0;
            while (1) {
                if (p >= end) return -1;
                s += d[p];
                if (d[p] < 255) { p++; break; }
                p++;
            }
            sizes[i] = s;
            acc += s;
        }
        if (acc > end - p) return -1;
        sizes[nframes - 1] = (end - p) - acc;
    } else if (lacing == 2) { // fixed-size lacing
        if ((end - p) % (size_t)nframes != 0) return -1;
        size_t each = (end - p) / (size_t)nframes;
        for (int i = 0; i < nframes; i++) sizes[i] = each;
    } else { // EBML lacing
        uint64_t first;
        int c = read_vint_raw(d + p, end - p, &first, 1);
        if (!c) return -1;
        p += c;
        sizes[0] = (size_t)first;
        size_t acc = (size_t)first;
        int64_t prev = (int64_t)first;
        for (int i = 1; i < nframes - 1; i++) {
            uint64_t raw;
            int cc = read_vint_raw(d + p, end - p, &raw, 0);
            if (!cc) return -1;
            p += cc;
            // signed vint: bias = 2^(7n-1) - 1
            int bits = 7 * cc - 1;
            int64_t bias = (int64_t)((1ULL << bits) - 1);
            int64_t delta = (int64_t)raw - bias;
            int64_t cur = prev + delta;
            if (cur < 0) return -1;
            sizes[i] = (size_t)cur;
            acc += (size_t)cur;
            prev = cur;
        }
        if (acc > end - p) return -1;
        sizes[nframes - 1] = (end - p) - acc;
    }
    size_t off = p;
    for (int i = 0; i < nframes; i++) {
        if (off + sizes[i] > end) return -1;
        w->blk_frames[i] = d + off;
        w->blk_sizes[i] = sizes[i];
        off += sizes[i];
    }
    w->blk_nframes = nframes;
    return 0;
}

int webm_next_packet(webm_t *w, const uint8_t **pkt, size_t *pkt_len,
                     int64_t *ts_ms) {
    const uint8_t *d = w->data;
    // Drain frames of the current block first.
    if (w->blk_index < w->blk_nframes) {
        *pkt = w->blk_frames[w->blk_index];
        *pkt_len = w->blk_sizes[w->blk_index];
        if (ts_ms) *ts_ms = w->blk_ts_ms;
        w->blk_index++;
        return 1;
    }
    while (1) {
        if (!w->in_cluster) {
            // Find the next Cluster at segment level.
            if (w->pos >= w->seg_end) return 0;
            if (w->pos >= w->avail) return w->final ? 0 : 2;
            size_t save = w->pos;
            uint32_t id = read_id(d, w->avail, &w->pos);
            uint64_t sz;
            int unk;
            if (!id || !read_size(d, w->avail, &w->pos, &sz, &unk)) {
                // A header split by the live window edge must be
                // re-read whole next time: rewind to its start.
                if (!w->final) w->pos = save;
                return w->final ? -1 : 2;
            }
            size_t bend = elem_end(w->pos, sz, unk, w->seg_end);
            if (bend > w->seg_end) bend = w->seg_end;
            if (id == ID_CLUSTER) {
                // Enter as soon as the header is readable; the
                // children are gated individually against avail.
                w->in_cluster = 1;
                w->cluster_end = bend;
                w->cluster_ts = 0;
                // w->pos already at first cluster child
                continue;
            }
            if (bend > w->avail) {
                // Element body still in flight: rewind so the
                // same element is re-read once more bytes land.
                if (!w->final) {
                    w->pos = save;
                    return 2;
                }
                bend = w->avail;
            }
            w->pos = bend;
            if (w->pos <= save) return -1;
            continue;
        }
        // Inside a cluster: read the next child element.
        if (w->pos >= w->cluster_end) {
            w->in_cluster = 0;
            w->pos = w->cluster_end;
            continue;
        }
        if (w->pos >= w->avail) {
            if (!w->final) return 2;
            // Complete buffer but the cluster claims more than
            // arrived (truncated file): end the stream cleanly.
            w->in_cluster = 0;
            w->pos = w->avail;
            continue;
        }
        size_t save = w->pos;
        size_t lim = w->cluster_end < w->avail ? w->cluster_end
                                               : w->avail;
        uint32_t id = read_id(d, lim, &w->pos);
        uint64_t sz;
        int unk;
        if (!id || !read_size(d, lim, &w->pos, &sz, &unk)) {
            if (!w->final) w->pos = save; // split header: rewind
            return w->final ? -1 : 2;
        }
        size_t body = w->pos;
        size_t bend = elem_end(body, sz, unk, w->cluster_end);
        if (bend > w->cluster_end) bend = w->cluster_end;
        if (bend > w->avail) {
            if (!w->final) {
                w->pos = save;
                return 2;
            }
            bend = w->avail;
        }
        if (id == ID_CLUSTERTS) {
            w->cluster_ts = read_uint(d + body, bend - body);
            w->pos = bend;
        } else if (id == ID_SIMPLEBLOCK) {
            w->pos = bend;
            int r = parse_block(w, body, bend, w->cluster_ts);
            if (r < 0) return -1;
            if (r == 1) continue; // other track's block
            if (w->blk_index < w->blk_nframes) {
                *pkt = w->blk_frames[w->blk_index];
                *pkt_len = w->blk_sizes[w->blk_index];
                if (ts_ms) *ts_ms = w->blk_ts_ms;
                w->blk_index++;
                return 1;
            }
        } else {
            w->pos = bend; // BlockGroup and anything else: skipped whole
            if (w->pos <= save) return -1;
        }
    }
}
