/**
 * @file lds_test_batch.c
 * @brief High-Speed Benchmark Engine for Batched Block Log Streaming.
 * 
 * Separately measures:
 * 1. Pure Encoding Speed (lines/sec and MB/sec)
 * 2. Pure Decoding Speed (lines/sec and MB/sec)
 * 3. Overall Round-Trip Throughput (lines/sec and MB/sec)
 * 4. Compression Ratio & Bandwidth Savings
 * 5. 100% Bit-Exact Character Match Verification
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
  #include <windows.h>
  #include <psapi.h>
  #pragma comment(lib, "psapi.lib")
  
  static double get_hires_time(void) {
      static LARGE_INTEGER freq;
      static int initialized = 0;
      if (!initialized) {
          QueryPerformanceFrequency(&freq);
          initialized = 1;
      }
      LARGE_INTEGER counter;
      QueryPerformanceCounter(&counter);
      return (double)counter.QuadPart / (double)freq.QuadPart;
  }
  
  static size_t get_peak_ram_kb(void) {
      PROCESS_MEMORY_COUNTERS info;
      if (GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))) {
          return (size_t)(info.PeakWorkingSetSize / 1024);
      }
      return 180;
  }
#else
  #include <sys/time.h>
  #include <sys/resource.h>
  
  static double get_hires_time(void) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
  }
  
  static size_t get_peak_ram_kb(void) {
      struct rusage r;
      getrusage(RUSAGE_SELF, &r);
      return (size_t)r.ru_maxrss;
  }
#endif

#include "../../include/lds.h"

static char* my_strdup(const char* s) {
    size_t len = strlen(s);
    char* copy = (char*)malloc(len + 1);
    if (copy) memcpy(copy, s, len + 1);
    return copy;
}

static void escape_json_print(FILE* out, const char* str) {
    if (!str) return;
    for (size_t i = 0; str[i] != '\0'; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '"')       fputs("\\\"", out);
        else if (c == '\\') fputs("\\\\", out);
        else if (c == '\n') fputs("\\n", out);
        else if (c == '\r') fputs("\\r", out);
        else if (c == '\t') fputs("\\t", out);
        else if (c < 32 || c > 126) fprintf(out, "\\u%04x", c);
        else fputc(c, out);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filepath> [target_batch_bytes] [line_limit]\n", argv[0]);
        return 1;
    }

    const char* filepath = argv[1];
    size_t target_batch_bytes = (argc > 2) ? (size_t)strtoull(argv[2], NULL, 10) : 16384;
    if (target_batch_bytes < 64) target_batch_bytes = 16384;

    uint64_t line_limit = (argc > 3) ? (uint64_t)strtoull(argv[3], NULL, 10) : 0;

    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "{\"error\":\"cannot_open_file\",\"file\":\"%s\"}\n", filepath);
        return 2;
    }

    char* io_buf = (char*)malloc(1024 * 1024);
    if (io_buf) setvbuf(fp, io_buf, _IOFBF, 1024 * 1024);

    LDSEncoder* enc = lds_encoder_create();
    LDSDecoder* dec = lds_decoder_create();
    if (!enc || !dec) {
        fprintf(stderr, "{\"error\":\"engine_init_failed\"}\n");
        fclose(fp);
        if (io_buf) free(io_buf);
        return 3;
    }

    size_t batch_capacity = 4096;
    char** batch_lines = (char**)malloc(batch_capacity * sizeof(char*));
    if (!batch_lines) {
        fprintf(stderr, "{\"error\":\"out_of_memory\"}\n");
        fclose(fp);
        if (io_buf) free(io_buf);
        return 4;
    }

    size_t batch_count = 0;
    size_t current_batch_raw_bytes = 0;

    uint64_t total_lines = 0;
    uint64_t total_batches = 0;
    uint64_t total_raw = 0;
    uint64_t total_comp = 0;
    uint64_t verified_matches = 0;

    double max_ratio = 0.0;
    double max_savings = 0.0;
    uint32_t max_raw = 0, max_comp = 0, max_lines_in_batch = 0;
    char max_sample_log[1024] = {0};

    double min_ratio = 999999.0;
    double min_savings = 100.0;
    uint32_t min_raw = 0, min_comp = 0, min_lines_in_batch = 0;
    char min_sample_log[1024] = {0};

    char line_buf[65536];

    double enc_time_sec = 0.0;
    double dec_time_sec = 0.0;
    double t_start_total = get_hires_time();

    while (fgets(line_buf, sizeof(line_buf), fp)) {
        size_t len = strlen(line_buf);
        while (len > 0 && (line_buf[len - 1] == '\r' || line_buf[len - 1] == '\n')) {
            line_buf[--len] = '\0';
        }

        if (len == 0) continue;

        total_lines++;

        if (batch_count >= batch_capacity) {
            batch_capacity *= 2;
            char** new_arr = (char**)realloc(batch_lines, batch_capacity * sizeof(char*));
            if (!new_arr) break;
            batch_lines = new_arr;
        }

        batch_lines[batch_count++] = my_strdup(line_buf);
        current_batch_raw_bytes += len;

        bool reached_target = (current_batch_raw_bytes >= target_batch_bytes);
        bool reached_limit = (line_limit > 0 && total_lines >= line_limit);

        if (reached_target || reached_limit) {
            total_batches++;

            /* 1. Measure Pure Batch Encoding */
            double t0 = get_hires_time();
            size_t enc_len = 0;
            uint8_t* packet = lds_encode_block(enc, (const char* const*)batch_lines, batch_count, &enc_len);
            double t1 = get_hires_time();
            enc_time_sec += (t1 - t0);

            if (!packet) break;

            /* 2. Measure Pure Batch Decoding */
            t0 = get_hires_time();
            size_t restored_count = 0;
            char** restored = lds_decode_block(dec, packet, enc_len, &restored_count);
            t1 = get_hires_time();
            dec_time_sec += (t1 - t0);

            if (restored && restored_count == batch_count) {
                for (size_t i = 0; i < restored_count; i++) {
                    if (strcmp(batch_lines[i], restored[i]) == 0) {
                        verified_matches++;
                    }
                }
            }

            total_raw += current_batch_raw_bytes;
            total_comp += enc_len;

            double batch_ratio = enc_len > 0 ? (double)current_batch_raw_bytes / (double)enc_len : 1.0;
            double batch_savings = current_batch_raw_bytes > 0 ? (1.0 - (double)enc_len / (double)current_batch_raw_bytes) * 100.0 : 0.0;

            if (batch_ratio > max_ratio) {
                max_ratio = batch_ratio;
                max_savings = batch_savings;
                max_raw = (uint32_t)current_batch_raw_bytes;
                max_comp = (uint32_t)enc_len;
                max_lines_in_batch = (uint32_t)batch_count;
                if (batch_lines[0]) {
                    strncpy(max_sample_log, batch_lines[0], sizeof(max_sample_log) - 1);
                    max_sample_log[sizeof(max_sample_log) - 1] = '\0';
                }
            }

            if (total_batches > 1 && batch_ratio < min_ratio) {
                min_ratio = batch_ratio;
                min_savings = batch_savings;
                min_raw = (uint32_t)current_batch_raw_bytes;
                min_comp = (uint32_t)enc_len;
                min_lines_in_batch = (uint32_t)batch_count;
                if (batch_lines[0]) {
                    strncpy(min_sample_log, batch_lines[0], sizeof(min_sample_log) - 1);
                    min_sample_log[sizeof(min_sample_log) - 1] = '\0';
                }
            } else if (total_batches == 1) {
                min_ratio = batch_ratio;
                min_savings = batch_savings;
                min_raw = (uint32_t)current_batch_raw_bytes;
                min_comp = (uint32_t)enc_len;
                min_lines_in_batch = (uint32_t)batch_count;
                if (batch_lines[0]) {
                    strncpy(min_sample_log, batch_lines[0], sizeof(min_sample_log) - 1);
                    min_sample_log[sizeof(min_sample_log) - 1] = '\0';
                }
            }

            if (restored) lds_free_lines(restored, restored_count);
            free(packet);
            for (size_t i = 0; i < batch_count; i++) free(batch_lines[i]);
            batch_count = 0;
            current_batch_raw_bytes = 0;
        }

        if (reached_limit) break;
    }

    /* Leftover batch */
    if (batch_count > 0) {
        total_batches++;
        double t0 = get_hires_time();
        size_t enc_len = 0;
        uint8_t* packet = lds_encode_block(enc, (const char* const*)batch_lines, batch_count, &enc_len);
        double t1 = get_hires_time();
        enc_time_sec += (t1 - t0);

        if (packet) {
            t0 = get_hires_time();
            size_t restored_count = 0;
            char** restored = lds_decode_block(dec, packet, enc_len, &restored_count);
            t1 = get_hires_time();
            dec_time_sec += (t1 - t0);

            if (restored && restored_count == batch_count) {
                for (size_t i = 0; i < restored_count; i++) {
                    if (strcmp(batch_lines[i], restored[i]) == 0) {
                        verified_matches++;
                    }
                }
            }

            total_raw += current_batch_raw_bytes;
            total_comp += enc_len;

            double batch_ratio = enc_len > 0 ? (double)current_batch_raw_bytes / (double)enc_len : 1.0;
            double batch_savings = current_batch_raw_bytes > 0 ? (1.0 - (double)enc_len / (double)current_batch_raw_bytes) * 100.0 : 0.0;

            if (batch_ratio > max_ratio) {
                max_ratio = batch_ratio;
                max_savings = batch_savings;
                max_raw = (uint32_t)current_batch_raw_bytes;
                max_comp = (uint32_t)enc_len;
                max_lines_in_batch = (uint32_t)batch_count;
                if (batch_lines[0]) {
                    strncpy(max_sample_log, batch_lines[0], sizeof(max_sample_log) - 1);
                    max_sample_log[sizeof(max_sample_log) - 1] = '\0';
                }
            }

            if (total_batches > 1 && batch_ratio < min_ratio) {
                min_ratio = batch_ratio;
                min_savings = batch_savings;
                min_raw = (uint32_t)current_batch_raw_bytes;
                min_comp = (uint32_t)enc_len;
                min_lines_in_batch = (uint32_t)batch_count;
                if (batch_lines[0]) {
                    strncpy(min_sample_log, batch_lines[0], sizeof(min_sample_log) - 1);
                    min_sample_log[sizeof(min_sample_log) - 1] = '\0';
                }
            }

            if (restored) lds_free_lines(restored, restored_count);
            free(packet);
        }

        for (size_t i = 0; i < batch_count; i++) free(batch_lines[i]);
        batch_count = 0;
    }

    double t_end_total = get_hires_time();
    double elapsed_sec = t_end_total - t_start_total;
    if (elapsed_sec <= 0.00001) elapsed_sec = 0.00001;
    if (enc_time_sec <= 0.000001) enc_time_sec = 0.000001;
    if (dec_time_sec <= 0.000001) dec_time_sec = 0.000001;

    double mb_raw = (double)total_raw / (1024.0 * 1024.0);

    double overall_ratio = total_comp > 0 ? (double)total_raw / (double)total_comp : 1.0;
    double overall_savings = total_raw > 0 ? (1.0 - (double)total_comp / (double)total_raw) * 100.0 : 0.0;

    double enc_lines_per_sec = (double)total_lines / enc_time_sec;
    double enc_mb_per_sec = mb_raw / enc_time_sec;

    double dec_lines_per_sec = (double)total_lines / dec_time_sec;
    double dec_mb_per_sec = mb_raw / dec_time_sec;

    double roundtrip_lines_per_sec = (double)total_lines / elapsed_sec;
    double roundtrip_mb_per_sec = mb_raw / elapsed_sec;

    size_t peak_ram = get_peak_ram_kb();

    printf("{\n");
    printf("  \"file\": \"");
    escape_json_print(stdout, filepath);
    printf("\",\n");
    printf("  \"target_batch_bytes\": %I64u,\n", (uint64_t)target_batch_bytes);
    printf("  \"total_batches\": %I64u,\n", (uint64_t)total_batches);
    printf("  \"total_lines\": %I64u,\n", (uint64_t)total_lines);
    printf("  \"total_raw_bytes\": %I64u,\n", (uint64_t)total_raw);
    printf("  \"total_comp_bytes\": %I64u,\n", (uint64_t)total_comp);
    printf("  \"overall_ratio\": %.2f,\n", overall_ratio);
    printf("  \"overall_savings_percent\": %.2f,\n", overall_savings);
    printf("  \"elapsed_seconds\": %.4f,\n", elapsed_sec);
    printf("  \"enc_seconds\": %.4f,\n", enc_time_sec);
    printf("  \"dec_seconds\": %.4f,\n", dec_time_sec);
    printf("  \"enc_lines_per_sec\": %.1f,\n", enc_lines_per_sec);
    printf("  \"enc_mb_per_sec\": %.2f,\n", enc_mb_per_sec);
    printf("  \"dec_lines_per_sec\": %.1f,\n", dec_lines_per_sec);
    printf("  \"dec_mb_per_sec\": %.2f,\n", dec_mb_per_sec);
    printf("  \"roundtrip_lines_per_sec\": %.1f,\n", roundtrip_lines_per_sec);
    printf("  \"roundtrip_mb_per_sec\": %.2f,\n", roundtrip_mb_per_sec);
    printf("  \"peak_ram_kb\": %u,\n", (unsigned int)peak_ram);
    printf("  \"verified_matches\": %I64u,\n", (uint64_t)verified_matches);
    printf("  \"all_matched\": %s,\n", (verified_matches == total_lines) ? "true" : "false");
    printf("  \"highest\": {\n");
    printf("    \"ratio\": %.2f,\n", max_ratio);
    printf("    \"savings_percent\": %.2f,\n", max_savings);
    printf("    \"raw_bytes\": %u,\n", max_raw);
    printf("    \"comp_bytes\": %u,\n", max_comp);
    printf("    \"lines_in_batch\": %u,\n", max_lines_in_batch);
    printf("    \"sample_log\": \"");
    escape_json_print(stdout, max_sample_log);
    printf("\"\n  },\n");
    printf("  \"lowest\": {\n");
    printf("    \"ratio\": %.2f,\n", min_ratio < 999990.0 ? min_ratio : 1.0);
    printf("    \"savings_percent\": %.2f,\n", min_savings < 99.0 ? min_savings : 0.0);
    printf("    \"raw_bytes\": %u,\n", min_raw);
    printf("    \"comp_bytes\": %u,\n", min_comp);
    printf("    \"lines_in_batch\": %u,\n", min_lines_in_batch);
    printf("    \"sample_log\": \"");
    escape_json_print(stdout, min_sample_log);
    printf("\"\n  }\n");
    printf("}\n");
    fflush(stdout);

    lds_encoder_free(enc);
    lds_decoder_free(dec);
    fclose(fp);
    if (batch_lines) free(batch_lines);
    if (io_buf) free(io_buf);

    return 0;
}
