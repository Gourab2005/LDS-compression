/**
 * @file 02_batch_stream.c
 * @brief Demonstrates batched / block-based log compression.
 *
 * Useful for systems that buffer small groups of lines (e.g. 10 to 500 lines)
 * before flushing a packet to reduce TCP/MQTT header overhead.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/lds.h"

int main(void) {
    printf("=== LDS Example 2: Batched Block Streaming ===\n\n");

    LDSEncoder* encoder = lds_encoder_create();
    LDSDecoder* decoder = lds_decoder_create();

    const char* batch[] = {
        "2026-10-05 03:00:01.001 [AUTH] user=admin ip=192.168.1.50 action=login status=SUCCESS",
        "2026-10-05 03:00:01.050 [HTTP] GET /api/v1/telemetry 200 latency=12ms user_agent=Mozilla",
        "2026-10-05 03:00:01.120 [HTTP] GET /api/v1/sensors/temp 200 latency=8ms user_agent=Mozilla",
        "2026-10-05 03:00:01.200 [WARN] [DB] query_pool=MAIN active_conn=48/50 wait_ms=45",
        "2026-10-05 03:00:01.350 [AUTH] user=guest ip=192.168.1.88 action=login status=FAILED",
        "2026-10-05 03:00:01.400 [HTTP] POST /api/v1/auth/token 401 latency=15ms user_agent=Curl",
        "2026-10-05 03:00:01.500 [INFO] [SYSTEM] cpu_usage=14.2% ram_free=482MB disk_io=1.2MB/s",
        "2026-10-05 03:00:01.600 [INFO] [SYSTEM] cpu_usage=15.1% ram_free=480MB disk_io=0.8MB/s"
    };
    size_t batch_size = sizeof(batch) / sizeof(batch[0]);

    size_t raw_bytes = 0;
    for (size_t i = 0; i < batch_size; i++) raw_bytes += strlen(batch[i]);

    // 1. Compress the entire batch into one packet
    size_t packet_len = 0;
    uint8_t* packet = lds_encode_block(encoder, batch, batch_size, &packet_len);

    printf("Original Lines : %zu lines (%zu raw bytes)\n", batch_size, raw_bytes);
    printf("Compressed Size: %zu bytes\n", packet_len);
    printf("Space Saved    : %.1f%%\n\n", (1.0 - (double)packet_len / raw_bytes) * 100.0);

    // 2. Decompress the packet on the server
    size_t restored_count = 0;
    char** restored = lds_decode_block(decoder, packet, packet_len, &restored_count);

    printf("Decompressed %zu lines:\n", restored_count);
    bool all_ok = (restored_count == batch_size);

    for (size_t i = 0; i < restored_count; i++) {
        bool match = (strcmp(batch[i], restored[i]) == 0);
        if (!match) all_ok = false;
        printf("  [%s] Line %zu: %s\n", match ? "OK" : "MISMATCH", i + 1, restored[i]);
    }

    printf("\nBatch Round-Trip Result: %s\n", all_ok ? "PASSED (100% Exact)" : "FAILED");

    // Clean up
    free(packet);
    lds_free_lines(restored, restored_count);
    lds_encoder_free(encoder);
    lds_decoder_free(decoder);

    return all_ok ? 0 : 1;
}
