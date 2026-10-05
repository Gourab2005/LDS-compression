#include "encoder.h"
#include "model.h"
#include "wire.h"
#include "miner.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct Encoder {
    SharedConfig o;
    EncOnlyConfig enc_o;
    Model* model;
    Wire* w;
    
    Tpl** mru;
    int mru_count;
    int mru_cap;
    
    void*** seeds_el;
    int* seeds_len;
    int seeds_count;
    int seeds_cap;
    
    size_t linesProcessed;
    size_t totalRawBytes;
    size_t totalCompressedBytes;
    
    size_t opData;
    size_t opDefine;
    size_t opRaw;
};

Encoder* createEncoder() {
    Encoder* enc = malloc(sizeof(Encoder));
    enc->o = SHARED;
    enc->enc_o = ENC_ONLY;
    enc->model = createModel(&enc->o, true);
    enc->w = createWire(tidBitsOf(enc->o), enc->o.maxTemplates);
    
    enc->mru_cap = enc->o.maxTemplates;
    enc->mru = malloc(enc->mru_cap * sizeof(Tpl*));
    enc->mru_count = 0;
    
    enc->seeds_cap = enc->enc_o.maxSeeds + 1;
    enc->seeds_el = malloc(enc->seeds_cap * sizeof(void**));
    enc->seeds_len = malloc(enc->seeds_cap * sizeof(int));
    enc->seeds_count = 0;
    
    enc->linesProcessed = 0;
    enc->totalRawBytes = 0;
    enc->totalCompressedBytes = 0;
    
    enc->opData = 0;
    enc->opDefine = 0;
    enc->opRaw = 0;
    
    return enc;
}

static void freeEncoderSeeds(Encoder* enc) {
    for (int i = 0; i < enc->seeds_count; i++) {
        for (int j = 0; j < enc->seeds_len[i]; j++) {
            if ((uintptr_t)enc->seeds_el[i][j] > 1) free(enc->seeds_el[i][j]);
        }
        free(enc->seeds_el[i]);
    }
    enc->seeds_count = 0;
}

void freeEncoder(Encoder* enc) {
    if (!enc) return;
    freeModel(enc->model);
    freeWire(enc->w);
    free(enc->mru);
    freeEncoderSeeds(enc);
    free(enc->seeds_el);
    free(enc->seeds_len);
    free(enc);
}

static void emitDataSlice(Encoder* enc, Tpl* tpl, StrSlice* slices) {
    Wire* w = enc->w;
    Model* m = enc->model;
    int n = tpl->n;
    TplContext* cx = tpl->cx;
    
    wireOp(w, OP_DATA);
    wireTid(w, tpl->tid);
    
    int64_t nums[64];
    int64_t* num_ptr = (n <= 64) ? nums : malloc(n * sizeof(int64_t));
    bool esc = false;
    int pm = 0;
    
    for (int i = 0; i < n; i++) {
        StrSlice sl = slices[i];
        uint8_t h = cx->hist[i];
        int hc = ((i * 2 + tpl->mode[i]) * 4 + h) * 2 + pm;
        bool hit;
        
        if (tpl->kinds[i] == 'd') {
            int64_t v = parse_int64(sl.ptr, sl.len);
            if (sl.len > 0) {
                char c0 = sl.ptr[0];
                if (c0 == '0' && sl.len > 1) {
                    if (sl.len != tpl->pads[i]) esc = true;
                } else if (c0 == '-' && v == 0) {
                    esc = true; /* -0 cannot be decoded properly as integer */
                } else if (c0 != '-' && sl.len < tpl->pads[i]) {
                    esc = true;
                }
            }
            num_ptr[i] = v;
            int64_t pred = predInt(tpl, i, num_ptr);
            hit = (v == pred);
            coderBit(&w->c, cx->pHit, hc, hit ? 0 : 1);
            
            if (!hit) {
                bool isBig = ((uint64_t)(v < 0 ? -v : v) >= BIGV);
                int d = isBig ? recentNumFind(m->numRing, (uint64_t)v) : 0;
                coderBit(&w->c, cx->pNC, i, d ? 1 : 0);
                if (d > 0) {
                    coderNum(&w->c, cx->pND[i], 0, d - 1);
                } else {
                    int64_t dl = v - tpl->prevs[i];
                    uint64_t abs_dl = (uint64_t)(dl < 0 ? -dl : dl);
                    bool direct = isBig && (abs_dl >= BIGV);
                    coderBit(&w->c, cx->pDR, i, direct ? 1 : 0);
                    if (direct) {
                        coderBit(&w->c, cx->pSN, i, v < 0 ? 1 : 0);
                        uint64_t abs_v = (uint64_t)(v < 0 ? -v : v);
                        coderBig(&w->c, cx->pMag[i], 0, abs_v - BIGV);
                    } else {
                        coderBig(&w->c, cx->pInt[i], 0, zz(dl));
                    }
                }
                if (isBig) recentNumPush(m->numRing, (uint64_t)v);
            }
            learnInt(tpl, i, v, num_ptr, hit);
        } else {
            const char* pred = (tpl->mode[i] == 1 && tpl->friend[i] >= 0) ? 
                (slices[tpl->friend[i]].ptr ? tpl->last[tpl->friend[i]] : "") : tpl->last[i];
            hit = slice_eq_str(sl, pred);
            coderBit(&w->c, cx->pHit, hc, hit ? 0 : 1);
            
            if (!hit) {
                int rank = dictRankSlice(tpl, i, sl);
                coderBit(&w->c, cx->pDict, i * 4 + h, rank >= 0 ? 1 : 0);
                if (rank >= 0) {
                    coderNum(&w->c, cx->pRank[i], 0, rank);
                    dictTouchSlice(m, tpl, i, sl);
                } else {
                    int d = (sl.len >= 4) ? recentStrFindSlice(m->strRing, sl) : 0;
                    coderBit(&w->c, cx->pSG, i, d ? 1 : 0);
                    if (d > 0) {
                        coderNum(&w->c, cx->pSD[i], 0, d - 1);
                    } else {
                        uint8_t* tmp_buf = NULL;
                        size_t tmp_len = 0;
                        encStrSlice(sl, &tmp_buf, &tmp_len, m->corpus);
                        size_t out_blob_l = 0;
                        wireBlob(w, cx->pLen[i], 0, tmp_buf, tmp_len, &out_blob_l);
                        free(tmp_buf);
                    }
                    dictAddSlice(m, tpl, i, sl);
                    if (sl.len >= 4) recentStrPushSlice(m->strRing, sl);
                }
            }
            learnStrSlice(tpl, i, sl, slices, hit);
        }
        pm = hit ? 0 : 1;
        cx->hist[i] = ((h << 1) | pm) & 3;
    }
    
    coderBit(&w->c, cx->pEsc, 0, esc ? 1 : 0);
    if (esc) {
        for (int j = 0; j < n; j++) {
            if (tpl->kinds[j] == 'd') {
                int p = padOfSlice(slices[j], tpl->pads[j]);
                tpl->pads[j] = p;
                coderNum(&w->c, cx->pPad[j], 0, p);
            }
        }
    }
    
    if (num_ptr != nums) free(num_ptr);
}

static void emitDefine(Encoder* enc, Tpl* tpl, char** strs, bool redef) {
    Wire* w = enc->w;
    Model* m = enc->model;
    
    wireOp(w, redef ? OP_REDEF : OP_SYNC);
    if (redef) {
        coderTree(&w->c, w->pRedefTid, 0, w->tidBits, tpl->tid);
    }
    w->prevTid = tpl->tid;
    coderNum(&w->c, w->pDefN, 0, tpl->n);
    
    int pk = 0;
    for (int i = 0; i < tpl->n; i++) {
        int k = (tpl->kinds[i] == 's') ? 1 : 0;
        coderBit(&w->c, w->pKind, pk, k);
        pk = k;
    }
    
    for (int i = 0; i < tpl->lits_count; i++) {
        uint8_t* tmp_buf = NULL;
        size_t tmp_len = 0;
        encStr(tpl->lits[i], &tmp_buf, &tmp_len, m->corpus);
        size_t out_l = 0;
        wireBlob(w, w->pLitLen, 0, tmp_buf, tmp_len, &out_l);
        free(tmp_buf);
    }
    
    for (int i = 0; i < tpl->n; i++) {
        const char* t = strs[i];
        if (tpl->kinds[i] == 'd') {
            int64_t v = (int64_t)atoll(t);
            int p = padOf(t, 0);
            tpl->pads[i] = p;
            coderNum(&w->c, w->pDefPad, 0, p);
            coderBig(&w->c, w->pDefInt, 0, zz(v));
            tpl->prevs[i] = v;
            if ((uint64_t)(v < 0 ? -v : v) >= BIGV) {
                recentNumPush(m->numRing, (uint64_t)v);
            }
        } else {
            uint8_t* tmp_buf = NULL;
            size_t tmp_len = 0;
            encStr(t, &tmp_buf, &tmp_len, m->corpus);
            size_t out_l = 0;
            wireBlob(w, w->pDefStrLen, 0, tmp_buf, tmp_len, &out_l);
            free(tmp_buf);
            dictAdd(m, tpl, i, t);
            free(tpl->last[i]);
            tpl->last[i] = strdup(t);
            if (strlen(t) >= 4) {
                recentStrPush(m->strRing, t);
            }
        }
    }
}

static void emitRaw(Encoder* enc, const char* line) {
    Wire* w = enc->w;
    Model* m = enc->model;
    wireOp(w, OP_RAW);
    uint8_t* tmp_buf = NULL;
    size_t tmp_len = 0;
    encStr(line, &tmp_buf, &tmp_len, m->corpus);
    size_t out_l = 0;
    wireBlob(w, w->pRawLen, 0, tmp_buf, tmp_len, &out_l);
    free(tmp_buf);
    enc->opRaw++;
}

static bool encFast(Encoder* enc, const char* line, int line_len) {
    StrSlice slices[64];
    
    // 1. First check MRU[0] (hot loop fast path)
    if (enc->mru_count > 0) {
        Tpl* tpl0 = enc->mru[0];
        StrSlice* sl_ptr = (tpl0->n <= 64) ? slices : malloc(tpl0->n * sizeof(StrSlice));
        if (matchTplSlices(tpl0, line, line_len, sl_ptr)) {
            enc->opData++;
            emitDataSlice(enc, tpl0, sl_ptr);
            if (sl_ptr != slices) free(sl_ptr);
            return true;
        }
        if (sl_ptr != slices) free(sl_ptr);
    }
    
    // 2. Prefix bucket lookup in O(1)
    int cands[32];
    int cand_count = modelLookupCandidates(enc->model, line, line_len, cands, 32);
    for (int ci = 0; ci < cand_count; ci++) {
        int tid = cands[ci];
        Tpl* tpl = (tid < enc->model->tpls_count) ? enc->model->tpls[tid] : NULL;
        if (!tpl || tpl == enc->mru[0]) continue;
        
        StrSlice* sl_ptr = (tpl->n <= 64) ? slices : malloc(tpl->n * sizeof(StrSlice));
        if (matchTplSlices(tpl, line, line_len, sl_ptr)) {
            for (int j = 0; j < enc->mru_count; j++) {
                if (enc->mru[j] == tpl) {
                    for (int k = j; k > 0; k--) enc->mru[k] = enc->mru[k - 1];
                    enc->mru[0] = tpl;
                    break;
                }
            }
            enc->opData++;
            emitDataSlice(enc, tpl, sl_ptr);
            if (sl_ptr != slices) free(sl_ptr);
            return true;
        }
        if (sl_ptr != slices) free(sl_ptr);
    }
    
    // 3. Fallback scan top MRU templates
    int lim = enc->mru_count < enc->enc_o.maxTry ? enc->mru_count : enc->enc_o.maxTry;
    for (int k = 1; k < lim; k++) {
        Tpl* tpl = enc->mru[k];
        StrSlice* sl_ptr = (tpl->n <= 64) ? slices : malloc(tpl->n * sizeof(StrSlice));
        if (matchTplSlices(tpl, line, line_len, sl_ptr)) {
            for (int j = k; j > 0; j--) {
                enc->mru[j] = enc->mru[j - 1];
            }
            enc->mru[0] = tpl;
            enc->opData++;
            emitDataSlice(enc, tpl, sl_ptr);
            if (sl_ptr != slices) free(sl_ptr);
            return true;
        }
        if (sl_ptr != slices) free(sl_ptr);
    }
    return false;
}

static void encSlow(Encoder* enc, const char* line) {
    if ((int)strlen(line) > enc->enc_o.maxLineLen) {
        emitRaw(enc, line);
        return;
    }
    
    LexResult* atoms = lexLine(line);
    
    // 1. Try extending MRU templates
    int best_k = -1;
    void** best_mel = NULL;
    int best_el_len = 0;
    double best_sim = 0.0;
    Tpl* best_tpl = NULL;
    
    int lim = enc->mru_count < enc->enc_o.maxTry ? enc->mru_count : enc->enc_o.maxTry;
    for (int k = 0; k < lim; k++) {
        Tpl* tpl = enc->mru[k];
        if (tpl->redefs >= enc->enc_o.maxRedefs || !tpl->el) continue;
        int el_len = 0;
        double sim = 0.0;
        void** mel = mergeEl(tpl->el, tpl->el_len, atoms, enc->enc_o.simExtend, &el_len, &sim);
        if (mel && (!best_mel || sim > best_sim)) {
            if (best_mel) {
                for (int j = 0; j < best_el_len; j++) if ((uintptr_t)best_mel[j] > 1) free(best_mel[j]);
                free(best_mel);
            }
            best_mel = mel;
            best_el_len = el_len;
            best_sim = sim;
            best_tpl = tpl;
            best_k = k;
        } else if (mel) {
            for (int j = 0; j < el_len; j++) if ((uintptr_t)mel[j] > 1) free(mel[j]);
            free(mel);
        }
    }
    
    if (best_mel) {
        LKResult* lk = elToLK(best_mel, best_el_len);
        int vcnt = 0;
        char** strs = matchTemplate(lk->lits, lk->lits_count, lk->kinds, line, &vcnt);
        if (strs) {
            int target_tid = best_tpl->tid;
            int new_redefs = best_tpl->redefs + 1;
            Tpl* nt = modelDefine(enc->model, target_tid, lk->lits, lk->lits_count, lk->kinds);
            nt->el = best_mel;
            nt->el_len = best_el_len;
            nt->redefs = new_redefs;
            
            for (int j = best_k; j > 0; j--) {
                enc->mru[j] = enc->mru[j - 1];
            }
            enc->mru[0] = nt;
            
            enc->opDefine++;
            emitDefine(enc, nt, strs, true);
            
            for (int v = 0; v < vcnt; v++) free(strs[v]);
            free(strs);
            freeLKResult(lk);
            freeLexResult(atoms);
            return;
        }
        for (int j = 0; j < best_el_len; j++) if ((uintptr_t)best_mel[j] > 1) free(best_mel[j]);
        free(best_mel);
        freeLKResult(lk);
    }
    
    // 2. Try seed templates
    if (enc->model->tpls_count < enc->o.maxTemplates) {
        int best_seed_k = -1;
        void** best_smel = NULL;
        int best_sel_len = 0;
        double best_ssim = 0.0;
        
        for (int k = enc->seeds_count - 1; k >= 0; k--) {
            int el_len = 0;
            double sim = 0.0;
            void** mel = mergeEl(enc->seeds_el[k], enc->seeds_len[k], atoms, enc->enc_o.simSeed, &el_len, &sim);
            if (mel && (!best_smel || sim > best_ssim)) {
                if (best_smel) {
                    for (int j = 0; j < best_sel_len; j++) if ((uintptr_t)best_smel[j] > 1) free(best_smel[j]);
                    free(best_smel);
                }
                best_smel = mel;
                best_sel_len = el_len;
                best_ssim = sim;
                best_seed_k = k;
            } else if (mel) {
                for (int j = 0; j < el_len; j++) if ((uintptr_t)mel[j] > 1) free(mel[j]);
                free(mel);
            }
        }
        
        if (best_smel) {
            LKResult* lk = elToLK(best_smel, best_sel_len);
            int vcnt = 0;
            char** strs = matchTemplate(lk->lits, lk->lits_count, lk->kinds, line, &vcnt);
            if (strs) {
                for (int j = 0; j < enc->seeds_len[best_seed_k]; j++) {
                    if ((uintptr_t)enc->seeds_el[best_seed_k][j] > 1) free(enc->seeds_el[best_seed_k][j]);
                }
                free(enc->seeds_el[best_seed_k]);
                for (int j = best_seed_k; j < enc->seeds_count - 1; j++) {
                    enc->seeds_el[j] = enc->seeds_el[j + 1];
                    enc->seeds_len[j] = enc->seeds_len[j + 1];
                }
                enc->seeds_count--;
                
                int next_tid = enc->model->tpls_count;
                Tpl* nt = modelDefine(enc->model, next_tid, lk->lits, lk->lits_count, lk->kinds);
                nt->el = best_smel;
                nt->el_len = best_sel_len;
                nt->redefs = 0;
                
                if (enc->mru_count < enc->mru_cap) {
                    for (int j = enc->mru_count; j > 0; j--) {
                        enc->mru[j] = enc->mru[j - 1];
                    }
                    enc->mru[0] = nt;
                    enc->mru_count++;
                } else {
                    for (int j = enc->mru_cap - 1; j > 0; j--) {
                        enc->mru[j] = enc->mru[j - 1];
                    }
                    enc->mru[0] = nt;
                }
                
                enc->opDefine++;
                emitDefine(enc, nt, strs, false);
                
                for (int v = 0; v < vcnt; v++) free(strs[v]);
                free(strs);
                freeLKResult(lk);
                freeLexResult(atoms);
                return;
            }
            for (int j = 0; j < best_sel_len; j++) if ((uintptr_t)best_smel[j] > 1) free(best_smel[j]);
            free(best_smel);
            freeLKResult(lk);
        }
    }
    
    // 3. Fall back: Push new seed & OP_RAW
    int seed_el_len = 0;
    void** seed_el = elFromAtoms(atoms, &seed_el_len);
    
    if (enc->seeds_count >= enc->enc_o.maxSeeds) {
        for (int j = 0; j < enc->seeds_len[0]; j++) {
            if ((uintptr_t)enc->seeds_el[0][j] > 1) free(enc->seeds_el[0][j]);
        }
        free(enc->seeds_el[0]);
        for (int j = 0; j < enc->seeds_count - 1; j++) {
            enc->seeds_el[j] = enc->seeds_el[j + 1];
            enc->seeds_len[j] = enc->seeds_len[j + 1];
        }
        enc->seeds_count--;
    }
    
    enc->seeds_el[enc->seeds_count] = seed_el;
    enc->seeds_len[enc->seeds_count] = seed_el_len;
    enc->seeds_count++;
    
    emitRaw(enc, line);
    freeLexResult(atoms);
}

uint8_t* encodeBlock(Encoder* enc, const char** lines, size_t num_lines, size_t* out_len) {
    coderStartEnc(&enc->w->c);
    
    for (size_t i = 0; i < num_lines; i++) {
        const char* line = lines[i];
        int line_len = (int)strlen(line);
        enc->totalRawBytes += (size_t)line_len;
        enc->linesProcessed++;
        
        if (!encFast(enc, line, line_len)) {
            encSlow(enc, line);
        }
    }
    
    size_t body_len = 0;
    uint8_t* body = coderFinish(&enc->w->c, &body_len);
    
    uint8_t* hdr = NULL;
    size_t hdr_len = 0, hdr_cap = 0;
    putUv(num_lines, &hdr, &hdr_len, &hdr_cap);
    
    size_t pkt_len = hdr_len + body_len;
    uint8_t* pkt = malloc(pkt_len ? pkt_len : 1);
    if (hdr_len > 0) memcpy(pkt, hdr, hdr_len);
    if (body_len > 0) memcpy(pkt + hdr_len, body, body_len);
    
    free(hdr);
    free(body);
    
    enc->totalCompressedBytes += pkt_len;
    *out_len = pkt_len;
    return pkt;
}

uint8_t* encodeLine(Encoder* enc, const char* line, size_t* out_len) {
    coderStartEnc(&enc->w->c);
    int line_len = (int)strlen(line);
    enc->totalRawBytes += (size_t)line_len;
    enc->linesProcessed++;
    
    if (!encFast(enc, line, line_len)) {
        encSlow(enc, line);
    }
    
    uint8_t* pkt = coderFinish(&enc->w->c, out_len);
    enc->totalCompressedBytes += *out_len;
    return pkt;
}

char* getMetrics(Encoder* enc) {
    char* buf = malloc(512);
    double ratio = enc->totalCompressedBytes > 0 ? (double)enc->totalRawBytes / enc->totalCompressedBytes : 0.0;
    double percentage = enc->totalRawBytes > 0 ? ((double)enc->totalCompressedBytes / enc->totalRawBytes) * 100.0 : 0.0;
    
    snprintf(buf, 512, 
        "--- Compression Results ---\n"
        "Lines processed : %zu\n"
        "Raw size        : %zu bytes\n"
        "Compressed size : %zu bytes\n"
        "Compression ratio: %.2fx smaller\n"
        "Space saved     : %.2f%%\n"
        "OP_DATA : %zu  OP_DEFINE : %zu  OP_RAW : %zu\n",
        enc->linesProcessed, enc->totalRawBytes, enc->totalCompressedBytes, ratio, 100.0 - percentage,
        enc->opData, enc->opDefine, enc->opRaw);
    return buf;
}

void printEncoderModelMemory(Encoder* enc) {
    printModelMemory(enc->model);
}
