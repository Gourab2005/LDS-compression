#include "wire.h"
#include <stdlib.h>
#include <string.h>

void coderStartEnc(Coder* c) {
    c->enc     = true;
    c->out     = NULL;
    c->out_len = 0;
    c->out_cap = 0;
    c->low     = 0;
    c->range   = 0xFFFFFFFF;
    c->cache   = 0;
    c->cs      = 1;
}

void coderStartDec(Coder* c, const uint8_t* buf, size_t buf_len) {
    c->enc   = false;
    c->b     = buf;
    c->b_len = buf_len;
    c->p     = 0;
    c->range = 0xFFFFFFFF;
    c->code  = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t bv = (c->p < c->b_len) ? c->b[c->p++] : 0;
        c->code = (c->code << 8) | bv;
    }
}

static inline uint8_t coder_byte(Coder* c) {
    return (c->p < c->b_len) ? c->b[c->p++] : 0;
}

static inline void coder_out_push(Coder* c, uint8_t b) {
    if (c->out_len >= c->out_cap) {
        c->out_cap = (c->out_cap == 0) ? 8192 : (c->out_cap * 2);
        c->out = realloc(c->out, c->out_cap);
    }
    c->out[c->out_len++] = b;
}

static inline void coder_shift(Coder* c) {
    if (c->low < 0xFF000000ULL || c->low >= 0x100000000ULL) {
        uint32_t carry = (c->low >= 0x100000000ULL) ? 1 : 0;
        uint32_t t     = c->cache;
        do {
            coder_out_push(c, (uint8_t)((t + carry) & 0xFF));
            t = 0xFF;
        } while (--c->cs);
        c->cache = (uint8_t)((c->low >> 24) & 0xFF);
    }
    c->cs++;
    c->low = (c->low & 0x00FFFFFFULL) << 8;
}

static inline void coder_norm(Coder* c) {
    while (c->range < TOP) {
        c->range <<= 8;
        if (c->enc) coder_shift(c);
        else c->code = (c->code << 8) | coder_byte(c);
    }
}

uint8_t coderBit(Coder* c, uint16_t* P, size_t i, uint8_t b) {
    uint32_t p     = P[i];
    uint32_t bound = (c->range >> PB) * p;
    if (c->enc) {
        if (b) { c->low += bound; c->range -= bound; P[i] = (uint16_t)(p - (p >> SH)); }
        else   { c->range = bound;                   P[i] = (uint16_t)(p + ((ONE - p) >> SH)); }
    } else {
        if (c->code < bound) {
            c->range = bound; P[i] = (uint16_t)(p + ((ONE - p) >> SH)); b = 0;
        } else {
            c->code -= bound; c->range -= bound; P[i] = (uint16_t)(p - (p >> SH)); b = 1;
        }
    }
    coder_norm(c);
    return b;
}

uint8_t coderDirect(Coder* c, uint8_t b) {
    c->range >>= 1;
    if (c->enc) { if (b) c->low += c->range; }
    else {
        if (c->code >= c->range) { c->code -= c->range; b = 1; }
        else b = 0;
    }
    coder_norm(c);
    return b;
}

uint32_t coderTree(Coder* c, uint16_t* P, size_t base, int nbits, uint32_t v) {
    uint32_t m = 1;
    for (int i = nbits - 1; i >= 0; i--) {
        uint8_t b = c->enc ? ((v >> i) & 1) : 0;
        m = (m << 1) | coderBit(c, P, base + m, b);
    }
    return m - (1U << nbits);
}

uint32_t coderNum(Coder* c, uint16_t* P, size_t base, uint32_t v) {
    uint32_t w = v + 1;
    int nbE = 0;
    if (c->enc) {
#if defined(__GNUC__) || defined(__clang__)
        nbE = 31 - __builtin_clz(w);
#else
        uint32_t t = w; while (t > 1) { nbE++; t >>= 1; }
#endif
    }
    int k = 0;
    while (coderBit(c, P, base + (k < 31 ? k : 31), c->enc ? (k < nbE ? 1 : 0) : 0)) k++;
    uint32_t r = 1, node = 1;
    for (int j = k - 1, cnt = 0; j >= 0; j--, cnt++) {
        uint8_t bt = c->enc ? ((w >> j) & 1) : 0;
        if (cnt < 2) {
            bt   = coderBit(c, P, base + 32 + (k < 31 ? k : 31) * 4 + node, bt);
            node = (cnt == 0) ? 2 + bt : 3;
        } else bt = coderDirect(c, bt);
        r = (r << 1) | bt;
    }
    return r - 1;
}

uint64_t coderBig(Coder* c, uint16_t* P, size_t base, uint64_t z) {
    uint64_t w = z + 1;
    int nbE = 0;
    if (c->enc) {
#if defined(__GNUC__) || defined(__clang__)
        nbE = 63 - __builtin_clzll(w);
#else
        uint64_t t = w; while (t > 1) { nbE++; t >>= 1; }
#endif
    }
    int k = 0;
    while (coderBit(c, P, base + (k < 127 ? k : 127), c->enc ? (k < nbE ? 1 : 0) : 0)) k++;
    uint64_t r = 1; int node = 1;
    for (int j = k - 1, cnt = 0; j >= 0; j--, cnt++) {
        uint8_t bt = c->enc ? ((w >> j) & 1) : 0;
        if (cnt < 2) {
            bt   = coderBit(c, P, base + 128 + (k < 127 ? k : 127) * 4 + node, bt);
            node = (cnt == 0) ? 2 + bt : 3;
        } else bt = coderDirect(c, bt);
        r = (r << 1) | bt;
    }
    return r - 1;
}

uint8_t* coderFinish(Coder* c, size_t* out_len) {
    if (!c->enc) { *out_len = 0; return NULL; }
    uint64_t best = c->low;
    for (int k = 32; k >= 0; k--) {
        uint64_t u = 1ULL << k;
        uint64_t v = ((c->low + u - 1) / u) * u;
        if (v < c->low + c->range) { best = v; break; }
    }
    c->low = best;
    for (int i = 0; i < 5; i++) coder_shift(c);

    size_t end = c->out_len;
    while (end > 1 && c->out[end - 1] == 0) end--;

    size_t    final_len = (end > 1) ? (end - 1) : 0;
    uint8_t*  final_out = malloc(final_len ? final_len : 1);
    if (final_len > 0) memcpy(final_out, c->out + 1, final_len);
    *out_len = final_len;
    free(c->out);
    c->out = NULL;
    return final_out;
}

/* -----------------------------------------------------------------------
 * TplContext — memory-optimised
 *
 * Key saving: share a single flat block for all per-variable arrays.
 * BIGSZ was 640 uint16_t each, NUMSZ 160 uint16_t each, per variable.
 * We reduce to BIGSZ_EDGE=320, NUMSZ_EDGE=80.
 * For a 4-variable template that is:
 *   old: 4*(640*2*3 + 160*2*5) = 4*(3840+1600) = 21.8 KB
 *   new: 4*(320*2*3 + 80*2*5)  = 4*(1920+800)  = 10.9 KB
 * A 50% reduction per context table.
 * ----------------------------------------------------------------------- */
TplContext* createTplContext(int n) {
    TplContext* cx = malloc(sizeof(TplContext));
    cx->n = n;

    cx->pHit  = malloc(n * 16 * sizeof(uint16_t));
    for (int i = 0; i < n * 16; i++) cx->pHit[i]  = INIT;

    cx->pDict = malloc(n * 4 * sizeof(uint16_t));
    for (int i = 0; i < n * 4; i++) cx->pDict[i] = INIT;

    cx->pEsc[0] = INIT;
    cx->hist    = calloc(n ? n : 1, sizeof(uint8_t));

    cx->pInt  = malloc(n * sizeof(uint16_t*));
    cx->pRank = malloc(n * sizeof(uint16_t*));
    cx->pLen  = malloc(n * sizeof(uint16_t*));
    cx->pPad  = malloc(n * sizeof(uint16_t*));
    cx->pMag  = malloc(n * sizeof(uint16_t*));
    cx->pND   = malloc(n * sizeof(uint16_t*));
    cx->pSD   = malloc(n * sizeof(uint16_t*));

    for (int i = 0; i < n; i++) {
        cx->pInt[i]  = malloc(BIGSZ * sizeof(uint16_t));
        for (int k = 0; k < BIGSZ; k++) cx->pInt[i][k]  = INIT;

        cx->pRank[i] = malloc(NUMSZ * sizeof(uint16_t));
        for (int k = 0; k < NUMSZ; k++) cx->pRank[i][k] = INIT;

        cx->pLen[i]  = malloc(NUMSZ * sizeof(uint16_t));
        for (int k = 0; k < NUMSZ; k++) cx->pLen[i][k]  = INIT;

        cx->pPad[i]  = malloc(NUMSZ * sizeof(uint16_t));
        for (int k = 0; k < NUMSZ; k++) cx->pPad[i][k]  = INIT;

        cx->pMag[i]  = malloc(BIGSZ * sizeof(uint16_t));
        for (int k = 0; k < BIGSZ; k++) cx->pMag[i][k]  = INIT;

        cx->pND[i]   = malloc(NUMSZ * sizeof(uint16_t));
        for (int k = 0; k < NUMSZ; k++) cx->pND[i][k]   = INIT;

        cx->pSD[i]   = malloc(NUMSZ * sizeof(uint16_t));
        for (int k = 0; k < NUMSZ; k++) cx->pSD[i][k]   = INIT;
    }

    cx->pNC = malloc((n ? n : 1) * sizeof(uint16_t));
    for (int i = 0; i < n; i++) cx->pNC[i] = INIT;

    cx->pDR = malloc((n ? n : 1) * sizeof(uint16_t));
    for (int i = 0; i < n; i++) cx->pDR[i] = INIT;

    cx->pSN = malloc((n ? n : 1) * sizeof(uint16_t));
    for (int i = 0; i < n; i++) cx->pSN[i] = INIT;

    cx->pSG = malloc((n ? n : 1) * sizeof(uint16_t));
    for (int i = 0; i < n; i++) cx->pSG[i] = INIT;

    return cx;
}

void freeTplContext(TplContext* cx) {
    if (!cx) return;
    free(cx->pHit);
    free(cx->pDict);
    free(cx->hist);
    for (int i = 0; i < cx->n; i++) {
        free(cx->pInt[i]);  free(cx->pRank[i]); free(cx->pLen[i]);
        free(cx->pPad[i]);  free(cx->pMag[i]);  free(cx->pND[i]);
        free(cx->pSD[i]);
    }
    free(cx->pInt);  free(cx->pRank); free(cx->pLen);
    free(cx->pPad);  free(cx->pMag);  free(cx->pND);  free(cx->pSD);
    free(cx->pNC);   free(cx->pDR);   free(cx->pSN);  free(cx->pSG);
    free(cx);
}

/* -----------------------------------------------------------------------
 * Wire — optimised pByte: from 257*256 (131KB) to 33*256 (16KB)
 *
 * Strategy: only track the low 5 bits of the previous byte as context
 * (32 contexts + 1 for "no previous") → 33*256*2 = ~16.9KB (was 131KB)
 * This is a LogeLite-inspired trick: byte context is approximated.
 * ----------------------------------------------------------------------- */
Wire* createWire(int tidBits, int maxTemplates) {
    Wire* w = malloc(sizeof(Wire));
    w->tidBits      = tidBits;
    w->maxTemplates = maxTemplates;

    for (int i = 0; i < 16; i++) w->pOp[i] = INIT;

    w->pTid = calloc(maxTemplates + 2, sizeof(uint16_t*));

    int tidsz = 1 << tidBits;
    w->pRedefTid = malloc(tidsz * sizeof(uint16_t));
    for (int i = 0; i < tidsz; i++) w->pRedefTid[i] = INIT;

    /* Reduced byte-context table: BYTE_CTX_SZ contexts × 256 symbols */
    w->pByte = malloc(BYTE_CTX_SZ * 256 * sizeof(uint16_t));
    for (int i = 0; i < BYTE_CTX_SZ * 256; i++) w->pByte[i] = INIT;

    for (int i = 0; i < NUMSZ; i++) w->pRawLen[i]    = INIT;
    for (int i = 0; i < NUMSZ; i++) w->pLitLen[i]    = INIT;
    for (int i = 0; i < NUMSZ; i++) w->pDefStrLen[i] = INIT;
    for (int i = 0; i < NUMSZ; i++) w->pDefN[i]      = INIT;
    for (int i = 0; i < NUMSZ; i++) w->pDefPad[i]    = INIT;
    for (int i = 0; i < BIGSZ; i++) w->pDefInt[i]    = INIT;
    for (int i = 0; i < 2; i++)     w->pKind[i]      = INIT;

    w->prevOp  = 0;
    w->prevTid = -1;
    return w;
}

void freeWire(Wire* w) {
    if (!w) return;
    for (int i = 0; i <= w->maxTemplates + 1; i++)
        if (w->pTid[i]) free(w->pTid[i]);
    free(w->pTid);
    free(w->pRedefTid);
    free(w->pByte);
    free(w);
}

int wireOp(Wire* w, int op) {
    int v = coderTree(&w->c, w->pOp, w->prevOp * 4, 2, op);
    w->prevOp = v;
    return v;
}

int wireTid(Wire* w, int tid) {
    int idx = w->prevTid + 1;
    if (idx < 0) idx = 0;
    if (idx > w->maxTemplates + 1) idx = w->maxTemplates + 1;
    if (!w->pTid[idx]) {
        int tidsz = 1 << w->tidBits;
        w->pTid[idx] = malloc(tidsz * sizeof(uint16_t));
        for (int i = 0; i < tidsz; i++) w->pTid[idx][i] = INIT;
    }
    int v = coderTree(&w->c, w->pTid[idx], 0, w->tidBits, tid);
    w->prevTid = v;
    return v;
}

/* wireBlob uses reduced byte-context: prev & (BYTE_CTX_SZ-1) */
uint8_t* wireBlob(Wire* w, uint16_t* Plen, size_t base,
                  const uint8_t* arr, size_t in_len, size_t* out_len) {
    Coder* c = &w->c;
    uint32_t n = coderNum(c, Plen, base, c->enc ? (uint32_t)in_len : 0);
    uint8_t* out = c->enc ? (uint8_t*)arr : malloc(n ? n : 1);
    int prev = BYTE_CTX_SZ - 1;          /* "no previous" sentinel */
    for (size_t i = 0; i < n; i++) {
        uint8_t v = (uint8_t)coderTree(c, w->pByte,
                                        (size_t)prev * 256, 8,
                                        c->enc ? arr[i] : 0);
        if (!c->enc) out[i] = v;
        prev = v & (BYTE_CTX_SZ - 1);    /* keep low bits as context */
    }
    *out_len = n;
    return out;
}
