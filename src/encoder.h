#ifndef ENCODER_H
#define ENCODER_H

#include "config.h"
#include "model.h"
#include "wire.h"
#include "miner.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define OP_DATA  0
#define OP_RAW   1
#define OP_SYNC  2
#define OP_REDEF 3

typedef struct Encoder Encoder;

Encoder* createEncoder();
void freeEncoder(Encoder* enc);

uint8_t* encodeBlock(Encoder* enc, const char** lines, size_t num_lines, size_t* out_len);
uint8_t* encodeLine(Encoder* enc, const char* line, size_t* out_len);

char* getMetrics(Encoder* enc);
void printEncoderModelMemory(Encoder* enc);

#endif
