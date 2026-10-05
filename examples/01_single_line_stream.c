/**
 * @file 01_single_line_stream.c
 * @brief Demonstrates online, line-by-line log compression and decompression.
 *
 * This is the primary mode for edge devices generating logs one line at a time.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/lds.h"

int main(void) {
    printf("=== LDS Example 1: Real-Time Single-Line Streaming ===\n\n");

    // 1. Initialize Encoder (on edge device) and Decoder (on receiver/server)
    LDSEncoder* encoder = lds_encoder_create();
    LDSDecoder* decoder = lds_decoder_create();

    if (!encoder || !decoder) {
        fprintf(stderr, "Failed to initialize LDS encoder/decoder\n");
        return 1;
    }

    // Sample incoming real-time logs (e.g. from automotive ECU or IoT sensor)
    const char* sample_logs[] = {
        "2026-10-05 02:01:47.520 INFO ADAS [RADAR] sensor=FRONT objects=5 dist=41.7m speed=45.2km/h status=OK",
        "2026-10-05 02:01:47.620 INFO ADAS [RADAR] sensor=FRONT objects=5 dist=40.9m speed=45.1km/h status=OK",
        "2026-10-05 02:01:47.720 INFO ADAS [RADAR] sensor=FRONT objects=5 dist=40.1m speed=44.9km/h status=OK",
        "2026-10-05 02:01:47.820 WARN ADAS [RADAR] sensor=FRONT objects=6 dist=26.3m speed=-0.1km/h status=ALERT",
        "2026-10-05 02:01:47.920 INFO BMS [BATTERY] pack=HV01 voltage=386.20V min_cell=3.094V max_cell=4.108V",
        "2026-10-05 02:01:48.020 INFO BMS [BATTERY] pack=HV01 voltage=386.18V min_cell=3.093V max_cell=4.107V"
    };
    size_t num_logs = sizeof(sample_logs) / sizeof(sample_logs[0]);

    size_t total_raw_bytes = 0;
    size_t total_compressed_bytes = 0;

    for (size_t i = 0; i < num_logs; i++) {
        const char* original_log = sample_logs[i];
        size_t raw_len = strlen(original_log);
        total_raw_bytes += raw_len;

        // --- SENDER / EDGE DEVICE ---
        size_t compressed_len = 0;
        uint8_t* packet = lds_encode_line(encoder, original_log, &compressed_len);
        total_compressed_bytes += compressed_len;

        printf("Log #%zu:\n", i + 1);
        printf("  [RAW %3zu B] %s\n", raw_len, original_log);
        printf("  [ENC %3zu B] (Sent over wire)\n", compressed_len);

        // --- RECEIVER / SERVER ---
        char* restored_log = lds_decode_line(decoder, packet, compressed_len);
        printf("  [DEC %3zu B] %s\n", strlen(restored_log), restored_log);

        // Verify exact match
        if (strcmp(original_log, restored_log) == 0) {
            printf("  [STATUS] MATCH OK\n\n");
        } else {
            printf("  [STATUS] MISMATCH ERROR!\n\n");
        }

        free(packet);
        free(restored_log);
    }

    printf("--- Summary ---\n");
    printf("Total Raw Bytes       : %zu bytes\n", total_raw_bytes);
    printf("Total Compressed Bytes: %zu bytes\n", total_compressed_bytes);
    printf("Compression Ratio     : %.2fx smaller (%.1f%% bandwidth saved)\n",
           (double)total_raw_bytes / total_compressed_bytes,
           (1.0 - (double)total_compressed_bytes / total_raw_bytes) * 100.0);

    // Clean up
    lds_encoder_free(encoder);
    lds_decoder_free(decoder);

    return 0;
}
