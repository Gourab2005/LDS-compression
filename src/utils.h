#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define MINM 6

typedef struct {
    const char* ptr;
    int len;
} StrSlice;

static inline bool slice_eq(StrSlice a, StrSlice b) {
    if (a.len != b.len) return false;
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}

static inline bool slice_eq_str(StrSlice sl, const char* s) {
    size_t slen = strlen(s);
    if ((size_t)sl.len != slen) return false;
    return memcmp(sl.ptr, s, slen) == 0;
}

static inline int padOfSlice(StrSlice sl, int old_pad) {
    if (sl.len <= 0) return old_pad;
    if (sl.ptr[0] == '-') return old_pad;
    if (sl.ptr[0] == '0' && sl.len > 1) return sl.len;
    return 0;
}

static inline char* strdup_slice(StrSlice sl) {
    char* s = malloc(sl.len + 1);
    memcpy(s, sl.ptr, sl.len);
    s[sl.len] = '\0';
    return s;
}

static inline int64_t parse_int64(const char* s, int len) {
    if (len <= 0) return 0;
    int64_t v = 0;
    int i = 0;
    bool neg = false;
    if (s[0] == '-') { neg = true; i = 1; }
    for (; i < len; i++) {
        v = v * 10 + (s[i] - '0');
    }
    return neg ? -v : v;
}

void putUv(uint64_t v, uint8_t** buf, size_t* len, size_t* cap);
uint64_t getUv(const uint8_t* data, size_t data_len, size_t* pos);

void putZz(int64_t d, uint8_t** buf, size_t* len, size_t* cap);
int64_t getZz(const uint8_t* data, size_t data_len, size_t* pos);

int padOf(const char* t, int old_pad);

typedef struct {
    size_t win;
    size_t cap;
    uint8_t* buf;
    size_t len;
    bool index;
    int32_t* head;
    int32_t* prev;
} Corpus;

Corpus* createCorpus(size_t win, bool index);
void freeCorpus(Corpus* c);
void corpusAdd(Corpus* c, const uint8_t* b, size_t b_len);
void corpusAddString(Corpus* c, const char* s);
void corpusAddSlice(Corpus* c, StrSlice sl);
bool corpusLongest(Corpus* c, const uint8_t* b, size_t p, size_t b_len, size_t maxLen, size_t* out_len, size_t* out_pos);

void encStr(const char* t, uint8_t** out_buf, size_t* out_len, Corpus* corpus);
void encStrSlice(StrSlice sl, uint8_t** out_buf, size_t* out_len, Corpus* corpus);
char* decStr(const uint8_t* data, size_t data_len, size_t* pos, Corpus* corpus);

#endif
