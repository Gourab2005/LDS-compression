#include "config.h"

// Edge-device optimized configuration:
// - Corpus window: 8KB (was 64KB) → corpus ~144KB  
// - Templates: 256 (was 1024)
// - Dict budget: 24KB
// - Ring sizes: 512 (was 16384) → saves ~2MB of hash maps
SharedConfig SHARED = {
    .maxDictBytes  = 24000,
    .maxEntryBytes = 64,
    .corpusWin     = 8192,
    .maxTemplates  = 256,
};

EncOnlyConfig ENC_ONLY = {
    .simExtend  = 0.75,
    .simSeed    = 0.65,
    .maxRedefs  = 32,
    .maxSeeds   = 16,
    .maxTry     = 32,
    .maxLineLen = 16384,
};

int tidBitsOf(SharedConfig o) {
    int bits = (int)ceil(log2((double)o.maxTemplates));
    return bits > 1 ? bits : 1;
}
