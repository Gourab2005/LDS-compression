#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void putUv(uint64_t v, uint8_t** buf, size_t* len, size_t* cap) {
    while (v >= 128) {
        if (*len >= *cap) {
            *cap = (*cap == 0) ? 32 : (*cap * 2);
            *buf = realloc(*buf, *cap);
        }
        (*buf)[(*len)++] = (uint8_t)((v & 0x7F) | 0x80);
        v >>= 7;
    }
    if (*len >= *cap) {
        *cap = (*cap == 0) ? 32 : (*cap * 2);
        *buf = realloc(*buf, *cap);
    }
    (*buf)[(*len)++] = (uint8_t)v;
}

uint64_t getUv(const uint8_t* data, size_t data_len, size_t* pos) {
    uint64_t v = 0;
    uint64_t mul = 1;
    while (*pos < data_len) {
        uint8_t b = data[(*pos)++];
        v += (uint64_t)(b & 0x7F) * mul;
        if ((b & 0x80) == 0) {
            return v;
        }
        mul <<= 7;
    }
    return 0;
}

void putZz(int64_t d, uint8_t** buf, size_t* len, size_t* cap) {
    uint64_t z = (d >= 0) ? ((uint64_t)d << 1) : (((uint64_t)(-d) << 1) - 1);
    putUv(z, buf, len, cap);
}

int64_t getZz(const uint8_t* data, size_t data_len, size_t* pos) {
    uint64_t z = getUv(data, data_len, pos);
    if ((z & 1) == 0) {
        return (int64_t)(z >> 1);
    } else {
        return -(int64_t)((z + 1) >> 1);
    }
}

int padOf(const char* t, int old_pad) {
    if (!t || t[0] == '\0') return old_pad;
    if (t[0] == '-') return old_pad;
    if (t[0] == '0' && strlen(t) > 1) return (int)strlen(t);
    return 0;
}

/* HEAD_BITS must be at least ceil(log2(win)) for good coverage.
 * We use a 2-way set-associative cache: each bucket holds 2 positions.
 * This eliminates the 'prev' chain array entirely (saves cap*4 bytes).
 * With 8192 sets × 2 ways = 16384 entries, fits a 8KB window well. */
#define CORPUS_HEAD_BITS  13
#define CORPUS_HEAD_SIZE  (1 << CORPUS_HEAD_BITS)   /* sets   */
#define CORPUS_WAYS       2                         /* ways per set */

Corpus* createCorpus(size_t win, bool index) {
    Corpus* c = malloc(sizeof(Corpus));
    c->win   = win;
    c->cap   = win * 2;
    c->buf   = malloc(c->cap);
    c->len   = 0;
    c->index = index;
    if (index) {
        /* head[set][way] = position; use flat array: CORPUS_HEAD_SIZE*CORPUS_WAYS */
        c->head = malloc(CORPUS_HEAD_SIZE * CORPUS_WAYS * sizeof(int32_t));
        for (int i = 0; i < CORPUS_HEAD_SIZE * CORPUS_WAYS; i++) c->head[i] = -1;
        c->prev = NULL;   /* no prev chain needed */
    } else {
        c->head = NULL;
        c->prev = NULL;
    }
    return c;
}

void freeCorpus(Corpus* c) {
    if (!c) return;
    free(c->buf);
    if (c->index) free(c->head);
    /* prev is always NULL now */
    free(c);
}

static inline uint32_t corpus_hash(const uint8_t* a, size_t i) {
    uint32_t v = a[i] | (a[i+1] << 8) | (a[i+2] << 16) | (a[i+3] << 24);
    return (v * 0x9E3779B1) >> (32 - CORPUS_HEAD_BITS);
}

/* LRU within a set: way-0 is oldest, way-1 is newest */
static inline void corpus_ins(Corpus* c, size_t q) {
    uint32_t s = corpus_hash(c->buf, q);
    int32_t* slot = c->head + s * CORPUS_WAYS;
    /* shift: slot[0] = slot[1]; slot[1] = new */
    slot[0] = slot[1];
    slot[1] = (int32_t)q;
}

void corpusAdd(Corpus* c, const uint8_t* b, size_t b_len) {
    if (!c || b_len == 0) return;
    if (b_len > c->win) { b += (b_len - c->win); b_len = c->win; }
    if (c->len + b_len > c->cap) {
        memmove(c->buf, c->buf + (c->len - c->win), c->win);
        c->len = c->win;
        if (c->index) {
            /* Reset all head slots */
            for (int i = 0; i < CORPUS_HEAD_SIZE * CORPUS_WAYS; i++) c->head[i] = -1;
            for (size_t q = 0; q + 4 <= c->len; q++) corpus_ins(c, q);
        }
    }
    size_t old = c->len;
    memcpy(c->buf + old, b, b_len);
    c->len += b_len;
    if (c->index) {
        size_t start = (old >= 3) ? (old - 3) : 0;
        for (size_t q = start; q + 4 <= c->len; q++) corpus_ins(c, q);
    }
}

void corpusAddString(Corpus* c, const char* s) {
    if (!c || !s) return;
    corpusAdd(c, (const uint8_t*)s, strlen(s));
}

void corpusAddSlice(Corpus* c, StrSlice sl) {
    if (!c || sl.len <= 0) return;
    corpusAdd(c, (const uint8_t*)sl.ptr, (size_t)sl.len);
}

bool corpusLongest(Corpus* c, const uint8_t* b, size_t p, size_t b_len,
                   size_t maxLen, size_t* out_len, size_t* out_pos) {
    if (!c || !c->index || b_len - p < 4) return false;
    uint32_t  s     = corpus_hash(b, p);
    int32_t*  slot  = c->head + s * CORPUS_WAYS;
    size_t    best  = 0, bpos = 0;
    size_t    lim   = (b_len - p < maxLen) ? (b_len - p) : maxLen;

    /* Check both ways (newest first) */
    for (int w = CORPUS_WAYS - 1; w >= 0; w--) {
        int32_t cur = slot[w];
        if (cur < 0 || (size_t)cur >= c->len) continue;
        size_t mk = (lim < c->len - (size_t)cur) ? lim : (c->len - (size_t)cur);
        size_t k  = 0;
        while (k < mk && c->buf[(size_t)cur + k] == b[p + k]) k++;
        if (k > best) { best = k; bpos = (size_t)cur; if (k == lim) break; }
    }
    if (best >= MINM) { *out_len = best; *out_pos = bpos; return true; }
    return false;
}

static void lzTokens(const uint8_t* b, size_t b_len, Corpus* corpus, uint8_t** out_buf, size_t* out_len, size_t* out_cap) {
    putUv(b_len, out_buf, out_len, out_cap);
    size_t p = 0, ls = 0;
    while (p + 4 <= b_len) {
        size_t m_len = 0, m_pos = 0;
        if (!corpusLongest(corpus, b, p, b_len, 4096, &m_len, &m_pos)) {
            p++;
            continue;
        }
        putUv(p - ls, out_buf, out_len, out_cap);
        for (size_t k = ls; k < p; k++) {
            if (*out_len >= *out_cap) {
                *out_cap = (*out_cap == 0) ? 64 : (*out_cap * 2);
                *out_buf = realloc(*out_buf, *out_cap);
            }
            (*out_buf)[(*out_len)++] = b[k];
        }
        putUv(m_len - MINM, out_buf, out_len, out_cap);
        putUv(corpus->len - m_pos, out_buf, out_len, out_cap);
        p += m_len;
        ls = p;
    }
    if (ls < b_len) {
        putUv(b_len - ls, out_buf, out_len, out_cap);
        for (size_t k = ls; k < b_len; k++) {
            if (*out_len >= *out_cap) {
                *out_cap = (*out_cap == 0) ? 64 : (*out_cap * 2);
                *out_buf = realloc(*out_buf, *out_cap);
            }
            (*out_buf)[(*out_len)++] = b[k];
        }
    }
}

static char* decLZ(const uint8_t* data, size_t data_len, size_t* pos, Corpus* corpus) {
    uint64_t total = getUv(data, data_len, pos);
    char* out = malloc(total + 1);
    size_t o = 0;
    while (o < total && *pos < data_len) {
        uint64_t ll = getUv(data, data_len, pos);
        if (ll > 0 && *pos + ll <= data_len && o + ll <= total) {
            memcpy(out + o, data + *pos, ll);
            *pos += ll;
            o += ll;
        }
        if (o >= total) break;
        uint64_t ml = getUv(data, data_len, pos) + MINM;
        uint64_t dist = getUv(data, data_len, pos);
        if (dist <= corpus->len && o + ml <= total) {
            size_t st = corpus->len - dist;
            memcpy(out + o, corpus->buf + st, ml);
            o += ml;
        }
    }
    out[total] = '\0';
    return out;
}

static inline bool is_ipv4_slice(StrSlice sl, uint8_t octets[4]) {
    if (sl.len < 7 || sl.len > 15) return false;
    int o[4] = {0, 0, 0, 0};
    int dots = 0;
    int cur_digits = 0;
    for (int i = 0; i < sl.len; i++) {
        if (sl.ptr[i] == '.') {
            if (cur_digits == 0 || o[dots] > 255) return false;
            dots++;
            if (dots > 3) return false;
            cur_digits = 0;
        } else if (isdigit((unsigned char)sl.ptr[i])) {
            o[dots] = o[dots] * 10 + (sl.ptr[i] - '0');
            cur_digits++;
            if (cur_digits > 3 || o[dots] > 255) return false;
        } else {
            return false;
        }
    }
    if (dots != 3 || cur_digits == 0 || o[3] > 255) return false;
    for (int i = 0; i < 4; i++) octets[i] = (uint8_t)o[i];
    return true;
}

static inline bool is_uuid_slice(StrSlice sl, bool* is_upper) {
    if (sl.len != 36) return false;
    if (sl.ptr[8] != '-' || sl.ptr[13] != '-' || sl.ptr[18] != '-' || sl.ptr[23] != '-') return false;
    bool has_upper = false, has_lower = false;
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        if (!isxdigit((unsigned char)sl.ptr[i])) return false;
        if (isupper((unsigned char)sl.ptr[i])) has_upper = true;
        if (islower((unsigned char)sl.ptr[i])) has_lower = true;
    }
    *is_upper = has_upper && !has_lower;
    return true;
}

static inline bool is_hex_slice(StrSlice sl, bool* is_upper) {
    if (sl.len < 6) return false;
    bool has_upper = false, has_lower = false;
    for (int i = 0; i < sl.len; i++) {
        if (!isxdigit((unsigned char)sl.ptr[i])) return false;
        if (isupper((unsigned char)sl.ptr[i])) has_upper = true;
        if (islower((unsigned char)sl.ptr[i])) has_lower = true;
    }
    *is_upper = has_upper && !has_lower;
    return true;
}

static inline uint8_t hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

void encStrSlice(StrSlice sl, uint8_t** out_buf, size_t* out_len, Corpus* corpus) {
    size_t cap = 64;
    *out_buf = malloc(cap);
    *out_len = 0;
    
    uint8_t octets[4];
    if (is_ipv4_slice(sl, octets)) {
        (*out_buf)[(*out_len)++] = 6;
        for (int i = 0; i < 4; i++) (*out_buf)[(*out_len)++] = octets[i];
        if (corpus) corpusAddSlice(corpus, sl);
        return;
    }
    
    bool is_upper = false;
    if (is_uuid_slice(sl, &is_upper)) {
        (*out_buf)[(*out_len)++] = is_upper ? 4 : 2;
        int hex_idx = 0;
        char hex[33];
        for (int i = 0; i < 36; i++) {
            if (sl.ptr[i] != '-') hex[hex_idx++] = sl.ptr[i];
        }
        for (int i = 0; i < 32; i += 2) {
            uint8_t b = (hex_val(hex[i]) << 4) | hex_val(hex[i + 1]);
            (*out_buf)[(*out_len)++] = b;
        }
        if (corpus) corpusAddSlice(corpus, sl);
        return;
    }
    
    if (is_hex_slice(sl, &is_upper)) {
        (*out_buf)[(*out_len)++] = is_upper ? 3 : 1;
        putUv((uint64_t)sl.len, out_buf, out_len, &cap);
        for (int i = 0; i < sl.len; i += 2) {
            uint8_t hi = hex_val(sl.ptr[i]);
            uint8_t lo = (i + 1 < sl.len) ? hex_val(sl.ptr[i + 1]) : 0;
            if (*out_len >= cap) {
                cap *= 2;
                *out_buf = realloc(*out_buf, cap);
            }
            (*out_buf)[(*out_len)++] = (hi << 4) | lo;
        }
        if (corpus) corpusAddSlice(corpus, sl);
        return;
    }
    
    size_t raw_len = (size_t)sl.len;
    bool done = false;
    if (corpus && raw_len >= 16) {
        uint8_t* lz_buf = NULL;
        size_t lz_len = 0, lz_cap = 0;
        lzTokens((const uint8_t*)sl.ptr, raw_len, corpus, &lz_buf, &lz_len, &lz_cap);
        size_t raw_cost = raw_len + (raw_len < 128 ? 1 : raw_len < 16384 ? 2 : 3);
        if (lz_len < raw_cost) {
            (*out_buf)[(*out_len)++] = 5;
            for (size_t i = 0; i < lz_len; i++) {
                if (*out_len >= cap) {
                    cap *= 2;
                    *out_buf = realloc(*out_buf, cap);
                }
                (*out_buf)[(*out_len)++] = lz_buf[i];
            }
            free(lz_buf);
            done = true;
        } else {
            free(lz_buf);
        }
    }
    
    if (!done) {
        (*out_buf)[(*out_len)++] = 0;
        putUv(raw_len, out_buf, out_len, &cap);
        for (size_t i = 0; i < raw_len; i++) {
            if (*out_len >= cap) {
                cap *= 2;
                *out_buf = realloc(*out_buf, cap);
            }
            (*out_buf)[(*out_len)++] = (uint8_t)sl.ptr[i];
        }
    }
    
    if (corpus) corpusAddSlice(corpus, sl);
}

void encStr(const char* t, uint8_t** out_buf, size_t* out_len, Corpus* corpus) {
    StrSlice sl = { .ptr = t, .len = (int)strlen(t) };
    encStrSlice(sl, out_buf, out_len, corpus);
}

char* decStr(const uint8_t* data, size_t data_len, size_t* pos, Corpus* corpus) {
    if (*pos >= data_len) return strdup("");
    uint8_t mode = data[(*pos)++];
    char* s = NULL;
    
    if (mode == 0) {
        uint64_t n = getUv(data, data_len, pos);
        s = malloc(n + 1);
        if (n > 0 && *pos + n <= data_len) {
            memcpy(s, data + *pos, n);
            *pos += n;
        }
        s[n] = '\0';
    } else if (mode == 1 || mode == 3) {
        uint64_t n = getUv(data, data_len, pos);
        size_t nb = (n + 1) >> 1;
        s = malloc(n + 1);
        const char* hex_digits = (mode == 3) ? "0123456789ABCDEF" : "0123456789abcdef";
        size_t s_idx = 0;
        for (size_t i = 0; i < nb && *pos < data_len; i++) {
            uint8_t b = data[(*pos)++];
            if (s_idx < n) s[s_idx++] = hex_digits[(b >> 4) & 0xF];
            if (s_idx < n) s[s_idx++] = hex_digits[b & 0xF];
        }
        s[n] = '\0';
    } else if (mode == 6) {
        s = malloc(16);
        if (*pos + 4 <= data_len) {
            sprintf(s, "%u.%u.%u.%u", data[*pos], data[*pos + 1], data[*pos + 2], data[*pos + 3]);
            *pos += 4;
        } else {
            strcpy(s, "0.0.0.0");
        }
    } else if (mode == 2 || mode == 4) {
        s = malloc(37);
        const char* hex_digits = (mode == 4) ? "0123456789ABCDEF" : "0123456789abcdef";
        char hex[33];
        for (int i = 0; i < 16 && *pos < data_len; i++) {
            uint8_t b = data[(*pos)++];
            hex[i * 2] = hex_digits[(b >> 4) & 0xF];
            hex[i * 2 + 1] = hex_digits[b & 0xF];
        }
        hex[32] = '\0';
        sprintf(s, "%.8s-%.4s-%.4s-%.4s-%.12s", hex, hex + 8, hex + 12, hex + 16, hex + 20);
    } else if (mode == 5) {
        s = decLZ(data, data_len, pos, corpus);
    } else {
        s = strdup("");
    }
    
    if (corpus && s) corpusAddString(corpus, s);
    return s;
}
