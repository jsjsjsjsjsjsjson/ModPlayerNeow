#include "ThinMix.h"
#include "ThinMixPort.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>

struct tm_stream {
    tm_context_t *owner;
    uint32_t index;

    int16_t *buffer;              /* internal s16 ring buffer */
    uint32_t capacity_frames;
    uint16_t channels;
    uint16_t reserved0;
    tm_sample_format_t sample_format; /* external input format for tm_stream_write() */
    uint32_t read_frame;
    uint32_t write_frame;
    uint32_t used_frames;

    int16_t gain_q15;
    bool enabled;
    bool alive;

    mutable std::mutex mutex;
    std::condition_variable cv_space;
};

struct tm_context {
    tm_config_t cfg;
    tm_port_device_t *port;
    bool port_opened;

    tm_stream_t **streams;
    uint16_t stream_count;

    int32_t *mix_accum;
    int16_t *mix_out;

    std::atomic<int> master_gain_q15;
    std::atomic<bool> running;
    std::thread *thread;

    mutable std::mutex registry_mutex;
};

static uint32_t tm_min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

static int16_t tm_clip_s16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static int32_t tm_clip_s8_i32(int32_t v) {
    if (v > 127) return 127;
    if (v < -128) return -128;
    return v;
}

static int32_t tm_clip_u8_i32(int32_t v) {
    if (v > 255) return 255;
    if (v < 0) return 0;
    return v;
}

static int32_t tm_clip_s24_i32(int32_t v) {
    if (v > 8388607) return 8388607;
    if (v < -8388608) return -8388608;
    return v;
}

static bool tm_sample_format_is_valid(tm_sample_format_t fmt) {
    switch (fmt) {
        case TM_SAMPLE_U8:
        case TM_SAMPLE_S8:
        case TM_SAMPLE_S16:
        case TM_SAMPLE_S24LE:
        case TM_SAMPLE_S32:
            return true;
        default:
            return false;
    }
}

static int tm_channels_are_supported_for_conversion(uint16_t in_ch, uint16_t out_ch) {
    if (in_ch == 0 || out_ch == 0) return 0;
    if (in_ch == out_ch) return 1;
    if ((in_ch == TM_CHANNELS_MONO || in_ch == TM_CHANNELS_STEREO) &&
        (out_ch == TM_CHANNELS_MONO || out_ch == TM_CHANNELS_STEREO)) {
        return 1;
    }
    return 0;
}

static int32_t tm_read_s24le_to_s32norm(const uint8_t *p) {
    int32_t v = ((int32_t)p[0]) | ((int32_t)p[1] << 8) | ((int32_t)p[2] << 16);
    if (v & 0x00800000) {
        v |= (int32_t)0xff000000;
    }
    return v * 256;
}

static void tm_write_s24le_from_s32norm(uint8_t *p, int32_t v) {
    int32_t s24 = v / 256;
    s24 = tm_clip_s24_i32(s24);
    p[0] = (uint8_t)(s24 & 0xff);
    p[1] = (uint8_t)((s24 >> 8) & 0xff);
    p[2] = (uint8_t)((s24 >> 16) & 0xff);
}

/*
    Convert one source sample to signed 32-bit normalized PCM.

    The normalized domain is:
        s8  << 24
        u8  centered at 128, then << 24
        s16 << 16
        s24 << 8
        s32 unchanged
*/
static int32_t tm_load_sample_s32norm(const uint8_t *src,
                                      tm_sample_format_t fmt,
                                      uint32_t sample_index) {
    switch (fmt) {
        case TM_SAMPLE_U8: {
            const uint8_t v = src[sample_index];
            return ((int32_t)v - 128) * 16777216;
        }
        case TM_SAMPLE_S8: {
            const int8_t v = ((const int8_t *)src)[sample_index];
            return ((int32_t)v) * 16777216;
        }
        case TM_SAMPLE_S16: {
            const int16_t v = ((const int16_t *)src)[sample_index];
            return ((int32_t)v) * 65536;
        }
        case TM_SAMPLE_S24LE: {
            const uint8_t *p = src + (size_t)sample_index * 3u;
            return tm_read_s24le_to_s32norm(p);
        }
        case TM_SAMPLE_S32: {
            return ((const int32_t *)src)[sample_index];
        }
        default:
            return 0;
    }
}

static int16_t tm_load_sample_as_s16(const uint8_t *src,
                                     tm_sample_format_t fmt,
                                     uint32_t sample_index) {
    const int32_t v = tm_load_sample_s32norm(src, fmt, sample_index);
    return tm_clip_s16(v / 65536);
}

static void tm_store_sample_from_s32norm(uint8_t *dst,
                                         tm_sample_format_t fmt,
                                         uint32_t sample_index,
                                         int32_t v) {
    switch (fmt) {
        case TM_SAMPLE_U8: {
            const int32_t u = tm_clip_u8_i32((v / 16777216) + 128);
            dst[sample_index] = (uint8_t)u;
            return;
        }
        case TM_SAMPLE_S8: {
            const int32_t s = tm_clip_s8_i32(v / 16777216);
            ((int8_t *)dst)[sample_index] = (int8_t)s;
            return;
        }
        case TM_SAMPLE_S16: {
            ((int16_t *)dst)[sample_index] = tm_clip_s16(v / 65536);
            return;
        }
        case TM_SAMPLE_S24LE: {
            uint8_t *p = dst + (size_t)sample_index * 3u;
            tm_write_s24le_from_s32norm(p, v);
            return;
        }
        case TM_SAMPLE_S32: {
            ((int32_t *)dst)[sample_index] = v;
            return;
        }
        default:
            return;
    }
}

static void tm_mix_one_frame(int32_t *dst,
                             const int16_t *src,
                             uint16_t src_channels,
                             uint16_t dst_channels,
                             int16_t gain_q15) {
    if (src_channels == dst_channels) {
        for (uint16_t c = 0; c < dst_channels; ++c) {
            const int32_t x = src[c];
            dst[c] += (x * gain_q15) >> 15;
        }
        return;
    }

    if (src_channels == TM_CHANNELS_MONO && dst_channels == TM_CHANNELS_STEREO) {
        const int32_t x = (src[0] * gain_q15) >> 15;
        dst[0] += x;
        dst[1] += x;
        return;
    }

    if (src_channels == TM_CHANNELS_STEREO && dst_channels == TM_CHANNELS_MONO) {
        const int32_t mono = ((int32_t)src[0] + (int32_t)src[1]) / 2;
        dst[0] += (mono * gain_q15) >> 15;
        return;
    }
}

static void tm_output_thread_main(tm_context_t *ctx) {
    const uint32_t frames = ctx->cfg.mix_block_frames;

    while (ctx->running.load(std::memory_order_acquire)) {
        if (tm_mix(ctx, ctx->mix_out, frames) != TM_OK) {
            std::memset(ctx->mix_out, 0,
                        (size_t)frames * ctx->cfg.format.channels * sizeof(int16_t));
        }

        if (tm_port_write(ctx->port, ctx->mix_out, frames) < 0) {
            ctx->running.store(false, std::memory_order_release);
            break;
        }
    }
}

const char *tm_version(void) {
    return "ThinMix 0.5.2";
}

const char *tm_result_string(tm_result_t result) {
    switch (result) {
        case TM_OK: return "OK";
        case TM_ERR_INVALID: return "invalid argument";
        case TM_ERR_NOMEM: return "out of memory";
        case TM_ERR_STATE: return "invalid state";
        case TM_ERR_TIMEOUT: return "timeout";
        case TM_ERR_PORT: return "audio port error";
        default: return "unknown error";
    }
}

const char *tm_sample_format_string(tm_sample_format_t fmt) {
    switch (fmt) {
        case TM_SAMPLE_U8: return "u8";
        case TM_SAMPLE_S8: return "s8";
        case TM_SAMPLE_S16: return "s16";
        case TM_SAMPLE_S24LE: return "s24le";
        case TM_SAMPLE_S32: return "s32";
        default: return "unknown";
    }
}

uint16_t tm_sample_format_bytes(tm_sample_format_t fmt) {
    switch (fmt) {
        case TM_SAMPLE_U8: return 1;
        case TM_SAMPLE_S8: return 1;
        case TM_SAMPLE_S16: return 2;
        case TM_SAMPLE_S24LE: return 3;
        case TM_SAMPLE_S32: return 4;
        default: return 0;
    }
}

void tm_config_default(tm_config_t *cfg) {
    if (!cfg) return;
    std::memset(cfg, 0, sizeof(*cfg));
    cfg->format.sample_rate = 48000;
    cfg->format.channels = 2;
    cfg->max_streams = 8;
    cfg->mix_block_frames = 128;
    cfg->stream_buffer_frames = 1024;
    cfg->master_gain_q15 = TM_Q15_ONE;
    cfg->use_output_thread = 1;
    cfg->clear_output_on_underrun = 1;
    cfg->port_flags = 0;
    cfg->port_user = 0;
}

void tm_stream_config_default(tm_stream_config_t *cfg) {
    if (!cfg) return;
    std::memset(cfg, 0, sizeof(*cfg));
    cfg->gain_q15 = TM_Q15_ONE;
    cfg->start_enabled = 1;
    cfg->channels = TM_CHANNELS_MATCH;
    cfg->sample_format = TM_SAMPLE_S16;
}

static bool tm_validate_config(const tm_config_t *cfg) {
    if (!cfg) return false;
    if (cfg->format.sample_rate == 0) return false;
    if (cfg->format.channels == 0 || cfg->format.channels > TM_MAX_CHANNELS) return false;
    if (cfg->max_streams == 0) return false;
    if (cfg->mix_block_frames == 0) return false;
    if (cfg->stream_buffer_frames == 0) return false;
    return true;
}

tm_result_t tm_create(const tm_config_t *cfg, tm_context_t **out_ctx) {
    if (!out_ctx || !tm_validate_config(cfg)) return TM_ERR_INVALID;
    *out_ctx = 0;

    tm_context_t *ctx = new (std::nothrow) tm_context_t;
    if (!ctx) return TM_ERR_NOMEM;

    ctx->cfg = *cfg;
    ctx->port = 0;
    ctx->port_opened = false;
    ctx->stream_count = 0;
    ctx->master_gain_q15.store((int)ctx->cfg.master_gain_q15, std::memory_order_release);
    ctx->running.store(false, std::memory_order_release);
    ctx->thread = 0;

    ctx->streams = new (std::nothrow) tm_stream_t *[ctx->cfg.max_streams];
    ctx->mix_accum = new (std::nothrow) int32_t[(size_t)ctx->cfg.mix_block_frames * ctx->cfg.format.channels];
    ctx->mix_out = new (std::nothrow) int16_t[(size_t)ctx->cfg.mix_block_frames * ctx->cfg.format.channels];

    if (!ctx->streams || !ctx->mix_accum || !ctx->mix_out) {
        delete[] ctx->streams;
        delete[] ctx->mix_accum;
        delete[] ctx->mix_out;
        delete ctx;
        return TM_ERR_NOMEM;
    }

    for (uint16_t i = 0; i < ctx->cfg.max_streams; ++i) {
        ctx->streams[i] = 0;
    }

    *out_ctx = ctx;
    return TM_OK;
}

void tm_destroy(tm_context_t *ctx) {
    if (!ctx) return;

    tm_stop(ctx);

    {
        std::lock_guard<std::mutex> lock(ctx->registry_mutex);
        for (uint16_t i = 0; i < ctx->cfg.max_streams; ++i) {
            tm_stream_t *s = ctx->streams[i];
            if (!s) continue;
            {
                std::lock_guard<std::mutex> slock(s->mutex);
                s->alive = false;
                s->enabled = false;
            }
            s->cv_space.notify_all();
            delete[] s->buffer;
            delete s;
            ctx->streams[i] = 0;
        }
        ctx->stream_count = 0;
    }

    delete[] ctx->streams;
    delete[] ctx->mix_accum;
    delete[] ctx->mix_out;
    delete ctx;
}

tm_result_t tm_start(tm_context_t *ctx) {
    if (!ctx) return TM_ERR_INVALID;
    if (!ctx->cfg.use_output_thread) return TM_ERR_STATE;
    if (ctx->running.load(std::memory_order_acquire)) return TM_OK;
    if (ctx->thread) return TM_ERR_STATE;

    tm_port_config_t pcfg;
    std::memset(&pcfg, 0, sizeof(pcfg));
    pcfg.format = ctx->cfg.format;
    pcfg.block_frames = ctx->cfg.mix_block_frames;
    pcfg.flags = ctx->cfg.port_flags;
    pcfg.user = ctx->cfg.port_user;

    if (tm_port_open(&ctx->port, &pcfg) < 0 || !ctx->port) {
        ctx->port = 0;
        return TM_ERR_PORT;
    }
    ctx->port_opened = true;

    if (tm_port_start(ctx->port) < 0) {
        tm_port_close(ctx->port);
        ctx->port = 0;
        ctx->port_opened = false;
        return TM_ERR_PORT;
    }

    ctx->running.store(true, std::memory_order_release);

#ifndef THINMIX_NO_EXCEPTIONS
    try {
        ctx->thread = new std::thread(tm_output_thread_main, ctx);
    } catch (...) {
        ctx->running.store(false, std::memory_order_release);
        tm_port_stop(ctx->port);
        tm_port_close(ctx->port);
        ctx->port = 0;
        ctx->port_opened = false;
        ctx->thread = 0;
        return TM_ERR_NOMEM;
    }
#else
    ctx->thread = new (std::nothrow) std::thread(tm_output_thread_main, ctx);
#endif

    if (!ctx->thread) {
        ctx->running.store(false, std::memory_order_release);
        tm_port_stop(ctx->port);
        tm_port_close(ctx->port);
        ctx->port = 0;
        ctx->port_opened = false;
        return TM_ERR_NOMEM;
    }

    return TM_OK;
}

tm_result_t tm_stop(tm_context_t *ctx) {
    if (!ctx) return TM_ERR_INVALID;

    const bool was_running = ctx->running.exchange(false, std::memory_order_acq_rel);
    if (!was_running && !ctx->thread && !ctx->port_opened) return TM_OK;

    if (ctx->port_opened && ctx->port) {
        /* The port should make this unblock a pending tm_port_write() if possible. */
        tm_port_stop(ctx->port);
    }

    if (ctx->thread) {
        if (ctx->thread->joinable()) ctx->thread->join();
        delete ctx->thread;
        ctx->thread = 0;
    }

    if (ctx->port_opened && ctx->port) {
        tm_port_close(ctx->port);
        ctx->port = 0;
        ctx->port_opened = false;
    }

    return TM_OK;
}

int tm_is_running(const tm_context_t *ctx) {
    if (!ctx) return 0;
    return ctx->running.load(std::memory_order_acquire) ? 1 : 0;
}

tm_result_t tm_mix(tm_context_t *ctx, int16_t *out_interleaved, uint32_t frames) {
    if (!ctx || !out_interleaved) return TM_ERR_INVALID;
    if (frames == 0 || frames > ctx->cfg.mix_block_frames) return TM_ERR_INVALID;

    const uint16_t out_channels = ctx->cfg.format.channels;
    const size_t out_sample_count = (size_t)frames * out_channels;

    std::memset(ctx->mix_accum, 0, out_sample_count * sizeof(int32_t));

    {
        std::lock_guard<std::mutex> rlock(ctx->registry_mutex);

        for (uint16_t si = 0; si < ctx->cfg.max_streams; ++si) {
            tm_stream_t *s = ctx->streams[si];
            if (!s) continue;

            uint32_t consumed = 0;
            {
                std::lock_guard<std::mutex> slock(s->mutex);
                if (!s->alive || !s->enabled || s->used_frames == 0) continue;

                const uint32_t todo = tm_min_u32(frames, s->used_frames);
                consumed = todo;
                const int16_t gain = s->gain_q15;
                const uint16_t src_channels = s->channels;

                for (uint32_t f = 0; f < todo; ++f) {
                    const uint32_t src_frame = s->read_frame;
                    const size_t src_base = (size_t)src_frame * src_channels;
                    const size_t dst_base = (size_t)f * out_channels;

                    tm_mix_one_frame(&ctx->mix_accum[dst_base],
                                     &s->buffer[src_base],
                                     src_channels,
                                     out_channels,
                                     gain);

                    s->read_frame++;
                    if (s->read_frame >= s->capacity_frames) s->read_frame = 0;
                }

                s->used_frames -= consumed;
            }

            if (consumed) s->cv_space.notify_all();
        }
    }

    const int master = ctx->master_gain_q15.load(std::memory_order_acquire);
    for (size_t i = 0; i < out_sample_count; ++i) {
        int32_t v = (ctx->mix_accum[i] * master) >> 15;
        out_interleaved[i] = tm_clip_s16(v);
    }

    return TM_OK;
}

tm_result_t tm_create_stream(tm_context_t *ctx,
                             const tm_stream_config_t *cfg,
                             tm_stream_t **out_stream) {
    if (!ctx || !out_stream) return TM_ERR_INVALID;
    *out_stream = 0;

    tm_stream_config_t local_cfg;
    if (cfg) local_cfg = *cfg;
    else tm_stream_config_default(&local_cfg);

    uint16_t stream_channels = local_cfg.channels;
    if (stream_channels == TM_CHANNELS_MATCH) {
        stream_channels = ctx->cfg.format.channels;
    }

    if (!tm_channels_are_supported_for_conversion(stream_channels, ctx->cfg.format.channels)) {
        return TM_ERR_INVALID;
    }

    if (!tm_sample_format_is_valid(local_cfg.sample_format)) {
        return TM_ERR_INVALID;
    }

    tm_stream_t *s = new (std::nothrow) tm_stream_t;
    if (!s) return TM_ERR_NOMEM;

    s->owner = ctx;
    s->index = 0;
    s->capacity_frames = ctx->cfg.stream_buffer_frames;
    s->channels = stream_channels;
    s->reserved0 = 0;
    s->sample_format = local_cfg.sample_format;
    s->read_frame = 0;
    s->write_frame = 0;
    s->used_frames = 0;
    s->gain_q15 = local_cfg.gain_q15;
    s->enabled = (local_cfg.start_enabled != 0);
    s->alive = true;
    s->buffer = new (std::nothrow) int16_t[(size_t)s->capacity_frames * s->channels];

    if (!s->buffer) {
        delete s;
        return TM_ERR_NOMEM;
    }

    std::memset(s->buffer, 0, (size_t)s->capacity_frames * s->channels * sizeof(int16_t));

    {
        std::lock_guard<std::mutex> lock(ctx->registry_mutex);
        if (ctx->stream_count >= ctx->cfg.max_streams) {
            delete[] s->buffer;
            delete s;
            return TM_ERR_STATE;
        }

        bool placed = false;
        for (uint16_t i = 0; i < ctx->cfg.max_streams; ++i) {
            if (!ctx->streams[i]) {
                ctx->streams[i] = s;
                s->index = i;
                ctx->stream_count++;
                placed = true;
                break;
            }
        }

        if (!placed) {
            delete[] s->buffer;
            delete s;
            return TM_ERR_STATE;
        }
    }

    *out_stream = s;
    return TM_OK;
}

void tm_destroy_stream(tm_stream_t *stream) {
    if (!stream) return;
    tm_context_t *ctx = stream->owner;
    if (!ctx) return;

    {
        std::lock_guard<std::mutex> rlock(ctx->registry_mutex);
        if (stream->index < ctx->cfg.max_streams && ctx->streams[stream->index] == stream) {
            ctx->streams[stream->index] = 0;
            if (ctx->stream_count) ctx->stream_count--;
        } else {
            for (uint16_t i = 0; i < ctx->cfg.max_streams; ++i) {
                if (ctx->streams[i] == stream) {
                    ctx->streams[i] = 0;
                    if (ctx->stream_count) ctx->stream_count--;
                    break;
                }
            }
        }

        {
            std::lock_guard<std::mutex> slock(stream->mutex);
            stream->alive = false;
            stream->enabled = false;
            stream->used_frames = 0;
        }
    }

    stream->cv_space.notify_all();
    delete[] stream->buffer;
    delete stream;
}

static tm_result_t tm_wait_for_space(tm_stream_t *stream,
                                     std::unique_lock<std::mutex> &lock,
                                     uint32_t timeout_ms) {
    if (timeout_ms == TM_TIMEOUT_INFINITE) {
        stream->cv_space.wait(lock, [stream] {
            return !stream->alive || stream->used_frames < stream->capacity_frames;
        });
        return stream->alive ? TM_OK : TM_ERR_STATE;
    }

    if (timeout_ms == 0) {
        return (stream->used_frames < stream->capacity_frames) ? TM_OK : TM_ERR_TIMEOUT;
    }

    const bool ok = stream->cv_space.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms),
        [stream] { return !stream->alive || stream->used_frames < stream->capacity_frames; });

    if (!stream->alive) return TM_ERR_STATE;
    return ok ? TM_OK : TM_ERR_TIMEOUT;
}

tm_result_t tm_stream_write(tm_stream_t *stream,
                            const void *interleaved,
                            uint32_t frames,
                            tm_write_mode_t mode,
                            uint32_t timeout_ms,
                            uint32_t *frames_written) {
    if (frames_written) *frames_written = 0;
    if (!stream || !interleaved) return TM_ERR_INVALID;
    if (mode != TM_WRITE_ASYNC && mode != TM_WRITE_BLOCK) return TM_ERR_INVALID;
    if (frames == 0) return TM_OK;
    if (!stream->owner) return TM_ERR_STATE;

    const uint8_t *src_bytes = (const uint8_t *)interleaved;
    uint32_t done = 0;
    tm_result_t final_result = TM_OK;

    std::unique_lock<std::mutex> lock(stream->mutex);

    if (!tm_sample_format_is_valid(stream->sample_format)) {
        return TM_ERR_INVALID;
    }

    const uint16_t channels = stream->channels;
    const tm_sample_format_t sample_format = stream->sample_format;

    while (done < frames) {
        if (!stream->alive) {
            final_result = TM_ERR_STATE;
            break;
        }

        uint32_t free_frames = stream->capacity_frames - stream->used_frames;

        if (free_frames == 0) {
            if (mode == TM_WRITE_ASYNC) break;

            final_result = tm_wait_for_space(stream, lock, timeout_ms);
            if (final_result != TM_OK) break;
            free_frames = stream->capacity_frames - stream->used_frames;
            if (free_frames == 0) continue;
        }

        const uint32_t chunk_frames = tm_min_u32(free_frames, frames - done);
        const uint32_t chunk_samples = chunk_frames * channels;

        if (sample_format == TM_SAMPLE_S16 && stream->write_frame + chunk_frames <= stream->capacity_frames) {
            const int16_t *src_s16 = (const int16_t *)interleaved;
            const size_t src_base = (size_t)done * channels;
            const size_t dst_base = (size_t)stream->write_frame * channels;
            std::memcpy(&stream->buffer[dst_base],
                        &src_s16[src_base],
                        (size_t)chunk_samples * sizeof(int16_t));
            stream->write_frame += chunk_frames;
            if (stream->write_frame >= stream->capacity_frames) stream->write_frame = 0;
        } else {
            for (uint32_t f = 0; f < chunk_frames; ++f) {
                const size_t src_base = (size_t)(done + f) * channels;
                const size_t dst_base = (size_t)stream->write_frame * channels;

                if (sample_format == TM_SAMPLE_S16) {
                    const int16_t *src_s16 = (const int16_t *)interleaved;
                    for (uint16_t c = 0; c < channels; ++c) {
                        stream->buffer[dst_base + c] = src_s16[src_base + c];
                    }
                } else {
                    for (uint16_t c = 0; c < channels; ++c) {
                        stream->buffer[dst_base + c] = tm_load_sample_as_s16(
                            src_bytes, sample_format, (uint32_t)(src_base + c));
                    }
                }

                stream->write_frame++;
                if (stream->write_frame >= stream->capacity_frames) stream->write_frame = 0;
            }
        }

        stream->used_frames += chunk_frames;
        done += chunk_frames;

        if (mode == TM_WRITE_ASYNC) break;
    }

    if (frames_written) *frames_written = done;

    if (mode == TM_WRITE_BLOCK && done < frames && final_result == TM_OK) {
        final_result = TM_ERR_TIMEOUT;
    }

    return final_result;
}

void tm_stream_clear(tm_stream_t *stream) {
    if (!stream) return;
    {
        std::lock_guard<std::mutex> lock(stream->mutex);
        stream->read_frame = 0;
        stream->write_frame = 0;
        stream->used_frames = 0;
    }
    stream->cv_space.notify_all();
}

void tm_stream_set_enabled(tm_stream_t *stream, int enabled) {
    if (!stream) return;
    std::lock_guard<std::mutex> lock(stream->mutex);
    stream->enabled = (enabled != 0);
}

int tm_stream_is_enabled(const tm_stream_t *stream) {
    if (!stream) return 0;
    std::lock_guard<std::mutex> lock(stream->mutex);
    return stream->enabled ? 1 : 0;
}

void tm_stream_set_gain_q15(tm_stream_t *stream, int16_t gain_q15) {
    if (!stream) return;
    std::lock_guard<std::mutex> lock(stream->mutex);
    stream->gain_q15 = gain_q15;
}

int16_t tm_stream_get_gain_q15(const tm_stream_t *stream) {
    if (!stream) return 0;
    std::lock_guard<std::mutex> lock(stream->mutex);
    return stream->gain_q15;
}

uint16_t tm_stream_get_channels(const tm_stream_t *stream) {
    if (!stream) return 0;
    std::lock_guard<std::mutex> lock(stream->mutex);
    return stream->channels;
}

tm_sample_format_t tm_stream_get_sample_format(const tm_stream_t *stream) {
    if (!stream) return (tm_sample_format_t)0;
    std::lock_guard<std::mutex> lock(stream->mutex);
    return stream->sample_format;
}

uint32_t tm_stream_get_queued_frames(const tm_stream_t *stream) {
    if (!stream) return 0;
    std::lock_guard<std::mutex> lock(stream->mutex);
    return stream->used_frames;
}

uint32_t tm_stream_get_free_frames(const tm_stream_t *stream) {
    if (!stream) return 0;
    std::lock_guard<std::mutex> lock(stream->mutex);
    return stream->capacity_frames - stream->used_frames;
}

void tm_set_master_gain_q15(tm_context_t *ctx, int16_t gain_q15) {
    if (!ctx) return;
    ctx->master_gain_q15.store((int)gain_q15, std::memory_order_release);
}

int16_t tm_get_master_gain_q15(const tm_context_t *ctx) {
    if (!ctx) return 0;
    return (int16_t)ctx->master_gain_q15.load(std::memory_order_acquire);
}

tm_result_t tm_convert_channels_s16(const int16_t *src,
                                    uint16_t src_channels,
                                    int16_t *dst,
                                    uint16_t dst_channels,
                                    uint32_t frames) {
    if (!src || !dst) return TM_ERR_INVALID;
    if (frames == 0) return TM_OK;
    if (!tm_channels_are_supported_for_conversion(src_channels, dst_channels)) {
        return TM_ERR_INVALID;
    }

    if (src_channels == dst_channels) {
        std::memmove(dst, src, (size_t)frames * src_channels * sizeof(int16_t));
        return TM_OK;
    }

    if (src_channels == TM_CHANNELS_MONO && dst_channels == TM_CHANNELS_STEREO) {
        for (uint32_t f = 0; f < frames; ++f) {
            const int16_t m = src[f];
            dst[(size_t)f * 2u + 0u] = m;
            dst[(size_t)f * 2u + 1u] = m;
        }
        return TM_OK;
    }

    if (src_channels == TM_CHANNELS_STEREO && dst_channels == TM_CHANNELS_MONO) {
        for (uint32_t f = 0; f < frames; ++f) {
            const int32_t l = src[(size_t)f * 2u + 0u];
            const int32_t r = src[(size_t)f * 2u + 1u];
            dst[f] = (int16_t)((l + r) / 2);
        }
        return TM_OK;
    }

    return TM_ERR_INVALID;
}

tm_result_t tm_convert_sample_format(const void *src,
                                     tm_sample_format_t src_format,
                                     void *dst,
                                     tm_sample_format_t dst_format,
                                     uint32_t sample_count) {
    if (!src || !dst) return TM_ERR_INVALID;
    if (sample_count == 0) return TM_OK;
    if (!tm_sample_format_is_valid(src_format) || !tm_sample_format_is_valid(dst_format)) {
        return TM_ERR_INVALID;
    }

    if (src == dst && src_format == dst_format) {
        return TM_OK;
    }

    const uint8_t *src_bytes = (const uint8_t *)src;
    uint8_t *dst_bytes = (uint8_t *)dst;

    if (src_format == dst_format) {
        std::memmove(dst_bytes, src_bytes,
                     (size_t)sample_count * tm_sample_format_bytes(src_format));
        return TM_OK;
    }

    /*
        In-place conversion between different byte widths is not supported.
        It would need direction-aware resizing and would be error-prone for s24.
    */
    if (src == dst) {
        return TM_ERR_INVALID;
    }

    for (uint32_t i = 0; i < sample_count; ++i) {
        const int32_t v = tm_load_sample_s32norm(src_bytes, src_format, i);
        tm_store_sample_from_s32norm(dst_bytes, dst_format, i, v);
    }

    return TM_OK;
}
