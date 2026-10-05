#ifndef WIRE_H
#define WIRE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PB 15
#define ONE (1 << PB)
#define INIT (ONE >> 1)
#define TOP (1 << 24)
#define SH 5
#define NUMSZ 160
#define BIGSZ 640
/* Reduced byte context for wireBlob: 32 low-bit buckets + 1 sentinel */
#define BYTE_CTX_SZ 33

static inline uint64_t zz(int64_t d) {
    return d >= 0 ? ((uint64_t)d << 1) : (((uint64_t)(-d) << 1) - 1);
}

static inline int64_t unzz(uint64_t z) {
    return (z & 1) == 0 ? (int64_t)(z >> 1) : -(int64_t)((z + 1) >> 1);
}

// Adaptive binary range coder
typedef struct {
    bool enc;
    // Encoding state
    uint8_t* out;
    size_t out_len;
    size_t out_cap;
    uint64_t low;
    uint32_t range;
    uint8_t cache;
    uint32_t cs;
    
    // Decoding state
    const uint8_t* b;
    size_t b_len;
    size_t p;
    uint32_t code;
} Coder;

void coderStartEnc(Coder* c);
void coderStartDec(Coder* c, const uint8_t* buf, size_t buf_len);
uint8_t* coderFinish(Coder* c, size_t* out_len);

uint8_t coderBit(Coder* c, uint16_t* P, size_t i, uint8_t b);
uint8_t coderDirect(Coder* c, uint8_t b);
uint32_t coderTree(Coder* c, uint16_t* P, size_t base, int nbits, uint32_t v);
uint32_t coderNum(Coder* c, uint16_t* P, size_t base, uint32_t v);
uint64_t coderBig(Coder* c, uint16_t* P, size_t base, uint64_t z);

typedef struct {
    int n;
    uint16_t* pHit;    // n * 16
    uint16_t* pDict;   // n * 4
    uint16_t  pEsc[1]; // 1
    uint8_t*  hist;    // n
    uint16_t** pInt;   // n x BIGSZ
    uint16_t** pRank;  // n x NUMSZ
    uint16_t** pLen;   // n x NUMSZ
    uint16_t** pPad;   // n x NUMSZ
    uint16_t* pNC;     // n
    uint16_t* pDR;     // n
    uint16_t* pSN;     // n
    uint16_t** pMag;   // n x BIGSZ
    uint16_t** pND;    // n x NUMSZ
    uint16_t* pSG;     // n
    uint16_t** pSD;    // n x NUMSZ
} TplContext;

TplContext* createTplContext(int n);
void freeTplContext(TplContext* cx);

typedef struct {
    Coder c;
    int tidBits;
    int maxTemplates;
    uint16_t pOp[16];
    uint16_t** pTid;      // (maxTemplates + 1) pointers
    uint16_t* pRedefTid;  // 1 << tidBits
    uint16_t* pByte;      // 257 * 256
    uint16_t pRawLen[NUMSZ];
    uint16_t pLitLen[NUMSZ];
    uint16_t pDefStrLen[NUMSZ];
    uint16_t pDefN[NUMSZ];
    uint16_t pDefPad[NUMSZ];
    uint16_t pDefInt[BIGSZ];
    uint16_t pKind[2];
    int prevOp;
    int prevTid;
} Wire;

Wire* createWire(int tidBits, int maxTemplates);
void freeWire(Wire* w);
int wireOp(Wire* w, int op);
int wireTid(Wire* w, int tid);
uint8_t* wireBlob(Wire* w, uint16_t* Plen, size_t base, const uint8_t* arr, size_t in_len, size_t* out_len);

#endif
