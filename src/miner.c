#include "miner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static inline bool is_uuid_token(const char* s, int len) {
    if (len != 36) return false;
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') return false;
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

static inline bool is_mac_token(const char* s, int len) {
    if (len != 17) return false;
    char sep = s[2];
    if (sep != ':' && sep != '-') return false;
    for (int i = 0; i < 17; i++) {
        if (i % 3 == 2) {
            if (s[i] != sep) return false;
        } else {
            if (!isxdigit((unsigned char)s[i])) return false;
        }
    }
    return true;
}

static inline bool is_hex_token(const char* s, int len) {
    if (len < 6) return false;
    for (int i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

LexResult* lexLine(const char* line) {
    LexResult* r = malloc(sizeof(LexResult));
    r->capacity = 128;
    r->txt = malloc(r->capacity * sizeof(char*));
    r->flag = malloc(r->capacity * sizeof(int));
    r->length = 0;
    r->nplain = 0;
    
    int i = 0;
    int len = (int)strlen(line);
    
    while (i < len) {
        // 1. Check UUID
        if (i + 36 <= len && is_uuid_token(line + i, 36)) {
            char* tok = malloc(37);
            memcpy(tok, line + i, 36);
            tok[36] = '\0';
            i += 36;
            if (r->length >= r->capacity) {
                r->capacity *= 2;
                r->txt = realloc(r->txt, r->capacity * sizeof(char*));
                r->flag = realloc(r->flag, r->capacity * sizeof(int));
            }
            r->txt[r->length] = tok;
            r->flag[r->length] = 2; // SS
            r->length++;
            continue;
        }
        
        // 2. Check MAC
        if (i + 17 <= len && is_mac_token(line + i, 17)) {
            char* tok = malloc(18);
            memcpy(tok, line + i, 17);
            tok[17] = '\0';
            i += 17;
            if (r->length >= r->capacity) {
                r->capacity *= 2;
                r->txt = realloc(r->txt, r->capacity * sizeof(char*));
                r->flag = realloc(r->flag, r->capacity * sizeof(int));
            }
            r->txt[r->length] = tok;
            r->flag[r->length] = 2; // SS
            r->length++;
            continue;
        }
        
        // 3. Check IPv4
        if (isdigit((unsigned char)line[i])) {
            int p = i;
            int dots = 0;
            int octet_val = 0;
            int octet_digits = 0;
            bool valid_ip = true;
            while (p < len && (isdigit((unsigned char)line[p]) || line[p] == '.')) {
                if (line[p] == '.') {
                    if (octet_digits == 0 || octet_val > 255) { valid_ip = false; break; }
                    dots++;
                    octet_digits = 0;
                    octet_val = 0;
                } else {
                    octet_digits++;
                    octet_val = octet_val * 10 + (line[p] - '0');
                    if (octet_digits > 3 || octet_val > 255) { valid_ip = false; break; }
                }
                p++;
            }
            if (valid_ip && dots == 3 && octet_digits > 0 && octet_val <= 255 &&
                (p == len || !isalnum((unsigned char)line[p]))) {
                int tok_len = p - i;
                char* tok = malloc(tok_len + 1);
                memcpy(tok, line + i, tok_len);
                tok[tok_len] = '\0';
                i = p;
                if (r->length >= r->capacity) {
                    r->capacity *= 2;
                    r->txt = realloc(r->txt, r->capacity * sizeof(char*));
                    r->flag = realloc(r->flag, r->capacity * sizeof(int));
                }
                r->txt[r->length] = tok;
                r->flag[r->length] = 2; // SS
                r->length++;
                continue;
            }
        }
        
        // 4. Check negative or positive integer
        if (isdigit((unsigned char)line[i]) || (line[i] == '-' && i + 1 < len && isdigit((unsigned char)line[i+1]) && (i == 0 || !isalnum((unsigned char)line[i-1])))) {
            int start = i;
            if (line[i] == '-') i++;
            while (i < len && isdigit((unsigned char)line[i])) i++;
            // Check if it continues into alphanumeric (hex or hash)
            if (i < len && isalpha((unsigned char)line[i])) {
                while (i < len && isalnum((unsigned char)line[i])) i++;
                int tok_len = i - start;
                char* tok = malloc(tok_len + 1);
                memcpy(tok, line + start, tok_len);
                tok[tok_len] = '\0';
                if (r->length >= r->capacity) {
                    r->capacity *= 2;
                    r->txt = realloc(r->txt, r->capacity * sizeof(char*));
                    r->flag = realloc(r->flag, r->capacity * sizeof(int));
                }
                r->txt[r->length] = tok;
                r->flag[r->length] = (tok_len >= 6) ? 2 : 0;
                if (r->flag[r->length] == 0) r->nplain++;
                r->length++;
            } else {
                int tok_len = i - start;
                char* tok = malloc(tok_len + 1);
                memcpy(tok, line + start, tok_len);
                tok[tok_len] = '\0';
                if (r->length >= r->capacity) {
                    r->capacity *= 2;
                    r->txt = realloc(r->txt, r->capacity * sizeof(char*));
                    r->flag = realloc(r->flag, r->capacity * sizeof(int));
                }
                r->txt[r->length] = tok;
                /* -0 cannot be represented in int64 without losing negative sign; treat as string */
                bool is_neg_zero = (tok[0] == '-' && tok_len > 1 && tok[1] == '0');
                if (is_neg_zero || tok_len > 18) {
                    r->flag[r->length] = 2; // SS
                } else {
                    r->flag[r->length] = 1; // SD (integer)
                }
                if (r->flag[r->length] == 2) r->nplain++;
                r->length++;
            }
            continue;
        }
        
        // 5. Check words / alphanumerics
        if (isalpha((unsigned char)line[i])) {
            int start = i;
            bool has_digit = false;
            while (i < len && isalnum((unsigned char)line[i])) {
                if (isdigit((unsigned char)line[i])) has_digit = true;
                i++;
            }
            int tok_len = i - start;
            char* tok = malloc(tok_len + 1);
            memcpy(tok, line + start, tok_len);
            tok[tok_len] = '\0';
            
            int f = 0;
            if ((has_digit && tok_len >= 10) || (!has_digit && tok_len >= 6 && is_hex_token(tok, tok_len))) {
                f = 2; // long hash
            }
            
            if (r->length >= r->capacity) {
                r->capacity *= 2;
                r->txt = realloc(r->txt, r->capacity * sizeof(char*));
                r->flag = realloc(r->flag, r->capacity * sizeof(int));
            }
            r->txt[r->length] = tok;
            r->flag[r->length] = f;
            if (f == 0) r->nplain++;
            r->length++;
            continue;
        }
        
        // 6. Single character (punctuation, whitespace, symbol)
        char* tok = malloc(2);
        tok[0] = line[i++];
        tok[1] = '\0';
        if (r->length >= r->capacity) {
            r->capacity *= 2;
            r->txt = realloc(r->txt, r->capacity * sizeof(char*));
            r->flag = realloc(r->flag, r->capacity * sizeof(int));
        }
        r->txt[r->length] = tok;
        r->flag[r->length] = 0;
        r->nplain++;
        r->length++;
    }
    
    return r;
}

void freeLexResult(LexResult* r) {
    if (!r) return;
    for (int i = 0; i < r->length; i++) {
        free(r->txt[i]);
    }
    free(r->txt);
    free(r->flag);
    free(r);
}

void** elFromAtoms(LexResult* a, int* out_len) {
    void** el = malloc((a->length ? a->length : 1) * sizeof(void*));
    int len = 0;
    for (int i = 0; i < a->length; i++) {
        if (a->flag[i] == 0) {
            el[len++] = strdup(a->txt[i]);
        } else if (len > 0 && ((uintptr_t)el[len - 1] == MINER_SD || (uintptr_t)el[len - 1] == MINER_SS)) {
            el[len - 1] = (void*)(uintptr_t)MINER_SS;
        } else {
            el[len++] = (void*)(uintptr_t)(a->flag[i] == 1 ? MINER_SD : MINER_SS);
        }
    }
    *out_len = len;
    return el;
}

LKResult* elToLK(void** el, int el_len) {
    LKResult* lk = malloc(sizeof(LKResult));
    lk->lits = malloc((el_len + 1) * sizeof(char*));
    lk->kinds = malloc(el_len + 1);
    
    int lit_idx = 0;
    int kind_idx = 0;
    
    size_t cur_cap = 256;
    char* cur = malloc(cur_cap);
    cur[0] = '\0';
    size_t cur_len = 0;
    
    for (int i = 0; i < el_len; i++) {
        uintptr_t x = (uintptr_t)el[i];
        if (x != MINER_SD && x != MINER_SS) {
            const char* s = (const char*)el[i];
            size_t slen = strlen(s);
            if (cur_len + slen + 1 >= cur_cap) {
                cur_cap = (cur_len + slen + 1) * 2;
                cur = realloc(cur, cur_cap);
            }
            memcpy(cur + cur_len, s, slen);
            cur_len += slen;
            cur[cur_len] = '\0';
        } else {
            lk->lits[lit_idx++] = strdup(cur);
            cur[0] = '\0';
            cur_len = 0;
            lk->kinds[kind_idx++] = (x == MINER_SD) ? 'd' : 's';
        }
    }
    lk->lits[lit_idx++] = strdup(cur);
    lk->kinds[kind_idx] = '\0';
    free(cur);
    lk->lits_count = lit_idx;
    return lk;
}

void freeLKResult(LKResult* lk) {
    if (!lk) return;
    for (int i = 0; i < lk->lits_count; i++) free(lk->lits[i]);
    free(lk->lits);
    free(lk->kinds);
    free(lk);
}

typedef struct {
    int i;
    int j;
} Pair;

static inline bool eq_elem(void* x, const char* y, int yf) {
    if ((uintptr_t)x == MINER_SD || (uintptr_t)x == MINER_SS) return false;
    if (yf != 0) return false;
    return strcmp((const char*)x, y) == 0;
}

static Pair* lcsPairs(void** X, int n, char** Y, int* yf, int m, int* out_count) {
    int a = 0;
    while (a < n && a < m && eq_elem(X[a], Y[a], yf[a])) a++;
    
    int b = 0;
    while (b < n - a && b < m - a && eq_elem(X[n - 1 - b], Y[m - 1 - b], yf[m - 1 - b])) b++;
    
    int N = n - a - b;
    int M = m - a - b;
    
    int pairs_cap = a + b + N + 16;
    Pair* pairs = malloc(pairs_cap * sizeof(Pair));
    int pairs_count = 0;
    
    for (int i = 0; i < a; i++) {
        pairs[pairs_count++] = (Pair){ i, i };
    }
    
    if (N > 0 && M > 0) {
        if ((int64_t)N * M > 3000000LL) {
            free(pairs);
            *out_count = 0;
            return NULL;
        }
        int W = M + 1;
        uint16_t* dp = calloc((N + 1) * W, sizeof(uint16_t));
        for (int i = N - 1; i >= 0; i--) {
            for (int j = M - 1; j >= 0; j--) {
                if (eq_elem(X[a + i], Y[a + j], yf[a + j])) {
                    dp[i * W + j] = dp[(i + 1) * W + j + 1] + 1;
                } else {
                    uint16_t u = dp[(i + 1) * W + j];
                    uint16_t v = dp[i * W + j + 1];
                    dp[i * W + j] = (u > v) ? u : v;
                }
            }
        }
        int i = 0, j = 0;
        while (i < N && j < M) {
            if (eq_elem(X[a + i], Y[a + j], yf[a + j]) && dp[i * W + j] == dp[(i + 1) * W + j + 1] + 1) {
                pairs[pairs_count++] = (Pair){ a + i, a + j };
                i++;
                j++;
            } else if (dp[(i + 1) * W + j] >= dp[i * W + j + 1]) {
                i++;
            } else {
                j++;
            }
        }
        free(dp);
    }
    
    for (int k = b; k > 0; k--) {
        pairs[pairs_count++] = (Pair){ n - k, m - k };
    }
    
    *out_count = pairs_count;
    return pairs;
}

void** mergeEl(void** X, int X_len, LexResult* A, double threshold, int* out_len, double* out_sim) {
    char** Y = A->txt;
    int* yf = A->flag;
    int Y_len = A->length;
    
    int nlit = 0, common = 0;
    for (int i = 0; i < X_len; i++) {
        if ((uintptr_t)X[i] != MINER_SD && (uintptr_t)X[i] != MINER_SS) {
            nlit++;
            const char* s = (const char*)X[i];
            for (int j = 0; j < Y_len; j++) {
                if (yf[j] == 0 && strcmp(s, Y[j]) == 0) {
                    common++;
                    break;
                }
            }
        }
    }
    
    int denom = nlit + A->nplain;
    int min_c = (common < A->nplain) ? common : A->nplain;
    if (denom > 0 && (2.0 * min_c) / (double)denom < threshold) {
        *out_len = 0; *out_sim = 0.0; return NULL;
    }
    
    int pairs_count = 0;
    Pair* pairs = lcsPairs(X, X_len, Y, yf, Y_len, &pairs_count);
    if (!pairs) {
        *out_len = 0; *out_sim = 0.0; return NULL;
    }
    
    double sim = (denom == 0) ? 1.0 : (2.0 * pairs_count) / (double)denom;
    if (sim < threshold) {
        free(pairs);
        *out_len = 0; *out_sim = 0.0; return NULL;
    }
    
    int res_cap = X_len + Y_len + 16;
    void** res = malloc(res_cap * sizeof(void*));
    int res_len = 0;
    
    int pi = 0, pj = 0;
    for (int k = 0; k <= pairs_count; k++) {
        int i = (k < pairs_count) ? pairs[k].i : X_len;
        int j = (k < pairs_count) ? pairs[k].j : Y_len;
        int gx = i - pi;
        int gy = j - pj;
        if (gx > 0 || gy > 0) {
            if (gx == 1 && gy == 1 && (uintptr_t)X[pi] == MINER_SD && yf[pj] == 1) {
                res[res_len++] = (void*)(uintptr_t)MINER_SD;
            } else {
                res[res_len++] = (void*)(uintptr_t)MINER_SS;
            }
        }
        if (k < pairs_count) {
            res[res_len++] = strdup((const char*)X[i]);
            pi = i + 1;
            pj = j + 1;
        }
    }
    free(pairs);
    
    *out_len = res_len;
    *out_sim = sim;
    return res;
}
