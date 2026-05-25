#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "mod_file.h"
#include "mod_player.hpp"
#include "ThinMix.h"
#include "WAV.h"

#include "audio_type.h"

#define SAMP_RATE 48000
#define BUF_FRAMES 256

MOD_FILE mod;

static void print_usage(const char *argv0) {
    printf("Usage:\n");
    printf("  %s [module.mod]\n", argv0);
    printf("  %s [module.mod] --wav <output.wav>\n", argv0);
}

static int parse_args(int argc, char **argv, const char **mod_path, const char **wav_path) {
    int i;

    *mod_path = "ducc_-_our_great_minds.mod";
    *wav_path = NULL;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--wav") == 0) {
            if (i + 1 >= argc) {
                printf("ERR: --wav requires output path\n");
                return -1;
            }

            *wav_path = argv[i + 1];
            i++;
        } else if (argv[i][0] == '-') {
            printf("ERR: unknown option: %s\n", argv[i]);
            return -1;
        } else {
            *mod_path = argv[i];
        }
    }

    return 0;
}

static int load_mod(const char *path) {
    char err_buf[64];

    if (mod.open(path)) {
        printf("ERR: failed to open %s\n", path);
        return -1;
    }

    if (mod.read(err_buf, sizeof(err_buf))) {
        printf("ERR: %s\n", err_buf);
        return -1;
    }

    return 0;
}

static void init_tracker(MOD_TRACKER *tracker) {
    tracker->init_mod(&mod);
    tracker->set_sample_rate(SAMP_RATE);
    tracker->set_tempo(125);
    tracker->set_ticks_row(6);
    tracker->start();
}

static int export_wav(const char *wav_path) {
    MOD_TRACKER tracker;
    WAV wav;
    audio16_t buf[BUF_FRAMES];
    int r;

    init_tracker(&tracker);

    tracker.set_song_loop(0);

    r = wav.open_write(wav_path, SAMP_RATE, 2);
    if (r != WAV_OK) {
        printf("wav.open_write failed: %s\n", WAV::result_string(r));
        return -1;
    }

    while (!tracker.get_song_finished()) {
        tracker.process_block(buf, BUF_FRAMES);

        if (wav.write_frames_s16((int16_t*)buf, BUF_FRAMES) != BUF_FRAMES) {
            printf("wav.write_frames_s16 failed\n");
            wav.close();
            return -1;
        }
    }

    wav.close();

    printf("WAV exported: %s\n", wav_path);
    return 0;
}

static int play_realtime() {
    tm_config_t cfg;
    tm_context_t *tm = NULL;
    tm_result_t r;

    tm_stream_config_t scfg;
    tm_stream_t *stream = NULL;

    MOD_TRACKER tracker;
    audio16_t buf[BUF_FRAMES];
    uint32_t written = 0;

    tm_config_default(&cfg);

    cfg.format.sample_rate = SAMP_RATE;
    cfg.format.channels = 2;
    cfg.max_streams = 4;
    cfg.mix_block_frames = 128;
    cfg.stream_buffer_frames = 2048;
    cfg.master_gain_q15 = TM_Q15_ONE;

    r = tm_create(&cfg, &tm);
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

    tm_stream_config_default(&scfg);

    scfg.gain_q15 = TM_Q15_HALF;
    scfg.channels = TM_CHANNELS_STEREO;
    scfg.sample_format = TM_SAMPLE_S16;

    r = tm_create_stream(tm, &scfg, &stream);
    if (r != TM_OK) {
        printf("tm_create_stream failed: %s\n", tm_result_string(r));
        tm_stop(tm);
        tm_destroy(tm);
        return 1;
    }

    init_tracker(&tracker);

    tracker.set_song_loop(-1);

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

int main(int argc, char **argv) {
    const char *mod_path;
    const char *wav_path;

    if (parse_args(argc, argv, &mod_path, &wav_path)) {
        print_usage(argv[0]);
        return 1;
    }

    if (load_mod(mod_path)) {
        return 1;
    }

    if (wav_path) {
        return export_wav(wav_path);
    }

    return play_realtime();
}
