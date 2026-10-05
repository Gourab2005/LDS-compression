#include "../include/lds.h"
#include "encoder.h"
#include "decoder.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>

struct LDSEncoder {
    Encoder* enc;
    LDSConfig cfg;
};

struct LDSDecoder {
    Decoder* dec;
    LDSConfig cfg;
};

LDSConfig lds_default_config(void) {
    LDSConfig cfg;
    cfg.maxDictBytes  = SHARED.maxDictBytes;
    cfg.maxEntryBytes = SHARED.maxEntryBytes;
    cfg.corpusWin     = SHARED.corpusWin;
    cfg.maxTemplates  = SHARED.maxTemplates;
    return cfg;
}

LDSEncoder* lds_encoder_create(void) {
    LDSConfig cfg = lds_default_config();
    return lds_encoder_create_with_config(&cfg);
}

LDSEncoder* lds_encoder_create_with_config(const LDSConfig* config) {
    LDSEncoder* le = malloc(sizeof(LDSEncoder));
    if (!le) return NULL;
    le->cfg = config ? *config : lds_default_config();
    le->enc = createEncoder();
    if (!le->enc) {
        free(le);
        return NULL;
    }
    return le;
}

uint8_t* lds_encode_line(LDSEncoder* enc, const char* line, size_t* out_len) {
    if (!enc || !enc->enc || !line) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    const char* lines[1] = { line };
    return encodeBlock(enc->enc, lines, 1, out_len);
}

uint8_t* lds_encode_block(LDSEncoder* enc, const char* const* lines, size_t num_lines, size_t* out_len) {
    if (!enc || !enc->enc || !lines || num_lines == 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    return encodeBlock(enc->enc, (const char**)lines, num_lines, out_len);
}

void lds_encoder_reset(LDSEncoder* enc) {
    if (!enc) return;
    if (enc->enc) freeEncoder(enc->enc);
    enc->enc = createEncoder();
}

void lds_encoder_free(LDSEncoder* enc) {
    if (!enc) return;
    if (enc->enc) freeEncoder(enc->enc);
    free(enc);
}

LDSDecoder* lds_decoder_create(void) {
    LDSConfig cfg = lds_default_config();
    return lds_decoder_create_with_config(&cfg);
}

LDSDecoder* lds_decoder_create_with_config(const LDSConfig* config) {
    LDSDecoder* ld = malloc(sizeof(LDSDecoder));
    if (!ld) return NULL;
    ld->cfg = config ? *config : lds_default_config();
    ld->dec = createDecoder();
    if (!ld->dec) {
        free(ld);
        return NULL;
    }
    return ld;
}

char* lds_decode_line(LDSDecoder* dec, const uint8_t* packet, size_t packet_len) {
    if (!dec || !dec->dec || !packet || packet_len == 0) return NULL;
    size_t count = 0;
    char** lines = decodeBlock(dec->dec, packet, packet_len, &count);
    if (!lines || count == 0) return NULL;
    char* result = lines[0];
    free(lines); // only free outer container array, caller owns lines[0]
    return result;
}

char** lds_decode_block(LDSDecoder* dec, const uint8_t* packet, size_t packet_len, size_t* out_lines_count) {
    if (!dec || !dec->dec || !packet || packet_len == 0) {
        if (out_lines_count) *out_lines_count = 0;
        return NULL;
    }
    return decodeBlock(dec->dec, packet, packet_len, out_lines_count);
}

void lds_free_lines(char** lines, size_t num_lines) {
    if (!lines) return;
    freeLines(lines, num_lines);
}

void lds_decoder_reset(LDSDecoder* dec) {
    if (!dec) return;
    if (dec->dec) freeDecoder(dec->dec);
    dec->dec = createDecoder();
}

void lds_decoder_free(LDSDecoder* dec) {
    if (!dec) return;
    if (dec->dec) freeDecoder(dec->dec);
    free(dec);
}
