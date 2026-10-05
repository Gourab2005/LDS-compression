#ifndef MODEL_H
#define MODEL_H

#include "config.h"
#include "wire.h"
#include "utils.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define BIGV (1ULL << 32)
#define TPL_BUCKET_CAP   8
#define TPL_HASH_BUCKETS 256

typedef struct {
    size_t size;
    bool index;
    uint64_t* ring;
    uint64_t cnt;
    uint64_t* map_keys;
    uint64_t* map_vals;
    size_t map_cap;
} RecentNum;

RecentNum* createRecentNum(size_t size, bool index);
void freeRecentNum(RecentNum* r);
void recentNumPush(RecentNum* r, uint64_t v);
int recentNumFind(RecentNum* r, uint64_t v);
uint64_t recentNumAt(RecentNum* r, int d);

typedef struct {
    size_t size;
    bool index;
    char** ring;
    uint64_t cnt;
    uint32_t* map_hashes;
    uint64_t* map_vals;
    size_t map_cap;
} RecentStr;

RecentStr* createRecentStr(size_t size, bool index);
void freeRecentStr(RecentStr* r);
void recentStrPush(RecentStr* r, const char* s);
void recentStrPushSlice(RecentStr* r, StrSlice sl);
int recentStrFind(RecentStr* r, const char* s);
int recentStrFindSlice(RecentStr* r, StrSlice sl);
const char* recentStrAt(RecentStr* r, int d);

typedef struct Tpl {
    int tid;
    char** lits;
    int* lit_lens;
    int lits_count;
    int first_lit_len;
    int last_lit_len;
    char* kinds;
    int n;
    int nb;
    int64_t* prevs;
    int64_t* strides;
    int64_t* cands;
    int* pads;
    char** last;
    int* mode;
    int* friend;
    char*** sl;
    int* sl_counts;
    int* sl_caps;
    void** el;
    int el_len;
    int redefs;
    TplContext* cx;
} Tpl;

typedef struct DictEntry {
    int tid;
    int i;
    char* s;
    size_t b;
    struct DictEntry* prev;
    struct DictEntry* next;
} DictEntry;

typedef struct {
    int tids[TPL_BUCKET_CAP];
    int count;
} TplBucket;

typedef struct {
    int maxDictBytes;
    int maxEntryBytes;
    int corpusWin;
    bool index;
    int dictBytes;
    Tpl** tpls;
    int tpls_count;
    int tpls_cap;
    Corpus* corpus;
    RecentNum* numRing;
    RecentStr* strRing;
    DictEntry* lru_head;
    DictEntry* lru_tail;
    TplBucket* prefix_index;
    int* wildcard_tids;
    int wildcard_count;
    int wildcard_cap;
} Model;

Model* createModel(SharedConfig* o, bool index);
void freeModel(Model* m);
void printModelMemory(Model* m);

Tpl* modelDefine(Model* m, int tid, char** lits, int lits_count, const char* kinds);

bool matchTplSlices(const Tpl* tpl, const char* line, int line_len, StrSlice* out_slices);
bool matchTemplateSlices(char** lits, int lits_count, const char* kinds, const char* line, int line_len, StrSlice* out_slices);
char** matchTemplate(char** lits, int lits_count, const char* kinds, const char* line, int* out_vals_count);
char* renderLine(Tpl* tpl, char** strs, int64_t* nums);

int64_t predInt(Tpl* t, int i, int64_t* nums);
void learnInt(Tpl* t, int i, int64_t v, int64_t* nums, bool hit);

const char* predStr(Tpl* t, int i, char** strs);
void learnStr(Tpl* t, int i, const char* s, char** strs, bool hit);
void learnStrSlice(Tpl* t, int i, StrSlice sl, StrSlice* slices, bool hit);

int dictRank(Tpl* t, int i, const char* s);
int dictRankSlice(Tpl* t, int i, StrSlice sl);
const char* dictAt(Tpl* t, int i, int rank);
void dictTouch(Model* m, Tpl* t, int i, const char* s);
void dictTouchSlice(Model* m, Tpl* t, int i, StrSlice sl);
void dictAdd(Model* m, Tpl* t, int i, const char* s);
void dictAddSlice(Model* m, Tpl* t, int i, StrSlice sl);

int modelLookupCandidates(Model* m, const char* line, int line_len, int* out_tids, int max_cands);

#endif
