#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* -----------------------------------------------------------------------
 * Memory-optimized RecentNum
 * Ring: 512 slots (was 16384)
 * Index: 2048-slot open-address hash (was 65536) → 2048*16 = 32KB (was 1MB)
 * ----------------------------------------------------------------------- */
RecentNum* createRecentNum(size_t size, bool index) {
    RecentNum* r = malloc(sizeof(RecentNum));
    r->size  = size;
    r->index = index;
    r->ring  = calloc(size, sizeof(uint64_t));
    r->cnt   = 0;
    if (index) {
        r->map_cap  = 2048;          /* power-of-2, small */
        r->map_keys = calloc(r->map_cap, sizeof(uint64_t));
        r->map_vals = calloc(r->map_cap, sizeof(uint64_t));
    } else {
        r->map_cap  = 0;
        r->map_keys = NULL;
        r->map_vals = NULL;
    }
    return r;
}

void freeRecentNum(RecentNum* r) {
    if (!r) return;
    free(r->ring);
    if (r->index) { free(r->map_keys); free(r->map_vals); }
    free(r);
}

static inline size_t num_hash(uint64_t v, size_t mask) {
    return (size_t)((v * 11400714819323198485ULL) >> 32) & mask;
}

void recentNumPush(RecentNum* r, uint64_t v) {
    r->ring[r->cnt % r->size] = v;
    if (r->index) {
        size_t mask = r->map_cap - 1;
        size_t h    = num_hash(v, mask);
        for (size_t i = 0; i < 16; i++) {
            size_t idx = (h + i) & mask;
            if (r->map_vals[idx] == 0 || r->map_keys[idx] == v ||
                (r->cnt + 1 >= r->map_vals[idx] &&
                 r->cnt + 1 - r->map_vals[idx] > r->size)) {
                r->map_keys[idx] = v;
                r->map_vals[idx] = r->cnt + 1;
                break;
            }
        }
    }
    r->cnt++;
}

int recentNumFind(RecentNum* r, uint64_t v) {
    if (!r->index) return 0;
    size_t mask = r->map_cap - 1;
    size_t h    = num_hash(v, mask);
    for (size_t i = 0; i < 16; i++) {
        size_t idx = (h + i) & mask;
        if (r->map_vals[idx] == 0) break;
        if (r->map_keys[idx] == v) {
            uint64_t val = r->map_vals[idx];
            if (r->cnt >= val && r->cnt - (val - 1) <= r->size) {
                uint64_t pos = val - 1;
                if (r->ring[pos % r->size] == v)
                    return (int)(r->cnt - pos);
            }
        }
    }
    return 0;
}

uint64_t recentNumAt(RecentNum* r, int d) {
    if (d < 1 || (size_t)d > r->size || (uint64_t)d > r->cnt) return 0;
    int64_t idx = (int64_t)r->cnt - d;
    return r->ring[(size_t)(idx % (int64_t)r->size)];
}

/* -----------------------------------------------------------------------
 * Memory-optimized RecentStr
 * Ring: 512 slots  (was 16384)
 * Index: 2048-slot open-address hash (was 65536) → 2048*12 = 24KB (was ~768KB)
 * ----------------------------------------------------------------------- */
RecentStr* createRecentStr(size_t size, bool index) {
    RecentStr* r = malloc(sizeof(RecentStr));
    r->size  = size;
    r->index = index;
    r->ring  = calloc(size, sizeof(char*));
    r->cnt   = 0;
    if (index) {
        r->map_cap    = 2048;
        r->map_hashes = calloc(r->map_cap, sizeof(uint32_t));
        r->map_vals   = calloc(r->map_cap, sizeof(uint64_t));
    } else {
        r->map_cap    = 0;
        r->map_hashes = NULL;
        r->map_vals   = NULL;
    }
    return r;
}

void freeRecentStr(RecentStr* r) {
    if (!r) return;
    for (size_t i = 0; i < r->size; i++)
        if (r->ring[i]) free(r->ring[i]);
    free(r->ring);
    if (r->index) { free(r->map_hashes); free(r->map_vals); }
    free(r);
}

static inline uint32_t str_fnv32(const char* s, int len) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h;
}

void recentStrPush(RecentStr* r, const char* s) {
    size_t slot = r->cnt % r->size;
    if (r->ring[slot]) free(r->ring[slot]);
    r->ring[slot] = strdup(s);
    if (r->index) {
        uint32_t raw_h = str_fnv32(s, (int)strlen(s));
        size_t mask = r->map_cap - 1;
        size_t h    = raw_h & mask;
        for (size_t i = 0; i < 16; i++) {
            size_t idx = (h + i) & mask;
            if (r->map_vals[idx] == 0 || r->map_hashes[idx] == raw_h ||
                (r->cnt + 1 >= r->map_vals[idx] &&
                 r->cnt + 1 - r->map_vals[idx] > r->size)) {
                r->map_hashes[idx] = raw_h;
                r->map_vals[idx]   = r->cnt + 1;
                break;
            }
        }
    }
    r->cnt++;
}

void recentStrPushSlice(RecentStr* r, StrSlice sl) {
    size_t slot = r->cnt % r->size;
    if (r->ring[slot]) free(r->ring[slot]);
    r->ring[slot] = strdup_slice(sl);
    if (r->index) {
        uint32_t raw_h = str_fnv32(sl.ptr, sl.len);
        size_t mask = r->map_cap - 1;
        size_t h    = raw_h & mask;
        for (size_t i = 0; i < 16; i++) {
            size_t idx = (h + i) & mask;
            if (r->map_vals[idx] == 0 || r->map_hashes[idx] == raw_h ||
                (r->cnt + 1 >= r->map_vals[idx] &&
                 r->cnt + 1 - r->map_vals[idx] > r->size)) {
                r->map_hashes[idx] = raw_h;
                r->map_vals[idx]   = r->cnt + 1;
                break;
            }
        }
    }
    r->cnt++;
}

int recentStrFind(RecentStr* r, const char* s) {
    if (!r->index) return 0;
    uint32_t raw_h = str_fnv32(s, (int)strlen(s));
    size_t mask = r->map_cap - 1;
    size_t h    = raw_h & mask;
    for (size_t i = 0; i < 16; i++) {
        size_t idx = (h + i) & mask;
        if (r->map_vals[idx] == 0) break;
        if (r->map_hashes[idx] == raw_h) {
            uint64_t val = r->map_vals[idx];
            if (r->cnt >= val && r->cnt - (val - 1) <= r->size) {
                uint64_t pos = val - 1;
                char* rs = r->ring[pos % r->size];
                if (rs && strcmp(rs, s) == 0)
                    return (int)(r->cnt - pos);
            }
        }
    }
    return 0;
}

int recentStrFindSlice(RecentStr* r, StrSlice sl) {
    if (!r->index) return 0;
    uint32_t raw_h = str_fnv32(sl.ptr, sl.len);
    size_t mask = r->map_cap - 1;
    size_t h    = raw_h & mask;
    for (size_t i = 0; i < 16; i++) {
        size_t idx = (h + i) & mask;
        if (r->map_vals[idx] == 0) break;
        if (r->map_hashes[idx] == raw_h) {
            uint64_t val = r->map_vals[idx];
            if (r->cnt >= val && r->cnt - (val - 1) <= r->size) {
                uint64_t pos = val - 1;
                char* rs = r->ring[pos % r->size];
                if (rs && (int)strlen(rs) == sl.len &&
                    memcmp(rs, sl.ptr, sl.len) == 0)
                    return (int)(r->cnt - pos);
            }
        }
    }
    return 0;
}

const char* recentStrAt(RecentStr* r, int d) {
    if (d < 1 || (size_t)d > r->size || (uint64_t)d > r->cnt) return "";
    int64_t idx = (int64_t)r->cnt - d;
    char* s = r->ring[(size_t)(idx % (int64_t)r->size)];
    return s ? s : "";
}

/* -----------------------------------------------------------------------
 * Tpl & Model
 * ----------------------------------------------------------------------- */
void freeTpl(Tpl* t) {
    if (!t) return;
    for (int i = 0; i < t->lits_count; i++) free(t->lits[i]);
    free(t->lits);
    free(t->lit_lens);
    free(t->kinds);
    free(t->prevs);
    free(t->strides);
    free(t->cands);
    free(t->pads);
    for (int i = 0; i < t->n; i++) free(t->last[i]);
    free(t->last);
    free(t->mode);
    free(t->friend);
    for (int i = 0; i < t->n; i++) {
        if (t->sl[i]) {
            for (int j = 0; j < t->sl_counts[i]; j++) free(t->sl[i][j]);
            free(t->sl[i]);
        }
    }
    free(t->sl);
    free(t->sl_counts);
    free(t->sl_caps);
    if (t->el) {
        for (int i = 0; i < t->el_len; i++)
            if ((uintptr_t)t->el[i] > 1) free(t->el[i]);
        free(t->el);
    }
    if (t->cx) freeTplContext(t->cx);
    free(t);
}

Model* createModel(SharedConfig* o, bool index) {
    Model* m = malloc(sizeof(Model));
    m->maxDictBytes  = o ? o->maxDictBytes  : 30000;
    m->maxEntryBytes = o ? o->maxEntryBytes : 64;
    m->corpusWin     = o ? o->corpusWin     : 16384;
    m->index         = index;

    m->tpls_cap   = o ? o->maxTemplates : 256;
    m->tpls_count = 0;
    m->tpls       = calloc(m->tpls_cap, sizeof(Tpl*));

    m->dictBytes = 0;
    m->corpus    = createCorpus(m->corpusWin, index);

    /* Small rings: 512 entries each (was 16384) */
    m->numRing = createRecentNum(512, index);
    m->strRing = createRecentStr(512, index);

    m->lru_head = NULL;
    m->lru_tail = NULL;

    /* Smaller prefix table: 256 buckets (was 1024) */
    m->prefix_index  = calloc(TPL_HASH_BUCKETS, sizeof(TplBucket));
    m->wildcard_cap  = 32;
    m->wildcard_count = 0;
    m->wildcard_tids  = malloc(m->wildcard_cap * sizeof(int));

    return m;
}

void freeModel(Model* m) {
    if (!m) return;
    for (int i = 0; i < m->tpls_count; i++)
        if (m->tpls[i]) freeTpl(m->tpls[i]);
    free(m->tpls);
    freeCorpus(m->corpus);
    freeRecentNum(m->numRing);
    freeRecentStr(m->strRing);

    DictEntry* cur = m->lru_head;
    while (cur) {
        DictEntry* nxt = cur->next;
        free(cur->s);
        free(cur);
        cur = nxt;
    }
    free(m->prefix_index);
    free(m->wildcard_tids);
    free(m);
}

void printModelMemory(Model* m) {
    /* Corpus: buf(cap) + 2-way head table (CORPUS_HEAD_SIZE*2*4), no prev */
    size_t corpus_buf  = m->corpus->cap;
    /* head = 8192 sets * 2 ways * 4 bytes = 65536 bytes for 8KB window */
    size_t corpus_head = m->corpus->index
        ? (8192 * 2 * sizeof(int32_t)) : 0;
    size_t corpus_bytes = corpus_buf + corpus_head;

    /* Rings: ring arrays + hash maps */
    size_t num_ring_bytes = m->numRing->size * sizeof(uint64_t)
        + (m->numRing->index ? m->numRing->map_cap * (sizeof(uint64_t)*2) : 0);
    size_t str_ring_bytes = m->strRing->size * sizeof(char*)
        + (m->strRing->index
           ? m->strRing->map_cap * (sizeof(uint32_t) + sizeof(uint64_t)) : 0);
    size_t ring_bytes = num_ring_bytes + str_ring_bytes;

    size_t tpl_bytes  = m->tpls_cap * sizeof(Tpl*);
    size_t dict_bytes = (size_t)m->dictBytes;
    size_t total      = corpus_bytes + ring_bytes + tpl_bytes + dict_bytes;

    printf("--- Algorithm Memory Breakdown ---\n");
    printf("  LZ Corpus buffer : %6.2f KB\n", corpus_bytes / 1024.0);
    printf("  Ring buffers     : %6.2f KB\n", ring_bytes   / 1024.0);
    printf("  Template slots   : %6.2f KB\n", tpl_bytes    / 1024.0);
    printf("  String dictionary: %6.2f KB\n", dict_bytes   / 1024.0);
    printf("  Total (algo only): %6.2f KB\n", total        / 1024.0);
}

/* -----------------------------------------------------------------------
 * Prefix index: smaller hash table, same bucket struct
 * ----------------------------------------------------------------------- */
static inline uint32_t prefix_hash_str(const char* s, int len) {
    uint32_t h = 2166136261u;
    int lim = len < 8 ? len : 8;
    for (int i = 0; i < lim; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h & (TPL_HASH_BUCKETS - 1);
}

Tpl* modelDefine(Model* m, int tid, char** lits, int lits_count, const char* kinds) {
    if (tid >= m->tpls_cap) {
        int new_cap = m->tpls_cap * 2;
        if (tid >= new_cap) new_cap = tid + 16;
        m->tpls = realloc(m->tpls, new_cap * sizeof(Tpl*));
        for (int i = m->tpls_cap; i < new_cap; i++) m->tpls[i] = NULL;
        m->tpls_cap = new_cap;
    }

    if (tid < m->tpls_count && m->tpls[tid]) {
        /* Evict dict entries for this tid */
        DictEntry* cur = m->lru_head;
        while (cur) {
            DictEntry* nxt = cur->next;
            if (cur->tid == tid) {
                if (cur->prev) cur->prev->next = cur->next;
                else m->lru_head = cur->next;
                if (cur->next) cur->next->prev = cur->prev;
                else m->lru_tail = cur->prev;
                m->dictBytes -= (int)cur->b;
                free(cur->s);
                free(cur);
            }
            cur = nxt;
        }
        freeTpl(m->tpls[tid]);
        m->tpls[tid] = NULL;
    }
    if (tid >= m->tpls_count) m->tpls_count = tid + 1;

    Tpl* t   = malloc(sizeof(Tpl));
    t->tid   = tid;
    t->n     = kinds ? (int)strlen(kinds) : 0;
    t->nb    = (t->n + 7) >> 3;
    t->lits_count = lits_count;
    t->lits       = malloc(lits_count * sizeof(char*));
    t->lit_lens   = malloc(lits_count * sizeof(int));
    for (int i = 0; i < lits_count; i++) {
        t->lits[i]    = strdup(lits[i]);
        t->lit_lens[i] = (int)strlen(lits[i]);
    }
    t->first_lit_len = t->lit_lens[0];
    t->last_lit_len  = (t->n < lits_count) ? t->lit_lens[t->n] : 0;

    t->kinds   = strdup(kinds ? kinds : "");
    t->prevs   = calloc(t->n ? t->n : 1, sizeof(int64_t));
    t->strides = calloc(t->n ? t->n : 1, sizeof(int64_t));
    t->cands   = calloc(t->n ? t->n : 1, sizeof(int64_t));
    t->pads    = calloc(t->n ? t->n : 1, sizeof(int));

    t->last = malloc((t->n ? t->n : 1) * sizeof(char*));
    for (int i = 0; i < t->n; i++) t->last[i] = strdup("");

    t->mode   = calloc(t->n ? t->n : 1, sizeof(int));
    t->friend = malloc((t->n ? t->n : 1) * sizeof(int));
    for (int i = 0; i < t->n; i++) t->friend[i] = -1;

    /* Per-slot dictionaries: start with cap=8 (was 16) */
    t->sl        = calloc(t->n ? t->n : 1, sizeof(char**));
    t->sl_counts = calloc(t->n ? t->n : 1, sizeof(int));
    t->sl_caps   = calloc(t->n ? t->n : 1, sizeof(int));
    for (int i = 0; i < t->n; i++) {
        if (t->kinds[i] == 's') {
            t->sl_caps[i]  = 8;
            t->sl[i]       = malloc(t->sl_caps[i] * sizeof(char*));
            t->sl_counts[i] = 0;
        }
    }

    t->el     = NULL;
    t->el_len = 0;
    t->redefs = 0;
    t->cx     = createTplContext(t->n);

    m->tpls[tid] = t;

    /* Register in prefix index */
    if (lits_count > 0 && strlen(lits[0]) > 0) {
        uint32_t b  = prefix_hash_str(lits[0], (int)strlen(lits[0]));
        TplBucket* buck = &m->prefix_index[b];
        bool found = false;
        for (int k = 0; k < buck->count; k++)
            if (buck->tids[k] == tid) { found = true; break; }
        if (!found && buck->count < TPL_BUCKET_CAP)
            buck->tids[buck->count++] = tid;
    } else {
        bool found = false;
        for (int k = 0; k < m->wildcard_count; k++)
            if (m->wildcard_tids[k] == tid) { found = true; break; }
        if (!found) {
            if (m->wildcard_count >= m->wildcard_cap) {
                m->wildcard_cap *= 2;
                m->wildcard_tids = realloc(m->wildcard_tids, m->wildcard_cap * sizeof(int));
            }
            m->wildcard_tids[m->wildcard_count++] = tid;
        }
    }
    return t;
}

int modelLookupCandidates(Model* m, const char* line, int line_len,
                           int* out_tids, int max_cands) {
    int count = 0;
    uint32_t b = prefix_hash_str(line, line_len);
    TplBucket* buck = &m->prefix_index[b];
    for (int k = 0; k < buck->count && count < max_cands; k++)
        out_tids[count++] = buck->tids[k];
    for (int k = 0; k < m->wildcard_count && count < max_cands; k++)
        out_tids[count++] = m->wildcard_tids[k];
    return count;
}

bool matchTplSlices(const Tpl* tpl, const char* line, int line_len,
                    StrSlice* out_slices) {
    int n = tpl->n;
    if (n == 0)
        return (line_len == tpl->first_lit_len &&
                memcmp(line, tpl->lits[0], line_len) == 0);

    int first_len   = tpl->first_lit_len;
    int lastLit_len = tpl->last_lit_len;

    if (line_len < first_len + lastLit_len) return false;
    if (first_len > 0 && memcmp(line, tpl->lits[0], first_len) != 0)
        return false;
    if (lastLit_len > 0 &&
        memcmp(line + line_len - lastLit_len, tpl->lits[n], lastLit_len) != 0)
        return false;

    int end = line_len - lastLit_len;
    int pos = first_len;

    for (int i = 0; i < n; i++) {
        int e;
        if (i == n - 1) {
            e = end;
        } else {
            const char* nx     = tpl->lits[i + 1];
            int         nx_len = tpl->lit_lens[i + 1];
            const char* found  = NULL;
            if (nx_len == 1) {
                found = (const char*)memchr(line + pos, nx[0], end - pos);
            } else {
                found = strstr(line + pos, nx);
                if (found && (found - line + nx_len > end)) found = NULL;
            }
            if (!found) return false;
            e = (int)(found - line);
        }
        if (e < pos) return false;
        int v_len = e - pos;
        if (tpl->kinds[i] == 'd') {
            if (v_len == 0 || v_len > 18) return false;
            const char* sp = line + pos;
            if (sp[0] == '-') {
                if (v_len == 1) return false;
                bool all_zero = true;
                for (int si = 1; si < v_len; si++) {
                    if (!isdigit((unsigned char)sp[si])) return false;
                    if (sp[si] != '0') all_zero = false;
                }
                if (all_zero) return false; /* -0 cannot be integer 'd' */
            } else {
                for (int si = 0; si < v_len; si++)
                    if (!isdigit((unsigned char)sp[si])) return false;
            }
        }
        out_slices[i].ptr = line + pos;
        out_slices[i].len = v_len;
        if (i < n - 1) pos = e + tpl->lit_lens[i + 1];
    }
    return true;
}

bool matchTemplateSlices(char** lits, int lits_count, const char* kinds,
                         const char* line, int line_len, StrSlice* out_slices) {
    int n = kinds ? (int)strlen(kinds) : 0;
    if (n == 0)
        return (line_len == (int)strlen(lits[0]) &&
                memcmp(line, lits[0], line_len) == 0);

    const char* first       = lits[0];
    const char* lastLit     = lits[n];
    int         first_len   = (int)strlen(first);
    int         lastLit_len = (int)strlen(lastLit);

    if (line_len < first_len + lastLit_len) return false;
    if (first_len > 0 && memcmp(line, first, first_len) != 0) return false;
    if (lastLit_len > 0 &&
        memcmp(line + line_len - lastLit_len, lastLit, lastLit_len) != 0)
        return false;

    int end = line_len - lastLit_len;
    int pos = first_len;

    for (int i = 0; i < n; i++) {
        int e;
        if (i == n - 1) {
            e = end;
        } else {
            const char* nx     = lits[i + 1];
            int         nx_len = (int)strlen(nx);
            const char* found  = NULL;
            if (nx_len == 1) {
                found = (const char*)memchr(line + pos, nx[0], end - pos);
            } else {
                found = strstr(line + pos, nx);
                if (found && (found - line + nx_len > end)) found = NULL;
            }
            if (!found) return false;
            e = (int)(found - line);
        }
        if (e < pos) return false;
        int v_len = e - pos;
        if (kinds[i] == 'd') {
            if (v_len == 0 || v_len > 18) return false;
            const char* sp = line + pos;
            if (sp[0] == '-') {
                if (v_len == 1) return false;
                bool all_zero = true;
                for (int si = 1; si < v_len; si++) {
                    if (!isdigit((unsigned char)sp[si])) return false;
                    if (sp[si] != '0') all_zero = false;
                }
                if (all_zero) return false; /* -0 cannot be integer 'd' */
            } else {
                for (int si = 0; si < v_len; si++)
                    if (!isdigit((unsigned char)sp[si])) return false;
            }
        }
        out_slices[i].ptr = line + pos;
        out_slices[i].len = v_len;
        if (i < n - 1) pos = e + (int)strlen(lits[i + 1]);
    }
    return true;
}

char** matchTemplate(char** lits, int lits_count, const char* kinds,
                     const char* line, int* out_vals_count) {
    int n        = kinds ? (int)strlen(kinds) : 0;
    int line_len = (int)strlen(line);

    StrSlice  slices[64];
    StrSlice* sl_ptr = (n <= 64) ? slices : malloc(n * sizeof(StrSlice));

    if (!matchTemplateSlices(lits, lits_count, kinds, line, line_len, sl_ptr)) {
        if (sl_ptr != slices) free(sl_ptr);
        return NULL;
    }

    char** vals = malloc((n ? n : 1) * sizeof(char*));
    for (int i = 0; i < n; i++) vals[i] = strdup_slice(sl_ptr[i]);
    if (sl_ptr != slices) free(sl_ptr);
    *out_vals_count = n;
    return vals;
}

static inline int fast_i64_to_str(int64_t v, int pad, char* buf) {
    if (pad > 0 && v >= 0) {
        char fmt[32];
        sprintf(fmt, "%%0%dlld", pad);
        return sprintf(buf, fmt, (long long)v);
    }
    char tmp[32];
    int  len = 0;
    bool neg = false;
    uint64_t u;
    if (v < 0) { neg = true; u = (uint64_t)-v; } else { u = (uint64_t)v; }
    if (u == 0) {
        tmp[len++] = '0';
    } else {
        while (u > 0) { tmp[len++] = (char)('0' + (u % 10)); u /= 10; }
    }
    int pos = 0;
    if (neg) buf[pos++] = '-';
    for (int k = len - 1; k >= 0; k--) buf[pos++] = tmp[k];
    buf[pos] = '\0';
    return pos;
}

char* renderLine(Tpl* tpl, char** strs, int64_t* nums) {
    int    n     = tpl->n;
    char   num_bufs_stack[64][36];
    int    num_lens_stack[64];
    
    char (*num_bufs)[36] = (n <= 64) ? num_bufs_stack : malloc(n * sizeof(*num_bufs));
    int* num_lens        = (n <= 64) ? num_lens_stack : malloc(n * sizeof(int));
    size_t total_len = (size_t)tpl->lit_lens[0];

    for (int i = 0; i < n; i++) {
        if (tpl->kinds[i] == 'd') {
            num_lens[i]  = fast_i64_to_str(nums[i], tpl->pads[i], num_bufs[i]);
            total_len   += (size_t)num_lens[i];
        } else {
            if (strs && strs[i]) total_len += strlen(strs[i]);
        }
        total_len += (size_t)tpl->lit_lens[i + 1];
    }

    char* out = malloc(total_len + 1);
    char* p   = out;
    int   l0  = tpl->lit_lens[0];
    if (l0 > 0) { memcpy(p, tpl->lits[0], l0); p += l0; }

    for (int i = 0; i < n; i++) {
        if (tpl->kinds[i] == 'd') {
            memcpy(p, num_bufs[i], num_lens[i]);
            p += num_lens[i];
        } else {
            if (strs && strs[i]) {
                size_t sl = strlen(strs[i]);
                memcpy(p, strs[i], sl);
                p += sl;
            }
        }
        int ln = tpl->lit_lens[i + 1];
        if (ln > 0) { memcpy(p, tpl->lits[i + 1], ln); p += ln; }
    }
    *p = '\0';

    if (num_bufs != num_bufs_stack) free(num_bufs);
    if (num_lens != num_lens_stack) free(num_lens);

    return out;
}

int64_t predInt(Tpl* t, int i, int64_t* nums) {
    return (t->mode[i] == 1 && t->friend[i] >= 0)
        ? nums[t->friend[i]]
        : (t->prevs[i] + t->strides[i]);
}

void learnInt(Tpl* t, int i, int64_t v, int64_t* nums, bool hit) {
    int64_t pv = t->prevs[i];
    if (!hit) {
        if (v == pv + t->strides[i]) {
            t->mode[i] = 0;
        } else {
            for (int j = i - 1; j >= 0; j--) {
                if (t->kinds[j] == 'd' && nums[j] == v) {
                    t->friend[i] = j;
                    t->mode[i]   = 1;
                    break;
                }
            }
        }
        int64_t d = v - pv;
        if (d == t->cands[i]) t->strides[i] = d;
        else t->cands[i] = d;
    }
    t->prevs[i] = v;
}

const char* predStr(Tpl* t, int i, char** strs) {
    return (t->mode[i] == 1 && t->friend[i] >= 0) ? strs[t->friend[i]] : t->last[i];
}

void learnStr(Tpl* t, int i, const char* s, char** strs, bool hit) {
    if (!hit) {
        if (strcmp(t->last[i], s) == 0) {
            t->mode[i] = 0;
        } else if (strlen(s) > 0) {
            for (int j = i - 1; j >= 0; j--) {
                if (t->kinds[j] == 's' && strcmp(strs[j], s) == 0) {
                    t->friend[i] = j; t->mode[i] = 1; break;
                }
            }
        }
        free(t->last[i]);
        t->last[i] = strdup(s);
    } else {
        if (t->mode[i] == 1 && t->friend[i] >= 0) {
            if (strcmp(t->last[i], s) != 0) {
                free(t->last[i]); t->last[i] = strdup(s);
            }
        }
    }
}

void learnStrSlice(Tpl* t, int i, StrSlice sl, StrSlice* slices, bool hit) {
    if (!hit) {
        if (slice_eq_str(sl, t->last[i])) {
            t->mode[i] = 0;
        } else if (sl.len > 0) {
            for (int j = i - 1; j >= 0; j--) {
                if (t->kinds[j] == 's' && slice_eq(slices[j], sl)) {
                    t->friend[i] = j; t->mode[i] = 1; break;
                }
            }
        }
        free(t->last[i]);
        t->last[i] = strdup_slice(sl);
    } else {
        if (t->mode[i] == 1 && t->friend[i] >= 0) {
            if (!slice_eq_str(sl, t->last[i])) {
                free(t->last[i]); t->last[i] = strdup_slice(sl);
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * Per-slot dictionary — small linear scan (max 8 slots per variable)
 * ----------------------------------------------------------------------- */
int dictRank(Tpl* t, int i, const char* s) {
    if (!t->sl[i]) return -1;
    for (int p = t->sl_counts[i] - 1; p >= 0; p--)
        if (strcmp(t->sl[i][p], s) == 0)
            return t->sl_counts[i] - 1 - p;
    return -1;
}

int dictRankSlice(Tpl* t, int i, StrSlice sl) {
    if (!t->sl[i]) return -1;
    for (int p = t->sl_counts[i] - 1; p >= 0; p--) {
        char* e = t->sl[i][p];
        if ((int)strlen(e) == sl.len && memcmp(e, sl.ptr, sl.len) == 0)
            return t->sl_counts[i] - 1 - p;
    }
    return -1;
}

const char* dictAt(Tpl* t, int i, int rank) {
    if (!t->sl[i]) return "";
    int p = t->sl_counts[i] - 1 - rank;
    if (p < 0 || p >= t->sl_counts[i]) return "";
    return t->sl[i][p];
}

static void lru_move_to_tail(Model* m, DictEntry* cur) {
    if (cur == m->lru_tail) return;
    if (cur->prev) cur->prev->next = cur->next;
    else m->lru_head = cur->next;
    if (cur->next) cur->next->prev = cur->prev;
    cur->prev = m->lru_tail;
    cur->next = NULL;
    if (m->lru_tail) m->lru_tail->next = cur;
    m->lru_tail = cur;
}

static DictEntry* lru_find(Model* m, int tid, int i, const char* s, int len) {
    for (DictEntry* c = m->lru_tail; c; c = c->prev)
        if (c->tid == tid && c->i == i &&
            (int)c->b == len && memcmp(c->s, s, len) == 0)
            return c;
    return NULL;
}

/* Inline slot bubble-up */
static void slot_move_to_top(Tpl* t, int i, int p) {
    if (p < t->sl_counts[i] - 1) {
        char* tmp = t->sl[i][p];
        for (int k = p; k < t->sl_counts[i] - 1; k++)
            t->sl[i][k] = t->sl[i][k + 1];
        t->sl[i][t->sl_counts[i] - 1] = tmp;
    }
}

void dictTouch(Model* m, Tpl* t, int i, const char* s) {
    if (!t->sl[i]) return;
    for (int k = t->sl_counts[i] - 1; k >= 0; k--) {
        if (strcmp(t->sl[i][k], s) == 0) { slot_move_to_top(t, i, k); break; }
    }
    DictEntry* e = lru_find(m, t->tid, i, s, (int)strlen(s));
    if (e) lru_move_to_tail(m, e);
}

void dictTouchSlice(Model* m, Tpl* t, int i, StrSlice sl) {
    if (!t->sl[i]) return;
    for (int k = t->sl_counts[i] - 1; k >= 0; k--) {
        char* e = t->sl[i][k];
        if ((int)strlen(e) == sl.len && memcmp(e, sl.ptr, sl.len) == 0) {
            slot_move_to_top(t, i, k); break;
        }
    }
    DictEntry* e = lru_find(m, t->tid, i, sl.ptr, sl.len);
    if (e) lru_move_to_tail(m, e);
}

static void evict_one(Model* m) {
    DictEntry* ev = m->lru_head;
    if (!ev) return;
    m->lru_head = ev->next;
    if (m->lru_head) m->lru_head->prev = NULL;
    else m->lru_tail = NULL;

    Tpl* ev_tpl = (ev->tid < m->tpls_count) ? m->tpls[ev->tid] : NULL;
    if (ev_tpl && ev_tpl->sl[ev->i]) {
        for (int k = 0; k < ev_tpl->sl_counts[ev->i]; k++) {
            if (strcmp(ev_tpl->sl[ev->i][k], ev->s) == 0) {
                free(ev_tpl->sl[ev->i][k]);
                for (int j = k; j < ev_tpl->sl_counts[ev->i] - 1; j++)
                    ev_tpl->sl[ev->i][j] = ev_tpl->sl[ev->i][j + 1];
                ev_tpl->sl_counts[ev->i]--;
                break;
            }
        }
    }
    m->dictBytes -= (int)ev->b;
    free(ev->s);
    free(ev);
}

void dictAdd(Model* m, Tpl* t, int i, const char* s) {
    if (!t->sl[i]) return;
    int b = (int)strlen(s);
    if (b > m->maxEntryBytes || b > m->maxDictBytes) return;

    if (dictRank(t, i, s) >= 0) { dictTouch(m, t, i, s); return; }

    while (m->dictBytes + b > m->maxDictBytes && m->lru_head) evict_one(m);

    if (t->sl_counts[i] >= t->sl_caps[i]) {
        t->sl_caps[i] *= 2;
        t->sl[i] = realloc(t->sl[i], t->sl_caps[i] * sizeof(char*));
    }
    t->sl[i][t->sl_counts[i]++] = strdup(s);

    DictEntry* entry = malloc(sizeof(DictEntry));
    entry->tid  = t->tid; entry->i = i;
    entry->s    = strdup(s); entry->b = (size_t)b;
    entry->prev = m->lru_tail; entry->next = NULL;
    if (m->lru_tail) m->lru_tail->next = entry;
    else m->lru_head = entry;
    m->lru_tail = entry;
    m->dictBytes += b;
}

void dictAddSlice(Model* m, Tpl* t, int i, StrSlice sl) {
    if (!t->sl[i]) return;
    int b = sl.len;
    if (b > m->maxEntryBytes || b > m->maxDictBytes) return;

    if (dictRankSlice(t, i, sl) >= 0) { dictTouchSlice(m, t, i, sl); return; }

    while (m->dictBytes + b > m->maxDictBytes && m->lru_head) evict_one(m);

    if (t->sl_counts[i] >= t->sl_caps[i]) {
        t->sl_caps[i] *= 2;
        t->sl[i] = realloc(t->sl[i], t->sl_caps[i] * sizeof(char*));
    }
    t->sl[i][t->sl_counts[i]++] = strdup_slice(sl);

    DictEntry* entry = malloc(sizeof(DictEntry));
    entry->tid  = t->tid; entry->i = i;
    entry->s    = strdup_slice(sl); entry->b = (size_t)b;
    entry->prev = m->lru_tail; entry->next = NULL;
    if (m->lru_tail) m->lru_tail->next = entry;
    else m->lru_head = entry;
    m->lru_tail = entry;
    m->dictBytes += b;
}
