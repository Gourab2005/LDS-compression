/**
 * @file lds_decoder_engine.c
 * @brief High-Performance C LDS Decoder Engine with Interactive Stdio Bridge.
 * 
 * Used by the Node.js Receiver Server to decompress incoming stream packets
 * with exact bit-for-bit reconstruction and constant memory (~166 KB RAM ceiling).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
  #include <io.h>
  #include <fcntl.h>
#endif

#include "../include/lds.h"

static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static uint8_t* b64_decode(const char* in, size_t in_len, size_t* out_len) {
    if (in_len == 0) {
        *out_len = 0;
        return (uint8_t*)strdup("");
    }
    static int8_t table[256];
    static bool init = false;
    if (!init) {
        memset(table, -1, sizeof(table));
        for (int k = 0; k < 64; k++) table[(uint8_t)b64_chars[k]] = (int8_t)k;
        init = true;
    }
    size_t alloc_size = (in_len / 4 + 1) * 3 + 1;
    uint8_t* out = (uint8_t*)malloc(alloc_size);
    if (!out) return NULL;
    size_t i = 0, j = 0;
    while (i < in_len) {
        while (i < in_len && (in[i] == '\r' || in[i] == '\n' || in[i] == ' ' || in[i] == '\t')) i++;
        if (i >= in_len) break;
        char c0 = in[i++];
        char c1 = (i < in_len) ? in[i++] : '=';
        char c2 = (i < in_len) ? in[i++] : '=';
        char c3 = (i < in_len) ? in[i++] : '=';
        int a = table[(uint8_t)c0];
        int b = table[(uint8_t)c1];
        int c = (c2 == '=') ? -2 : table[(uint8_t)c2];
        int d = (c3 == '=') ? -2 : table[(uint8_t)c3];
        if (a < 0 || b < 0) continue;
        uint32_t triple = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)(c >= 0 ? c : 0) << 6) | (uint32_t)(d >= 0 ? d : 0);
        out[j++] = (uint8_t)((triple >> 16) & 0xFF);
        if (c >= 0) out[j++] = (uint8_t)((triple >> 8) & 0xFF);
        if (d >= 0) out[j++] = (uint8_t)(triple & 0xFF);
    }
    *out_len = j;
    return out;
}

static void escape_json_print(const char* str) {
    if (!str) return;
    for (const char* p = str; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"':  fputs("\\\"", stdout); break;
            case '\\': fputs("\\\\", stdout); break;
            case '\b': fputs("\\b", stdout); break;
            case '\f': fputs("\\f", stdout); break;
            case '\n': fputs("\\n", stdout); break;
            case '\r': fputs("\\r", stdout); break;
            case '\t': fputs("\\t", stdout); break;
            default:
                if (c < 32) {
                    printf("\\u%04x", c);
                } else {
                    putchar(c);
                }
                break;
        }
    }
}

int main(int argc, char** argv) {
#ifdef _WIN32
    /* Set unbuffered stdout and stdin */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
#else
    setvbuf(stdout, NULL, _IONBF, 0);
#endif

    LDSDecoder* decoder = lds_decoder_create();
    if (!decoder) {
        fprintf(stderr, "[LDS_DECODER_ENGINE] Error: Failed to allocate LDSDecoder\n");
        return 1;
    }

    printf("{\"status\":\"READY\",\"engine\":\"LDS_C_DECODER_v1.0\",\"ram_budget_kb\":166}\n");
    fflush(stdout);

    char line_buffer[131072];
    uint64_t seq = 0;
    size_t total_comp = 0;
    size_t total_decomp = 0;

    while (fgets(line_buffer, sizeof(line_buffer), stdin)) {
        size_t len = strlen(line_buffer);
        while (len > 0 && (line_buffer[len - 1] == '\r' || line_buffer[len - 1] == '\n')) {
            line_buffer[--len] = '\0';
        }

        if (len == 0) continue;

        if (strcmp(line_buffer, "RESET") == 0) {
            lds_decoder_reset(decoder);
            seq = 0;
            total_comp = 0;
            total_decomp = 0;
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

        /* The input is a Base64 string representing the compressed packet */
        size_t packet_len = 0;
        uint8_t* packet = b64_decode(line_buffer, len, &packet_len);

        if (!packet) {
            printf("{\"status\":\"ERROR\",\"seq\":%I64u,\"msg\":\"b64_decode_failed\"}\n", (uint64_t)seq);
            fflush(stdout);
            continue;
        }

        char* restored_log = lds_decode_line(decoder, packet, packet_len);
        free(packet);

        if (!restored_log) {
            printf("{\"status\":\"ERROR\",\"seq\":%I64u,\"msg\":\"decode_failed\"}\n", (uint64_t)seq);
            fflush(stdout);
            continue;
        }

        seq++;
        size_t decomp_len = strlen(restored_log);
        total_comp += packet_len;
        total_decomp += decomp_len;

        double savings = total_decomp > 0 ? (1.0 - (double)total_comp / (double)total_decomp) * 100.0 : 0.0;
        double ratio = total_comp > 0 ? (double)total_decomp / (double)total_comp : 1.0;

        printf("{\"status\":\"OK\",\"seq\":%I64u,\"comp_bytes\":%u,\"decomp_bytes\":%u,\"total_comp\":%I64u,\"total_decomp\":%I64u,\"savings\":%.2f,\"ratio\":%.2f,\"log\":\"",
               (uint64_t)seq,
               (unsigned int)packet_len,
               (unsigned int)decomp_len,
               (uint64_t)total_comp,
               (uint64_t)total_decomp,
               savings,
               ratio);
        escape_json_print(restored_log);
        printf("\"}\n");
        fflush(stdout);

        free(restored_log);
    }

    lds_decoder_free(decoder);
    return 0;
}
