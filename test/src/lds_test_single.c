/**
 * @file lds_test_single.c
 * @brief High-Speed Benchmark Engine for Line-by-Line Log Streaming.
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
      return 166;
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

static void escape_json_print(FILE* out, const char* str) {
    if (!str) return;
    for (const char* p = str; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\b': fputs("\\b", out); break;
            case '\f': fputs("\\f", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if (c < 32) {
                    fprintf(out, "\\u%04x", c);
                } else {
                    fputc(c, out);
                }
                break;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <log_file_path> [line_limit]\n", argv[0]);
        return 1;
    }

    const char* filepath = argv[1];
    uint64_t line_limit = (argc > 2) ? (uint64_t)strtoull(argv[2], NULL, 10) : 0;

    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "{\"error\":\"failed_to_open_file\",\"path\":\"%s\"}\n", filepath);
        return 1;
    }

    /* 1MB fast file buffer */
    char* io_buf = (char*)malloc(1024 * 1024);
    if (io_buf) setvbuf(fp, io_buf, _IOFBF, 1024 * 1024);

    LDSEncoder* enc = lds_encoder_create();
    LDSDecoder* dec = lds_decoder_create();
    if (!enc || !dec) {
        fprintf(stderr, "{\"error\":\"failed_to_init_lds\"}\n");
        if (fp) fclose(fp);
        if (io_buf) free(io_buf);
        return 1;
    }

    uint64_t total_lines = 0;
    uint64_t total_raw = 0;
    uint64_t total_comp = 0;
    uint64_t verified_matches = 0;

    double max_ratio = 0.0;
    double max_savings = 0.0;
    char max_log[2048] = {0};
    uint32_t max_raw = 0, max_comp = 0;

    double min_ratio = 999999.0;
    double min_savings = 100.0;
    char min_log[2048] = {0};
    uint32_t min_raw = 0, min_comp = 0;

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

        /* 1. Measure Pure Encoding */
        double t_enc0 = get_hires_time();
        size_t enc_len = 0;
        uint8_t* packet = lds_encode_line(enc, line_buf, &enc_len);
        double t_enc1 = get_hires_time();
        enc_time_sec += (t_enc1 - t_enc0);

        if (!packet) {
            fprintf(stderr, "{\"error\":\"encode_failed\",\"line\":%I64u}\n", (uint64_t)total_lines);
            break;
        }

        /* 2. Measure Pure Decoding */
        double t_dec0 = get_hires_time();
        char* restored = lds_decode_line(dec, packet, enc_len);
        double t_dec1 = get_hires_time();
        dec_time_sec += (t_dec1 - t_dec0);

        /* 3. Verification */
        if (restored && strcmp(line_buf, restored) == 0) {
            verified_matches++;
        }

        total_raw += len;
        total_comp += enc_len;

        double line_ratio = enc_len > 0 ? (double)len / (double)enc_len : 1.0;
        double line_savings = len > 0 ? (1.0 - (double)enc_len / (double)len) * 100.0 : 0.0;

        /* Track Highest */
        if (line_ratio > max_ratio) {
            max_ratio = line_ratio;
            max_savings = line_savings;
            max_raw = (uint32_t)len;
            max_comp = (uint32_t)enc_len;
            strncpy(max_log, line_buf, sizeof(max_log) - 1);
            max_log[sizeof(max_log) - 1] = '\0';
        }

        /* Track Lowest */
        if (total_lines > 5 && line_ratio < min_ratio) {
            min_ratio = line_ratio;
            min_savings = line_savings;
            min_raw = (uint32_t)len;
            min_comp = (uint32_t)enc_len;
            strncpy(min_log, line_buf, sizeof(min_log) - 1);
            min_log[sizeof(min_log) - 1] = '\0';
        } else if (total_lines <= 5 && total_lines == 1) {
            min_ratio = line_ratio;
            min_savings = line_savings;
            min_raw = (uint32_t)len;
            min_comp = (uint32_t)enc_len;
            strncpy(min_log, line_buf, sizeof(min_log) - 1);
            min_log[sizeof(min_log) - 1] = '\0';
        }

        free(packet);
        if (restored) free(restored);

        if (line_limit > 0 && total_lines >= line_limit) {
            break;
        }
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

    /* Output comprehensive JSON */
    printf("{\n");
    printf("  \"file\": \"");
    escape_json_print(stdout, filepath);
    printf("\",\n");
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
    printf("    \"log\": \"");
    escape_json_print(stdout, max_log);
    printf("\"\n  },\n");
    printf("  \"lowest\": {\n");
    printf("    \"ratio\": %.2f,\n", min_ratio < 999990.0 ? min_ratio : 1.0);
    printf("    \"savings_percent\": %.2f,\n", min_savings < 99.0 ? min_savings : 0.0);
    printf("    \"raw_bytes\": %u,\n", min_raw);
    printf("    \"comp_bytes\": %u,\n", min_comp);
    printf("    \"log\": \"");
    escape_json_print(stdout, min_log);
    printf("\"\n  }\n");
    printf("}\n");
    fflush(stdout);

    lds_encoder_free(enc);
    lds_decoder_free(dec);
    fclose(fp);
    if (io_buf) free(io_buf);

    return 0;
}
