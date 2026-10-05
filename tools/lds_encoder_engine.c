/**
 * @file lds_encoder_engine.c
 * @brief High-Performance C LDS Encoder Engine with Interactive Stdio Bridge.
 * 
 * Used by the Node.js Edge Device to perform ultra-fast online log compression
 * with constant memory (~166 KB RAM ceiling).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#ifdef _WIN32
  #include <io.h>
  #include <fcntl.h>
#endif

#include "../include/lds.h"

static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char* b64_encode(const uint8_t* in, size_t in_len) {
    size_t elen = 4 * ((in_len + 2) / 3);
    char* out = (char*)malloc(elen + 1);
    if (!out) return NULL;
    size_t i = 0, j = 0;
    while (i < in_len) {
        size_t rem = in_len - i;
        uint32_t a = in[i++];
        uint32_t b = (rem > 1) ? in[i++] : 0;
        uint32_t c = (rem > 2) ? in[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out[j++] = b64_chars[(triple >> 18) & 0x3F];
        out[j++] = b64_chars[(triple >> 12) & 0x3F];
        out[j++] = (rem > 1) ? b64_chars[(triple >> 6) & 0x3F] : '=';
        out[j++] = (rem > 2) ? b64_chars[triple & 0x3F] : '=';
    }
    out[j] = '\0';
    return out;
}


int main(int argc, char** argv) {
#ifdef _WIN32
    /* Set line-buffered or unbuffered stdout */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
#else
    setvbuf(stdout, NULL, _IONBF, 0);
#endif

    LDSEncoder* encoder = lds_encoder_create();
    if (!encoder) {
        fprintf(stderr, "[LDS_ENCODER_ENGINE] Error: Failed to allocate LDSEncoder\n");
        return 1;
    }

    /* Print ready greeting */
    printf("{\"status\":\"READY\",\"engine\":\"LDS_C_ENCODER_v1.0\",\"ram_budget_kb\":166}\n");
    fflush(stdout);

    char line_buffer[65536];
    uint64_t seq = 0;
    size_t total_raw = 0;
    size_t total_comp = 0;

    while (fgets(line_buffer, sizeof(line_buffer), stdin)) {
        /* Strip newline characters */
        size_t len = strlen(line_buffer);
        while (len > 0 && (line_buffer[len - 1] == '\r' || line_buffer[len - 1] == '\n')) {
            line_buffer[--len] = '\0';
        }

        if (len == 0) {
            continue;
        }

        /* Check for control commands */
        if (strcmp(line_buffer, "RESET") == 0) {
            lds_encoder_reset(encoder);
            seq = 0;
            total_raw = 0;
            total_comp = 0;
            printf("{\"status\":\"RESET_OK\"}\n");
            fflush(stdout);
            continue;
        } else if (strcmp(line_buffer, "QUIT") == 0 || strcmp(line_buffer, "EXIT") == 0) {
            break;
        } else if (strcmp(line_buffer, "PING") == 0) {
            printf("{\"status\":\"PONG\"}\n");
            fflush(stdout);
            continue;
        }

        /* Standard log line to encode */
        seq++;
        size_t raw_len = len;
        size_t comp_len = 0;
        uint8_t* packet = lds_encode_line(encoder, line_buffer, &comp_len);

        if (!packet) {
            printf("{\"status\":\"ERROR\",\"seq\":%I64u,\"msg\":\"encode_failed\"}\n", (uint64_t)seq);
            fflush(stdout);
            continue;
        }

        char* b64 = b64_encode(packet, comp_len);
        free(packet);

        if (!b64) {
            printf("{\"status\":\"ERROR\",\"seq\":%I64u,\"msg\":\"b64_failed\"}\n", (uint64_t)seq);
            fflush(stdout);
            continue;
        }

        total_raw += raw_len;
        total_comp += comp_len;

        double savings = total_raw > 0 ? (1.0 - (double)total_comp / (double)total_raw) * 100.0 : 0.0;
        double ratio = total_comp > 0 ? (double)total_raw / (double)total_comp : 1.0;

        printf("{\"status\":\"OK\",\"seq\":%I64u,\"raw_bytes\":%u,\"comp_bytes\":%u,\"b64\":\"%s\",\"total_raw\":%I64u,\"total_comp\":%I64u,\"savings\":%.2f,\"ratio\":%.2f}\n",
               (uint64_t)seq,
               (unsigned int)raw_len,
               (unsigned int)comp_len,
               b64,
               (uint64_t)total_raw,
               (uint64_t)total_comp,
               savings,
               ratio);
        fflush(stdout);

        free(b64);
    }

    lds_encoder_free(encoder);
    return 0;
}
