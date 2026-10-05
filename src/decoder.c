#include "decoder.h"
#include "model.h"
#include "wire.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_DATA  0
#define OP_RAW   1
#define OP_SYNC  2
#define OP_REDEF 3

struct Decoder {
    SharedConfig o;
    Model* model;
    Wire* w;
};

Decoder* createDecoder() {
    Decoder* dec = malloc(sizeof(Decoder));
    dec->o = SHARED;
    dec->model = createModel(&dec->o, false);
    dec->w = createWire(tidBitsOf(dec->o), dec->o.maxTemplates);
    return dec;
}

void freeDecoder(Decoder* dec) {
    if (!dec) return;
    freeModel(dec->model);
    freeWire(dec->w);
    free(dec);
}

static char* decData(Decoder* dec, Tpl* tpl) {
    Wire* w = dec->w;
    Model* m = dec->model;
    int n = tpl->n;
    TplContext* cx = tpl->cx;
    
    char* strs_stack[64];
    int64_t nums_stack[64];
    bool s_alloc_stack[64];
    
    char** strs = (n <= 64) ? strs_stack : malloc(n * sizeof(char*));
    int64_t* nums = (n <= 64) ? nums_stack : malloc(n * sizeof(int64_t));
    bool* s_alloc = (n <= 64) ? s_alloc_stack : malloc(n * sizeof(bool));
    int pm = 0;
    
    for (int i = 0; i < n; i++) {
        uint8_t h = cx->hist[i];
        int hc = ((i * 2 + tpl->mode[i]) * 4 + h) * 2 + pm;
        bool hit = (coderBit(&w->c, cx->pHit, hc, 0) == 0);
        
        if (tpl->kinds[i] == 'd') {
            int64_t v;
            if (hit) {
                v = predInt(tpl, i, nums);
            } else {
                if (coderBit(&w->c, cx->pNC, i, 0)) {
                    int dist = (int)coderNum(&w->c, cx->pND[i], 0, 0) + 1;
                    v = (int64_t)recentNumAt(m->numRing, dist);
                } else if (coderBit(&w->c, cx->pDR, i, 0)) {
                    int neg = coderBit(&w->c, cx->pSN, i, 0);
                    uint64_t mag = coderBig(&w->c, cx->pMag[i], 0, 0) + BIGV;
                    v = neg ? -(int64_t)mag : (int64_t)mag;
                } else {
                    v = tpl->prevs[i] + unzz(coderBig(&w->c, cx->pInt[i], 0, 0));
                }
                if ((uint64_t)(v < 0 ? -v : v) >= BIGV) {
                    recentNumPush(m->numRing, (uint64_t)v);
                }
            }
            nums[i] = v;
            learnInt(tpl, i, v, nums, hit);
            s_alloc[i] = false;
        } else {
            char* s = NULL;
            if (hit) {
                s = (char*)predStr(tpl, i, strs);
                s_alloc[i] = false;
            } else if (coderBit(&w->c, cx->pDict, i * 4 + h, 0)) {
                int rank = (int)coderNum(&w->c, cx->pRank[i], 0, 0);
                s = strdup(dictAt(tpl, i, rank));
                dictTouch(m, tpl, i, s);
                s_alloc[i] = true;
            } else {
                if (coderBit(&w->c, cx->pSG, i, 0)) {
                    int dist = (int)coderNum(&w->c, cx->pSD[i], 0, 0) + 1;
                    s = strdup(recentStrAt(m->strRing, dist));
                } else {
                    size_t blob_len = 0;
                    uint8_t* blob = wireBlob(w, cx->pLen[i], 0, NULL, 0, &blob_len);
                    size_t pos = 0;
                    s = decStr(blob, blob_len, &pos, m->corpus);
                    free(blob);
                }
                dictAdd(m, tpl, i, s);
                if (strlen(s) >= 4) {
                    recentStrPush(m->strRing, s);
                }
                s_alloc[i] = true;
            }
            strs[i] = s;
            learnStr(tpl, i, s, strs, hit);
        }
        pm = hit ? 0 : 1;
        cx->hist[i] = ((h << 1) | pm) & 3;
    }
    
    if (coderBit(&w->c, cx->pEsc, 0, 0)) {
        for (int j = 0; j < n; j++) {
            if (tpl->kinds[j] == 'd') {
                tpl->pads[j] = (int)coderNum(&w->c, cx->pPad[j], 0, 0);
            }
        }
    }
    
    char* line = renderLine(tpl, strs, nums);
    for (int i = 0; i < n; i++) {
        if (tpl->kinds[i] == 's' && s_alloc[i]) free(strs[i]);
    }
    if (strs != strs_stack) free(strs);
    if (nums != nums_stack) free(nums);
    if (s_alloc != s_alloc_stack) free(s_alloc);
    return line;
}

static char* decDefine(Decoder* dec, int tid) {
    Wire* w = dec->w;
    Model* m = dec->model;
    
    int n = (int)coderNum(&w->c, w->pDefN, 0, 0);
    char* kinds = malloc(n + 1);
    int pk = 0;
    for (int i = 0; i < n; i++) {
        int k = coderBit(&w->c, w->pKind, pk, 0);
        kinds[i] = k ? 's' : 'd';
        pk = k;
    }
    kinds[n] = '\0';
    
    char** lits = malloc((n + 1) * sizeof(char*));
    for (int i = 0; i <= n; i++) {
        size_t blob_len = 0;
        uint8_t* blob = wireBlob(w, w->pLitLen, 0, NULL, 0, &blob_len);
        size_t pos = 0;
        lits[i] = decStr(blob, blob_len, &pos, m->corpus);
        free(blob);
    }
    
    Tpl* tpl = modelDefine(m, tid, lits, n + 1, kinds);
    for (int i = 0; i <= n; i++) free(lits[i]);
    free(lits);
    free(kinds);
    
    char** strs = malloc((n ? n : 1) * sizeof(char*));
    int64_t* nums = malloc((n ? n : 1) * sizeof(int64_t));
    
    for (int i = 0; i < n; i++) {
        if (tpl->kinds[i] == 'd') {
            int p = (int)coderNum(&w->c, w->pDefPad, 0, 0);
            int64_t v = unzz(coderBig(&w->c, w->pDefInt, 0, 0));
            tpl->pads[i] = p;
            tpl->prevs[i] = v;
            nums[i] = v;
            if ((uint64_t)(v < 0 ? -v : v) >= BIGV) {
                recentNumPush(m->numRing, (uint64_t)v);
            }
        } else {
            size_t blob_len = 0;
            uint8_t* blob = wireBlob(w, w->pDefStrLen, 0, NULL, 0, &blob_len);
            size_t pos = 0;
            char* s = decStr(blob, blob_len, &pos, m->corpus);
            free(blob);
            dictAdd(m, tpl, i, s);
            free(tpl->last[i]);
            tpl->last[i] = strdup(s);
            strs[i] = s;
            if (strlen(s) >= 4) {
                recentStrPush(m->strRing, s);
            }
        }
    }
    
    char* line = renderLine(tpl, strs, nums);
    for (int i = 0; i < n; i++) {
        if (tpl->kinds[i] == 's') free(strs[i]);
    }
    free(strs);
    free(nums);
    return line;
}

static char* decLineInternal(Decoder* dec) {
    Wire* w = dec->w;
    Model* m = dec->model;
    int op = wireOp(w, 0);
    
    if (op == OP_RAW) {
        size_t blob_len = 0;
        uint8_t* blob = wireBlob(w, w->pRawLen, 0, NULL, 0, &blob_len);
        size_t pos = 0;
        char* s = decStr(blob, blob_len, &pos, m->corpus);
        free(blob);
        return s;
    }
    
    if (op == OP_DATA) {
        int tid = wireTid(w, 0);
        Tpl* tpl = (tid < m->tpls_count) ? m->tpls[tid] : NULL;
        if (!tpl) return strdup("<<UNKNOWN_TPL>>");
        return decData(dec, tpl);
    }
    
    if (op == OP_SYNC) {
        int tid = m->tpls_count;
        w->prevTid = tid;
        return decDefine(dec, tid);
    }
    
    if (op == OP_REDEF) {
        int tid = (int)coderTree(&w->c, w->pRedefTid, 0, w->tidBits, 0);
        w->prevTid = tid;
        return decDefine(dec, tid);
    }
    
    return strdup("");
}

char** decodeBlock(Decoder* dec, const uint8_t* packet, size_t packet_len, size_t* num_lines) {
    if (packet_len == 0) {
        *num_lines = 0;
        return NULL;
    }
    
    size_t pos = 0;
    uint64_t cnt = getUv(packet, packet_len, &pos);
    
    coderStartDec(&dec->w->c, packet + pos, packet_len - pos);
    
    char** out = malloc(cnt * sizeof(char*));
    for (size_t k = 0; k < cnt; k++) {
        out[k] = decLineInternal(dec);
    }
    
    *num_lines = (size_t)cnt;
    return out;
}

char* decodeLine(Decoder* dec, const uint8_t* packet, size_t packet_len) {
    if (packet_len == 0) return strdup("");
    coderStartDec(&dec->w->c, packet, packet_len);
    return decLineInternal(dec);
}

void freeLines(char** lines, size_t num_lines) {
    if (!lines) return;
    for (size_t i = 0; i < num_lines; i++) {
        free(lines[i]);
    }
    free(lines);
}
