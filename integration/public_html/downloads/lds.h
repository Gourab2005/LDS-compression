/**
 * @file lds.h
 * @brief LDS (Log Data Stream) - Ultra-Low-Memory Online Log Compression Library
 *
 * LDS provides real-time, online log compression designed for memory-constrained
 * edge devices (MCU, IoT gateways, automotive ECUs) and high-throughput servers.
 *
 * Typical Memory Footprint: ~166 KB (Fixed ceiling, no unbounded growth)
 * Compression Ratio: 8x - 20x (85% - 95% bandwidth reduction)
 * Throughput: 50,000 - 100,000+ lines/sec
 */

#ifndef LDS_H
#define LDS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles */
typedef struct LDSEncoder LDSEncoder;
typedef struct LDSDecoder LDSDecoder;

/**
 * @brief Memory & algorithm configuration options.
 */
typedef struct {
    size_t maxDictBytes;   /**< Max string dictionary memory in bytes (default: 24000) */
    size_t maxEntryBytes;  /**< Max length for a single dictionary entry (default: 64) */
    size_t corpusWin;      /**< Sliding LZ corpus window in bytes (default: 8192) */
    int    maxTemplates;   /**< Maximum number of active learned templates (default: 256) */
} LDSConfig;

/**
 * @brief Get the default edge-optimized configuration (~166 KB RAM budget).
 */
LDSConfig lds_default_config(void);

/* ========================================================================= */
/*                              ENCODER API                                  */
/* ========================================================================= */

/**
 * @brief Create a new LDS Encoder with default edge configuration (~166 KB RAM).
 * @return Pointer to encoder instance, or NULL on allocation failure.
 */
LDSEncoder* lds_encoder_create(void);

/**
 * @brief Create a new LDS Encoder with custom configuration parameters.
 * @param config Pointer to custom configuration.
 * @return Pointer to encoder instance, or NULL on allocation failure.
 */
LDSEncoder* lds_encoder_create_with_config(const LDSConfig* config);

/**
 * @brief Compress a single log line in real-time online streaming mode.
 *
 * @param enc Pointer to encoder instance.
 * @param line Null-terminated log string.
 * @param out_len Output pointer receiving the compressed payload length in bytes.
 * @return Allocated buffer containing compressed bytes (caller MUST call free()).
 */
uint8_t* lds_encode_line(LDSEncoder* enc, const char* line, size_t* out_len);

/**
 * @brief Compress a batch of log lines into a single transport block.
 *
 * @param enc Pointer to encoder instance.
 * @param lines Array of null-terminated log strings.
 * @param num_lines Number of lines in the array.
 * @param out_len Output pointer receiving the compressed block size in bytes.
 * @return Allocated buffer containing compressed block (caller MUST call free()).
 */
uint8_t* lds_encode_block(LDSEncoder* enc, const char* const* lines, size_t num_lines, size_t* out_len);

/**
 * @brief Reset encoder state (clears learned templates and dictionaries).
 */
void lds_encoder_reset(LDSEncoder* enc);

/**
 * @brief Free encoder instance and all associated memory.
 */
void lds_encoder_free(LDSEncoder* enc);

/* ========================================================================= */
/*                              DECODER API                                  */
/* ========================================================================= */

/**
 * @brief Create a new LDS Decoder with default configuration.
 * @return Pointer to decoder instance, or NULL on allocation failure.
 */
LDSDecoder* lds_decoder_create(void);

/**
 * @brief Create a new LDS Decoder with matching custom configuration.
 * @param config Pointer to custom configuration (MUST match encoder config).
 * @return Pointer to decoder instance, or NULL on allocation failure.
 */
LDSDecoder* lds_decoder_create_with_config(const LDSConfig* config);

/**
 * @brief Decompress a single line compressed with lds_encode_line().
 *
 * @param dec Pointer to decoder instance.
 * @param packet Buffer containing the compressed packet.
 * @param packet_len Length of the packet buffer.
 * @return Allocated string containing original log line (caller MUST call free()).
 */
char* lds_decode_line(LDSDecoder* dec, const uint8_t* packet, size_t packet_len);

/**
 * @brief Decompress a block of lines compressed with lds_encode_block().
 *
 * @param dec Pointer to decoder instance.
 * @param packet Buffer containing the compressed block.
 * @param packet_len Length of the packet block.
 * @param out_lines_count Output pointer receiving the number of decompressed lines.
 * @return Array of allocated strings (caller MUST call lds_free_lines()).
 */
char** lds_decode_block(LDSDecoder* dec, const uint8_t* packet, size_t packet_len, size_t* out_lines_count);

/**
 * @brief Free an array of strings returned by lds_decode_block().
 *
 * @param lines Array of strings.
 * @param num_lines Number of lines in the array.
 */
void lds_free_lines(char** lines, size_t num_lines);

/**
 * @brief Reset decoder state.
 */
void lds_decoder_reset(LDSDecoder* dec);

/**
 * @brief Free decoder instance and all associated memory.
 */
void lds_decoder_free(LDSDecoder* dec);

#ifdef __cplusplus
}
#endif

#endif /* LDS_H */
