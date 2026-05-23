#ifndef THINMIX_H
#define THINMIX_H

/*
    ThinMix - small fixed-point multithreaded mixer core

    Public API is C-compatible. The reference implementation is C++11 and uses
    only standard-library threading primitives internally.

    Output format: signed 16-bit PCM, interleaved.

    Each input stream may use the mixer output channel count, mono, or stereo.
    Each input stream declares its input sample format in tm_stream_config_t:
        u8, s8, s16, packed s24 little-endian, or s32.

    Internally ThinMix converts input PCM to s16, then mixes with:
        int16 input -> int32 accumulator -> Q15 gain -> saturated int16 output

    Platform / hardware output is intentionally not configured through tm_create().
    Compile exactly one ThinMixPort_*.cpp file with ThinMix.cpp. That port file
    implements the platform-specific hardware API declared in ThinMixPort.h.
*/

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TM_VERSION_MAJOR 0
#define TM_VERSION_MINOR 5
#define TM_VERSION_PATCH 2

#define TM_Q15_ONE ((int16_t)32767)
#define TM_Q15_HALF ((int16_t)16384)
#define TM_TIMEOUT_INFINITE ((uint32_t)0xffffffffu)

#ifndef TM_MAX_CHANNELS
#define TM_MAX_CHANNELS 8u
#endif

#define TM_CHANNELS_MATCH  ((uint16_t)0u)
#define TM_CHANNELS_MONO   ((uint16_t)1u)
#define TM_CHANNELS_STEREO ((uint16_t)2u)

typedef enum tm_result {
    TM_OK = 0,
    TM_ERR_INVALID = -1,
    TM_ERR_NOMEM = -2,
    TM_ERR_STATE = -3,
    TM_ERR_TIMEOUT = -4,
    TM_ERR_PORT = -5
} tm_result_t;

typedef enum tm_write_mode {
    TM_WRITE_ASYNC = 0,   /* copy what currently fits, return immediately */
    TM_WRITE_BLOCK = 1    /* wait until all requested frames are copied or timeout */
} tm_write_mode_t;

typedef enum tm_sample_format {
    TM_SAMPLE_U8 = 1,      /* unsigned 8-bit PCM, 128 is zero */
    TM_SAMPLE_S8 = 2,      /* signed 8-bit PCM */
    TM_SAMPLE_S16 = 3,     /* native-endian signed 16-bit PCM */
    TM_SAMPLE_S24LE = 4,   /* packed little-endian signed 24-bit PCM, 3 bytes/sample */
    TM_SAMPLE_S32 = 5      /* native-endian signed 32-bit PCM */
} tm_sample_format_t;

typedef struct tm_format {
    uint32_t sample_rate;     /* e.g. 48000 */
    uint16_t channels;        /* 1..TM_MAX_CHANNELS */
    uint16_t reserved;
} tm_format_t;

typedef struct tm_config {
    tm_format_t format;

    uint16_t max_streams;          /* number of simultaneously registered streams */
    uint16_t mix_block_frames;     /* output-thread block size and tm_mix() upper limit */
    uint32_t stream_buffer_frames; /* per-stream ring-buffer capacity in frames */

    int16_t master_gain_q15;       /* 32767 = unity, 16384 = -6 dB approx */

    uint8_t use_output_thread;     /* 1: tm_start() runs port write loop; 0: caller uses tm_mix() */
    uint8_t clear_output_on_underrun; /* reserved; mixer outputs silence for missing data */
    uint16_t reserved0;

    uint32_t port_flags;           /* forwarded to ThinMixPort implementation */
    void *port_user;               /* optional platform-specific config pointer */
} tm_config_t;

typedef struct tm_stream_config {
    int16_t gain_q15;              /* per-stream gain, 32767 = unity */
    uint8_t start_enabled;         /* nonzero: stream contributes to mix */
    uint8_t reserved0;
    uint16_t channels;             /* TM_CHANNELS_MATCH, TM_CHANNELS_MONO, or TM_CHANNELS_STEREO */
    tm_sample_format_t sample_format; /* input format used by tm_stream_write() */
} tm_stream_config_t;

typedef struct tm_context tm_context_t;
typedef struct tm_stream tm_stream_t;

const char *tm_version(void);
const char *tm_result_string(tm_result_t result);
const char *tm_sample_format_string(tm_sample_format_t fmt);
uint16_t tm_sample_format_bytes(tm_sample_format_t fmt);

void tm_config_default(tm_config_t *cfg);
void tm_stream_config_default(tm_stream_config_t *cfg);

tm_result_t tm_create(const tm_config_t *cfg, tm_context_t **out_ctx);
void tm_destroy(tm_context_t *ctx);

tm_result_t tm_start(tm_context_t *ctx);
tm_result_t tm_stop(tm_context_t *ctx);
int tm_is_running(const tm_context_t *ctx);

/*
    Pull-mode mixing. Useful for audio callbacks, DMA refill hooks, or builds
    that do not want ThinMix to own an output thread.

    frames must be <= cfg.mix_block_frames.
*/
tm_result_t tm_mix(tm_context_t *ctx, int16_t *out_interleaved, uint32_t frames);

tm_result_t tm_create_stream(tm_context_t *ctx,
                             const tm_stream_config_t *cfg,
                             tm_stream_t **out_stream);
void tm_destroy_stream(tm_stream_t *stream);

/*
    Write PCM frames to a stream.

    The input buffer is interpreted according to the stream's configuration:
        tm_stream_config_t::channels
        tm_stream_config_t::sample_format

    Example:
        - mono s8 stream:   interleaved points to int8_t[frames]
        - stereo s16 stream: interleaved points to int16_t[frames * 2]
        - stereo s24 stream: interleaved points to packed 3-byte samples

    Conversion to ThinMix's internal s16 buffer is transparent to the caller.
*/
tm_result_t tm_stream_write(tm_stream_t *stream,
                            const void *interleaved,
                            uint32_t frames,
                            tm_write_mode_t mode,
                            uint32_t timeout_ms,
                            uint32_t *frames_written);

void tm_stream_clear(tm_stream_t *stream);
void tm_stream_set_enabled(tm_stream_t *stream, int enabled);
int tm_stream_is_enabled(const tm_stream_t *stream);

void tm_stream_set_gain_q15(tm_stream_t *stream, int16_t gain_q15);
int16_t tm_stream_get_gain_q15(const tm_stream_t *stream);

uint16_t tm_stream_get_channels(const tm_stream_t *stream);
tm_sample_format_t tm_stream_get_sample_format(const tm_stream_t *stream);

uint32_t tm_stream_get_queued_frames(const tm_stream_t *stream);
uint32_t tm_stream_get_free_frames(const tm_stream_t *stream);

void tm_set_master_gain_q15(tm_context_t *ctx, int16_t gain_q15);
int16_t tm_get_master_gain_q15(const tm_context_t *ctx);

/*
    Standalone integer channel converter for signed 16-bit interleaved PCM.

    Supported conversions:
        N -> N passthrough for identical channel counts
        1 -> 2 mono-to-stereo duplication
        2 -> 1 stereo-to-mono averaging

    stereo -> mono uses (L + R) / 2 with int32 intermediate.
    mono -> stereo duplicates the mono sample to both L and R.
*/
tm_result_t tm_convert_channels_s16(const int16_t *src,
                                    uint16_t src_channels,
                                    int16_t *dst,
                                    uint16_t dst_channels,
                                    uint32_t frames);

/*
    Standalone integer PCM bit-depth / sample-format converter.

    sample_count is the total number of samples, not frames. For stereo with
    128 frames, pass sample_count = 256.

    Supported formats:
        TM_SAMPLE_U8
        TM_SAMPLE_S8
        TM_SAMPLE_S16
        TM_SAMPLE_S24LE
        TM_SAMPLE_S32

    TM_SAMPLE_S24LE is packed as 3 bytes per sample, little-endian two's-complement.
    S16 and S32 are native-endian C integer values.
*/
tm_result_t tm_convert_sample_format(const void *src,
                                     tm_sample_format_t src_format,
                                     void *dst,
                                     tm_sample_format_t dst_format,
                                     uint32_t sample_count);

#ifdef __cplusplus
}
#endif

#endif /* THINMIX_H */
