#include "ThinMix.h"
#include <stdint.h>
#include <stdio.h>

int main() {
    int8_t s8[3] = {-128, 0, 127};
    int16_t s16[3] = {0,0,0};
    uint8_t u8[3] = {0,0,0};
    uint8_t s24[9] = {0};
    int32_t s32[3] = {0,0,0};

    if (tm_convert_sample_format(s8, TM_SAMPLE_S8, s16, TM_SAMPLE_S16, 3) != TM_OK) return 1;
    if (s16[0] != -32768 || s16[1] != 0 || s16[2] != 32512) {
        printf("s8->s16 failed: %d %d %d\n", s16[0], s16[1], s16[2]);
        return 2;
    }

    if (tm_convert_sample_format(s16, TM_SAMPLE_S16, u8, TM_SAMPLE_U8, 3) != TM_OK) return 3;
    if (u8[0] != 0 || u8[1] != 128 || u8[2] != 255) {
        printf("s16->u8 failed: %u %u %u\n", u8[0], u8[1], u8[2]);
        return 4;
    }

    if (tm_convert_sample_format(s16, TM_SAMPLE_S16, s24, TM_SAMPLE_S24LE, 3) != TM_OK) return 5;
    if (tm_convert_sample_format(s24, TM_SAMPLE_S24LE, s32, TM_SAMPLE_S32, 3) != TM_OK) return 6;
    if (s32[0] != (int32_t)0x80000000 || s32[1] != 0 || s32[2] != 2130706432) {
        printf("s24->s32 failed: %d %d %d\n", s32[0], s32[1], s32[2]);
        return 7;
    }


    tm_config_t cfg;
    tm_config_default(&cfg);
    cfg.use_output_thread = 0;
    cfg.format.channels = TM_CHANNELS_STEREO;
    cfg.mix_block_frames = 4;
    cfg.stream_buffer_frames = 8;
    cfg.max_streams = 1;

    tm_context_t *ctx = 0;
    if (tm_create(&cfg, &ctx) != TM_OK) return 8;

    tm_stream_config_t scfg;
    tm_stream_config_default(&scfg);
    scfg.channels = TM_CHANNELS_MONO;
    scfg.sample_format = TM_SAMPLE_S8;
    scfg.gain_q15 = TM_Q15_ONE;

    tm_stream_t *stream = 0;
    if (tm_create_stream(ctx, &scfg, &stream) != TM_OK) return 9;

    int8_t mono_s8[4] = {-128, 0, 127, 64};
    uint32_t written = 0;
    if (tm_stream_write(stream, mono_s8, 4, TM_WRITE_BLOCK, 0, &written) != TM_OK) return 10;
    if (written != 4) return 11;

    int16_t out[8] = {0};
    if (tm_mix(ctx, out, 4) != TM_OK) return 12;

    if (!(out[0] < -32000 && out[1] < -32000 &&
          out[2] == 0 && out[3] == 0 &&
          out[4] > 32000 && out[5] > 32000 &&
          out[6] > 15000 && out[7] > 15000)) {
        printf("stream s8 mono write/mix failed: %d %d %d %d %d %d %d %d\n",
               out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7]);
        return 13;
    }

    tm_destroy(ctx);

    printf("pcm convert and stream write ok\n");
    return 0;
}
