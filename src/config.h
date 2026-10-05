#ifndef CONFIG_H
#define CONFIG_H

#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int maxDictBytes;
    int maxEntryBytes;
    int corpusWin;
    int maxTemplates;
} SharedConfig;

typedef struct {
    double simExtend;
    double simSeed;
    int maxRedefs;
    int maxSeeds;
    int maxTry;
    int maxLineLen;
} EncOnlyConfig;

extern SharedConfig SHARED;
extern EncOnlyConfig ENC_ONLY;

int tidBitsOf(SharedConfig o);

#endif
