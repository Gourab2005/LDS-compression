#ifndef MINER_H
#define MINER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MINER_SD 0
#define MINER_SS 1

typedef struct {
    char** txt;
    int* flag;
    int length;
    int capacity;
    int nplain;
} LexResult;

typedef struct {
    char** lits;
    int lits_count;
    char* kinds;
} LKResult;

LexResult* lexLine(const char* line);
void freeLexResult(LexResult* r);

void** elFromAtoms(LexResult* a, int* out_len);
LKResult* elToLK(void** el, int el_len);
void freeLKResult(LKResult* lk);

void** mergeEl(void** X, int X_len, LexResult* A, double threshold, int* out_len, double* out_sim);

#endif
