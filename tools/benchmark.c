#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/lds.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
size_t getPeakRSS(void) {
    PROCESS_MEMORY_COUNTERS info;
    GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
    return (size_t)info.PeakWorkingSetSize;
}
#else
#include <sys/resource.h>
size_t getPeakRSS(void) {
    struct rusage rusage;
    getrusage(RUSAGE_SELF, &rusage);
    return (size_t)(rusage.ru_maxrss * 1024L);
}
#endif

#define CHUNK_SIZE 512
#define PROGRESS_INTERVAL 50000

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <logfile>\n", argv[0]);
        return 1;
    }

    FILE* fp = fopen(argv[1], "r");
    if (!fp) {
        perror("fopen");
        return 1;
    }
    setvbuf(fp, NULL, _IOFBF, 1024 * 1024);

    LDSEncoder* enc = lds_encoder_create();
    LDSDecoder* dec = lds_decoder_create();

    clock_t start_total = clock();
    double enc_time = 0.0;
    double dec_time = 0.0;

    char* chunk_lines[CHUNK_SIZE];
    printf("Processing: %s\n", argv[1]);
    size_t chunk_count = 0;
    size_t total_lines = 0;
    size_t raw_bytes = 0;
    size_t total_compressed_bytes = 0;
    bool round_trip_ok = true;

    size_t buf_cap = 1024;
    char* dynamic_buffer = malloc(buf_cap);
    size_t buf_len = 0;

    while (1) {
        if (!fgets(dynamic_buffer + buf_len, (int)(buf_cap - buf_len), fp)) {
            break;
        }

        buf_len += strlen(dynamic_buffer + buf_len);

        if (dynamic_buffer[buf_len - 1] != '\n' && !feof(fp)) {
            buf_cap *= 2;
            dynamic_buffer = realloc(dynamic_buffer, buf_cap);
            continue;
        }

        if (buf_len > 0 && dynamic_buffer[buf_len - 1] == '\n') {
            dynamic_buffer[buf_len - 1] = '\0';
            buf_len--;
        }
        if (buf_len > 0 && dynamic_buffer[buf_len - 1] == '\r') {
            dynamic_buffer[buf_len - 1] = '\0';
            buf_len--;
        }

        if (buf_len == 0) {
            buf_len = 0;
            continue;
        }

        chunk_lines[chunk_count] = strdup(dynamic_buffer);
        chunk_count++;
        total_lines++;
        raw_bytes += buf_len;

        if (total_lines % PROGRESS_INTERVAL == 0) {
            printf("  ... %zu lines processed\n", total_lines);
            fflush(stdout);
        }

        buf_len = 0;

        if (chunk_count == CHUNK_SIZE) {
            clock_t t0 = clock();
            size_t packet_len = 0;
            uint8_t* packet = lds_encode_block(enc, (const char* const*)chunk_lines, chunk_count, &packet_len);
            clock_t t1 = clock();
            enc_time += (double)(t1 - t0) / CLOCKS_PER_SEC;
            total_compressed_bytes += packet_len;

            t0 = clock();
            size_t restored_count = 0;
            char** restored = lds_decode_block(dec, packet, packet_len, &restored_count);
            t1 = clock();
            dec_time += (double)(t1 - t0) / CLOCKS_PER_SEC;

            if (restored_count != chunk_count) {
                fprintf(stderr, "Line count mismatch at total_lines %zu: expected %zu, got %zu\n", total_lines, chunk_count, restored_count);
                round_trip_ok = false;
                break;
            } else {
                for (size_t k = 0; k < chunk_count; k++) {
                    if (strcmp(restored[k], chunk_lines[k]) != 0) {
                        fprintf(stderr, "MISMATCH at line %zu:\nExpected: [%s]\nActual  : [%s]\n",
                            total_lines - chunk_count + k + 1, chunk_lines[k], restored[k]);
                        round_trip_ok = false;
                        break;
                    }
                }
                if (!round_trip_ok) break;
            }

            free(packet);
            lds_free_lines(restored, restored_count);
            for (size_t k = 0; k < chunk_count; k++) {
                free(chunk_lines[k]);
            }
            chunk_count = 0;
        }
    }

    if (chunk_count > 0 && round_trip_ok) {
        clock_t t0 = clock();
        size_t packet_len = 0;
        uint8_t* packet = lds_encode_block(enc, (const char* const*)chunk_lines, chunk_count, &packet_len);
        clock_t t1 = clock();
        enc_time += (double)(t1 - t0) / CLOCKS_PER_SEC;
        total_compressed_bytes += packet_len;

        t0 = clock();
        size_t restored_count = 0;
        char** restored = lds_decode_block(dec, packet, packet_len, &restored_count);
        t1 = clock();
        dec_time += (double)(t1 - t0) / CLOCKS_PER_SEC;

        if (restored_count != chunk_count) {
            fprintf(stderr, "Line count mismatch: expected %zu, got %zu\n", chunk_count, restored_count);
            round_trip_ok = false;
        } else {
            for (size_t k = 0; k < chunk_count; k++) {
                if (strcmp(restored[k], chunk_lines[k]) != 0) {
                    fprintf(stderr, "MISMATCH at line %zu:\nExpected: %s\nActual  : %s\n",
                        total_lines - chunk_count + k + 1, chunk_lines[k], restored[k]);
                    round_trip_ok = false;
                    break;
                }
            }
        }

        free(packet);
        lds_free_lines(restored, restored_count);
        for (size_t k = 0; k < chunk_count; k++) {
            free(chunk_lines[k]);
        }
    }

    fclose(fp);

    clock_t end_total = clock();
    double total_time = (double)(end_total - start_total) / CLOCKS_PER_SEC;
    if (total_time <= 0.0) total_time = 0.0001;
    if (enc_time <= 0.0) enc_time = 0.0001;
    if (dec_time <= 0.0) dec_time = 0.0001;

    double mb_raw = (double)raw_bytes / (1024.0 * 1024.0);
    double enc_speed_mb_s = mb_raw / enc_time;
    double enc_lines_per_sec = (double)total_lines / enc_time;

    double dec_speed_mb_s = mb_raw / dec_time;
    double dec_lines_per_sec = (double)total_lines / dec_time;

    size_t peak_ram = getPeakRSS();
    double peak_ram_mb = (double)peak_ram / (1024.0 * 1024.0);

    printf("--- Compression Results ---\n");
    printf("Lines processed : %zu\n", total_lines);
    printf("Raw size        : %zu bytes\n", raw_bytes);
    printf("Compressed size : %zu bytes\n", total_compressed_bytes);
    printf("Compression ratio: %.2fx smaller\n", (double)raw_bytes / (total_compressed_bytes ? total_compressed_bytes : 1));
    printf("Space saved     : %.2f%%\n", (1.0 - (double)total_compressed_bytes / (raw_bytes ? raw_bytes : 1)) * 100.0);

    printf("--- Performance Metrics ---\n");
    printf("Encode Speed    : %.2f MB/s (%.0f lines/s)\n", enc_speed_mb_s, enc_lines_per_sec);
    printf("Decode Speed    : %.2f MB/s (%.0f lines/s)\n", dec_speed_mb_s, dec_lines_per_sec);
    printf("Total Time      : %.4f seconds (incl. File I/O & Round-trip validation)\n", total_time);
    printf("Peak RAM Used   : %.2f MB\n", peak_ram_mb);
    printf("Round trip      : %s\n", round_trip_ok ? "OK" : "FAILED");

    lds_encoder_free(enc);
    lds_decoder_free(dec);
    free(dynamic_buffer);

    return round_trip_ok ? 0 : 1;
}
