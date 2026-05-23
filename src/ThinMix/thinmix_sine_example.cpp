#include "ThinMix.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <thread>
#include <chrono>

static int16_t clip_s16(int v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static int8_t clip_s8(int v) {
    if (v > 127) return 127;
    if (v < -128) return -128;
    return (int8_t)v;
}

int main(void) {
    tm_config_t cfg;
    tm_config_default(&cfg);
    cfg.format.sample_rate = 48000;
    cfg.format.channels = TM_CHANNELS_STEREO; /* output is stereo s16 */
    cfg.mix_block_frames = 128;
    cfg.stream_buffer_frames = 2048;
    cfg.max_streams = 4;

    tm_context_t *tm = 0;
    tm_result_t r = tm_create(&cfg, &tm);
    if (r != TM_OK) {
        printf("tm_create failed: %s\n", tm_result_string(r));
        return 1;
    }

    tm_stream_t *mono_s8_stream = 0;
    tm_stream_t *stereo_s16_stream = 0;

    tm_stream_config_t mono_cfg;
    tm_stream_config_default(&mono_cfg);
    mono_cfg.channels = TM_CHANNELS_MONO;        /* mono input, duplicated to stereo output */
    mono_cfg.sample_format = TM_SAMPLE_S8;       /* signed 8-bit input */
    mono_cfg.gain_q15 = TM_Q15_HALF;

    tm_stream_config_t stereo_cfg;
    tm_stream_config_default(&stereo_cfg);
    stereo_cfg.channels = TM_CHANNELS_STEREO;    /* stereo input, mixed directly */
    stereo_cfg.sample_format = TM_SAMPLE_S16;    /* signed 16-bit input */
    stereo_cfg.gain_q15 = TM_Q15_HALF;

    tm_create_stream(tm, &mono_cfg, &mono_s8_stream);
    tm_create_stream(tm, &stereo_cfg, &stereo_s16_stream);

    printf("mono stream: %u ch, %s\n",
           (unsigned)tm_stream_get_channels(mono_s8_stream),
           tm_sample_format_string(tm_stream_get_sample_format(mono_s8_stream)));
    printf("stereo stream: %u ch, %s\n",
           (unsigned)tm_stream_get_channels(stereo_s16_stream),
           tm_sample_format_string(tm_stream_get_sample_format(stereo_s16_stream)));

    r = tm_start(tm);
    if (r != TM_OK) {
        printf("tm_start failed: %s\n", tm_result_string(r));
        tm_destroy(tm);
        return 1;
    }

    const uint32_t frames = 128;
    int8_t mono_s8_block[128];       /* 128 frames * 1 channel, signed 8-bit */
    int16_t stereo_s16_block[128 * 2];/* 128 frames * 2 channels, signed 16-bit */

    double ph_mono = 0.0;
    double ph_left = 0.0;
    double ph_right = 0.0;

    const double sr = 48000.0;
    const double pi2 = 2.0 * 3.14159265358979323846;
    const double step_mono = pi2 * 440.0 / sr;
    const double step_left = pi2 * 660.0 / sr;
    const double step_right = pi2 * 880.0 / sr;

    const uint32_t total_blocks = (48000u * 5u) / frames;
    for (uint32_t n = 0; n < total_blocks; ++n) {
        for (uint32_t i = 0; i < frames; ++i) {
            const int8_t vm = clip_s8((int)(sin(ph_mono) * 90.0));
            const int16_t vl = clip_s16((int)(sin(ph_left) * 12000.0));
            const int16_t vr = clip_s16((int)(sin(ph_right) * 12000.0));

            mono_s8_block[i] = vm;
            stereo_s16_block[i * 2u + 0u] = vl;
            stereo_s16_block[i * 2u + 1u] = vr;

            ph_mono += step_mono;
            ph_left += step_left;
            ph_right += step_right;
        }

        uint32_t written = 0;

        tm_stream_write(mono_s8_stream,
                            mono_s8_block,
                            frames,
                            TM_WRITE_BLOCK,
                            TM_TIMEOUT_INFINITE,
                            &written);

        tm_stream_write(stereo_s16_stream,
                        stereo_s16_block,
                        frames,
                        TM_WRITE_BLOCK,
                        TM_TIMEOUT_INFINITE,
                        &written);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    tm_stop(tm);
    tm_destroy(tm);
    return 0;
}
