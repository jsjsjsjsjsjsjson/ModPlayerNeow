#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "mod_file.h"
#include "mod_player.hpp"
#include "ThinMix.h"

#define SAMP_RATE 48000
#define BUF_FRAMES 128

MOD_FILE mod;

int main() {
    tm_config_t cfg;
    tm_config_default(&cfg);

    cfg.format.sample_rate = SAMP_RATE;
    cfg.format.channels = 2;
    cfg.max_streams = 4;
    cfg.mix_block_frames = 128;
    cfg.stream_buffer_frames = 2048;
    cfg.master_gain_q15 = TM_Q15_ONE;

    tm_context_t *tm = NULL;
    tm_result_t r = tm_create(&cfg, &tm);
    if (r != TM_OK) {
        printf("tm_create failed: %s\n", tm_result_string(r));
        return 1;
    }

    r = tm_start(tm);
    if (r != TM_OK) {
        printf("tm_start failed: %s\n", tm_result_string(r));
        tm_destroy(tm);
        return 1;
    }

    char err_buf[64];

    if (mod.open("../urea.mod")) {
        tm_destroy(tm);
        return -2;
    }

    if (mod.read(err_buf, sizeof(err_buf))) {
        printf("ERR: %s\n", err_buf);
        tm_destroy(tm);
        return -1;
    }

    tm_stream_config_t scfg;
    tm_stream_config_default(&scfg);

    scfg.gain_q15 = TM_Q15_HALF;
    scfg.channels = TM_CHANNELS_MONO;
    scfg.sample_format = TM_SAMPLE_S16;

    tm_stream_t *stream = NULL;
    r = tm_create_stream(tm, &scfg, &stream);
    if (r != TM_OK) {
        printf("tm_create_stream failed: %s\n", tm_result_string(r));
        tm_destroy(tm);
        return 1;
    }

    MOD_TRACKER tracker;
    tracker.init_mod(&mod);
    tracker.set_sample_rate(SAMP_RATE);
    tracker.set_tempo(125);
    tracker.set_ticks_row(6);
    tracker.start();

    int16_t buf[BUF_FRAMES];
    uint32_t written = 0;

    while (tracker.get_active()) {
        tracker.process_block(buf, BUF_FRAMES);

        r = tm_stream_write(
            stream,
            buf,
            BUF_FRAMES,
            TM_WRITE_BLOCK,
            TM_TIMEOUT_INFINITE,
            &written
        );

        if (r != TM_OK) {
            printf("tm_stream_write failed after %u/%d frames: %s\n",
                   written,
                   BUF_FRAMES,
                   tm_result_string(r));
            break;
        }
    }

    tm_stop(tm);
    tm_destroy(tm);

    return 0;
}
