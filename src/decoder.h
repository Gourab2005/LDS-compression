#ifndef DECODER_H
#define DECODER_H

#include "config.h"
#include "model.h"
#include "wire.h"
#include <stdint.h>
#include <stddef.h>

typedef struct Decoder Decoder;

Decoder* createDecoder();
void freeDecoder(Decoder* dec);

char** decodeBlock(Decoder* dec, const uint8_t* packet, size_t packet_len, size_t* num_lines);
char* decodeLine(Decoder* dec, const uint8_t* packet, size_t packet_len);

void freeLines(char** lines, size_t num_lines);

#endif
