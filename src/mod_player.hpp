#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <vector>

#include "mod_helper.h"
#include "LFO.h"
#include "mod_file.h"
#include "audio_type.h"

#include "FIRFilter.h"

// #define NTSC_AMIGA

#ifdef NTSC_AMIGA
    #define MASTER_CLOCK_FREQ 3579545.0f
#else
    #define MASTER_CLOCK_FREQ 3546895.0f
#endif

static const uint8_t amiga_pan_default[4] = {
    96,     // ch0 / MOD channel 1: left
    160,    // ch1 / MOD channel 2: right
    160,    // ch2 / MOD channel 3: right
    96      // ch3 / MOD channel 4: left
};

static inline int16_t softclip(int32_t x) {
    if (x > 32767)
        x = 32767;
    else if (x < -32768)
        x = -32768;

    int32_t x2 = (x * x) >> 15;
    int32_t x3 = (x2 * x) >> 15;

    int32_t y = x - (x3 / 3);

    // gain compensation: roughly *1.5
    y = y + (y >> 1);

    if (y > 32767)
        y = 32767;
    else if (y < -32768)
        y = -32768;

    return (int16_t)y;
}

class MOD_CHANNEL {
private:
    uint32_t samp_rate = 0;
    int os_factor = 8; // "os" means "Over Sample"

    mod_sample_t *smp = NULL;

    bool active = false;
    uint8_t vol = 0;
    float freq = 0;
    float period = 0;
    int8_t finetune = 0;

    uint32_t p_i = 0; // Phase Int
    float p_f = 0.0f; // Phase Float
    float p_c = 0;    // Phase Count

    bool disable_loop = false; // For debug or view

    uint8_t invert_loop_speed = 0;
    uint16_t invert_loop_acc = 0;
    uint32_t invert_loop_pos = 0;
    std::vector<uint8_t> invert_loop_flags;

    FIRFilter aa_fir;
    size_t aa_fir_taps = 129;

    int16_t get_sample_value(uint32_t idx) {
        if (smp == NULL) {
            return 0;
        }

        if (idx >= smp->data.size()) {
            return 0;
        }

        int16_t v = smp->data[idx];

        if ((smp->loop_length > 2) && (invert_loop_flags.size() == smp->loop_length)) {
            uint32_t loop_start = smp->loop_start;
            uint32_t loop_end = smp->loop_start + smp->loop_length;

            if (loop_end > smp->data.size()) {
                loop_end = smp->data.size();
            }

            if ((idx >= loop_start) && (idx < loop_end)) {
                uint32_t loop_pos = idx - loop_start;

                if ((loop_pos < invert_loop_flags.size()) && invert_loop_flags[loop_pos]) {
                    v = -1 - v;
                }
            }
        }

        return v;
    }

    void rebuild_invert_loop_flags() {
        invert_loop_flags.clear();
        invert_loop_pos = 0;

        if (smp == NULL) {
            return;
        }

        if (smp->loop_length <= 2) {
            return;
        }

        invert_loop_flags.resize(smp->loop_length, 0);
    }

public:
    MOD_CHANNEL() {
        aa_fir.rebuild_lowpass(aa_fir_taps, os_factor);
    }

    bool get_active() {
        return active;
    }

    void start() {
        active = true;
        p_i = 0;
        p_f = 0.0f;
    }

    void stop() {
        active = false;
        p_i = 0;
        p_f = 0;
        vol = 0;
    }

    uint8_t get_volume() {
        return vol;
    }

    float get_freq() {
        return freq;
    }

    float get_period() {
        return period;
    }

    void process_block(audio16_mono_t *buf, size_t buf_size) {
        // No activity optimization
        if (!active) {
            return;
        } else if (vol == 0) {
            return;
        } else if (smp == NULL) {
            active = false;
            return;
        } else if (smp->data.size() < 2) {
            active = false;
            return;
        }

        // Abnormal param
        if (smp->data.size() == 0) {
            return;
        }

        if (freq < 4181) {
            printf("WARN: Freq too low (%d)\n", (int)freq);
            goto zero_buf;
        } else if (freq > 31680) {
            printf("WARN: Freq too high (%d)\n", (int)freq);
            goto zero_buf;
        }

        if (samp_rate < 4000) {
            printf("ERR: Sample rate too low (%d)\n", samp_rate);
            goto zero_buf;
        }

        if (os_factor == 0) {
            printf("ERR: OverSample factor too low (%d)\n", os_factor);
            goto zero_buf;
        }

        {
            float p_c_os = p_c / (float)os_factor;

            for (size_t i = 0; i < buf_size; i++) {
                float y_decim = 0.0f;

                for (uint32_t os = 0; os < os_factor; os++) {
                    float x = 0.0f;

                    if (active) {
                        if (p_i < smp->data.size()) {
                            x = (float)get_sample_value(p_i) * (float)vol;
                        } else {
                            x = 0.0f;
                        }
                    }

                    y_decim = aa_fir.process(x);

                    if (!active) {
                        continue;
                    }

                    p_f += p_c_os;

                    if (p_f >= 1.0f) {
                        uint32_t step = (uint32_t)p_f;

                        p_i += step;
                        p_f -= (float)step;
                    }

                    if ((smp->loop_length > 2) && !disable_loop) {
                        if (p_i >= (smp->loop_start + smp->loop_length)) {
                            p_i -= smp->loop_length;
                        }
                    } else {
                        if (p_i >= smp->data.size()) {
                            stop();
                        }
                    }
                }

                int32_t out;

                if (y_decim >= 0.0f) {
                    out = (int32_t)(y_decim + 0.5f);
                } else {
                    out = (int32_t)(y_decim - 0.5f);
                }

                buf[i] = softclip(out);
            }

            return;
        }

    zero_buf:
        memset(buf, 0, buf_size * sizeof(int16_t));
    }

    void set_sample_rate(uint32_t sr) {
        samp_rate = sr;
        set_freq(get_freq());
    }

    void set_oversample_factor(uint32_t osf) {
        os_factor = osf;
        aa_fir.rebuild_lowpass(aa_fir_taps, os_factor);
    }

    void set_aa_fir_taps(size_t taps) {
        aa_fir_taps = taps;
        aa_fir.rebuild_lowpass(aa_fir_taps, os_factor);
    }

    void set_sample(mod_sample_t *s) {
        if (s == NULL) {
            printf("WARN: set_sample(NULL)\n");
            smp = NULL;
            return;
        }
        smp = s;
        vol = smp->volume;
        invert_loop_acc = 0;
        rebuild_invert_loop_flags();
        // printf("set_sample(%p)\n", smp);
    }

    void set_sample_offset(uint32_t ofst) {
        p_i = ofst;
        p_f = 0.0f;
    }

    void clear_sample() {
        smp = NULL;
        stop();
        printf("clear_sample()\n");
    }

    void set_freq(float f) {
        freq = f;
        p_c = f / (float)samp_rate;
        period = MASTER_CLOCK_FREQ / f;
    }

    float apply_finetune(float p, int8_t ft) {
        return p * powf(2.0f, -(float)ft / 96.0f);
    }

    void set_period(float p) {
        period = p;
        freq = MASTER_CLOCK_FREQ / apply_finetune(p, finetune);
        p_c = freq / (float)samp_rate;
    }

    void set_volume(int v) {
        if (v > 64) v = 64; else if (v < 0) v = 0;
        vol = v;
        // printf("set_volume(%d)\n", vol);
    }

    void set_finetune(int8_t ft) {
        finetune = ft;
        freq = MASTER_CLOCK_FREQ / apply_finetune(period, finetune);
    }

    int8_t get_finetune() {
        return finetune;
    }

    void set_invert_loop(uint8_t speed) {
        invert_loop_speed = speed & 0x0F;

        if (invert_loop_speed == 0) {
            invert_loop_acc = 0;
            return;
        }

        if (invert_loop_flags.size() != (smp ? smp->loop_length : 0)) {
            rebuild_invert_loop_flags();
        }
    }

    void process_invert_loop_tick() {
        static const uint8_t invert_loop_table[16] = {
            0, 5, 6, 7, 8, 10, 11, 13,
            16, 19, 22, 26, 32, 43, 64, 128
        };

        if (invert_loop_speed == 0) {
            return;
        }

        if (smp == NULL) {
            return;
        }

        if ((smp->loop_length <= 2) || disable_loop) {
            return;
        }

        if (invert_loop_flags.size() != smp->loop_length) {
            rebuild_invert_loop_flags();
        }

        if (invert_loop_flags.size() == 0) {
            return;
        }

        invert_loop_acc += invert_loop_table[invert_loop_speed];

        if (invert_loop_acc < 128) {
            return;
        }

        invert_loop_acc -= 128;

        if (invert_loop_pos >= invert_loop_flags.size()) {
            invert_loop_pos = 0;
        }

        invert_loop_flags[invert_loop_pos] ^= 1;

        invert_loop_pos++;
        if (invert_loop_pos >= invert_loop_flags.size()) {
            invert_loop_pos = 0;
        }
    }

    void set_disable_loop(bool s) {
        disable_loop = s;
    }
};

struct efx_status_t {
    uint8_t cmd = 0;
    uint8_t par = 0;

    uint8_t vol_slide_par = 0;
    uint8_t porta_up = 0;
    uint8_t porta_down = 0;

    float base_period = 0;
    int base_volume = 0;

    bool vibrato_status_last = false;
    bool vibrato_enable = false;
    bool vibrato_disable_dirty = false;
    MOD_LFO vibrato;

    bool tremolo_status_last = false;
    bool tremolo_enable = false;
    bool tremolo_disable_dirty = false;
    MOD_LFO tremolo;

    float tone_porta_target_period = 0;
    uint8_t tone_porta_speed = 0;

    uint8_t note_cut_tick = 0;

    uint8_t retrig_note = 0;
    uint8_t retrig_note_tick = 0;

    uint8_t note_delay_tick = 0;
    float note_delay_period = 0;
    int note_delay_sample = 0;

    int arp_pos = 0;
    bool arp_status = false;
    bool arp_status_last = false;
};

class MOD_TRACKER {
private:
    MOD_FILE *mod = NULL;

    std::vector<MOD_CHANNEL> channels;
    std::vector<efx_status_t> chan_efx;
    std::vector<int16_t> chan_pan;

    uint8_t tempo = 125;
    uint8_t ticks_row = 6;
    uint32_t samples_tick = 0;
    uint32_t samp_rate = 0;

    int row = 0; // Row position
    int pos = 0; // Frame position

    uint32_t samples_left_tick = 0;
    uint32_t tick_count = 0;

    bool active = false;
    int song_loop = -1;
    bool song_finished = false;

    std::vector<std::vector<audio16_mono_t>> mix_buf;

    int pattern_break = -1;
    int pattern_loop = -1;
    int pattern_loop_start = 0;
    int pattern_loop_count = 0;

public:
    MOD_TRACKER() {
        realloc_channels(4);
        pause();
    }

    void set_tempo(uint8_t t) {
        tempo = t;
        samples_tick = roundf((float)samp_rate * (2.5f / (float)tempo));
    }

    uint8_t get_tempo() {
        return tempo;
    }

    void set_ticks_row(uint8_t tr) {
        ticks_row = tr;
    }

    uint8_t get_ticks_row() {
        return ticks_row;
    }

    uint32_t get_samples_tick() {
        return samples_tick;
    }

    void set_sample_rate(uint32_t sr) {
        samp_rate = sr;
        samples_tick = roundf((float)samp_rate * (2.5f / (float)tempo));
        for (int i = 0; i < channels.size(); i++) {
            channels[i].set_sample_rate(samp_rate);
        }
    }

    uint32_t get_sample_rate() {
        return samp_rate;
    }

    void realloc_channels(uint8_t num_chan) {
        if (num_chan > 99) {
            num_chan = 99;
        } else if (num_chan < 1) {
            num_chan = 1;
        }
        channels.clear();
        channels.resize(num_chan);
        chan_efx.clear();
        chan_efx.resize(num_chan);
        chan_pan.clear();
        chan_pan.resize(num_chan);
        for (int i = 0; i < channels.size(); i++) {
            channels[i].set_sample_rate(samp_rate);
            chan_pan[i] = amiga_pan_default[i & 3];
        }
        mix_buf.resize(channels.size());
    }

    uint8_t get_num_channel() {
        return channels.size();
    }

    void set_row(int r) {
        row = r;
    }

    int get_row() {
        return row;
    }

    void set_frame_pos(int p) {
        pos = p;
    }

    int get_frame_pos() {
        return pos;
    }

    void set_song_loop(int loop) {
        song_loop = loop;
    }

    int get_song_loop() {
        return song_loop;
    }

    bool get_song_finished() {
        return song_finished;
    }

    bool get_active() {
        return active;
    }

    void start() {
        active = true;
    }

    void pause() {
        active = false;
    }

    void reset() {
        pause();
        song_finished = false;
        samples_left_tick = 0;
        tick_count = 0;
        pos = 0;
        row = 0;
        song_loop = -1;
        realloc_channels(mod->get_num_channel());
    }

    void finish_song() {
        active = false;
        song_finished = true;
    }

    void init_mod(MOD_FILE *m) {
        mod = m;
        reset();
        set_tempo(125);
        set_ticks_row(6);
    }

    void apply_tone_portamento(int c) {
        efx_status_t &e = chan_efx[c];

        if (e.tone_porta_target_period <= 0.0f)
            return;

        if (e.tone_porta_speed == 0)
            return;

        float p = e.base_period;
        float target = e.tone_porta_target_period;
        float speed = e.tone_porta_speed;

        if (p > target) {
            p -= speed;
            if (p < target)
                p = target;
        } else if (p < target) {
            p += speed;
            if (p > target)
                p = target;
        }

        e.base_period = p;
        channels[c].set_period(e.base_period);
    }

    void process_tick_efx(int c) {
        chan_efx[c].arp_status_last = chan_efx[c].arp_status;
        chan_efx[c].arp_status = false;
        chan_efx[c].vibrato_status_last = chan_efx[c].vibrato_enable;
        chan_efx[c].vibrato_enable = false;
        chan_efx[c].tremolo_status_last = chan_efx[c].tremolo_enable;
        chan_efx[c].tremolo_enable = false;

        switch (chan_efx[c].cmd) {
        case 0x0: // Arpeggio
            if (chan_efx[c].par == 0) {
                break;
            }
            chan_efx[c].arp_status = true;
            switch (chan_efx[c].arp_pos) {
            case 0:
                channels[c].set_period(chan_efx[c].base_period);
                break;

            case 1:
                channels[c].set_period(transpose_period(chan_efx[c].base_period, U8_HI(chan_efx[c].par)));
                break;

            case 2:
                channels[c].set_period(transpose_period(chan_efx[c].base_period, U8_LO(chan_efx[c].par)));
                break;
            }
            chan_efx[c].arp_pos++;
            if (chan_efx[c].arp_pos > 2) {
                chan_efx[c].arp_pos = 0;
            }
            break;

        case 0x1: // Portamento Up
            channels[c].set_period(channels[c].get_period() - chan_efx[c].porta_up);
            chan_efx[c].base_period = channels[c].get_period();
            break;

        case 0x2: // Portamento Down
            channels[c].set_period(channels[c].get_period() + chan_efx[c].porta_down);
            chan_efx[c].base_period = channels[c].get_period();
            break;

        case 0x3: // TonePortamento
            apply_tone_portamento(c);
            chan_efx[c].base_period = channels[c].get_period();
            break;

        case 0x4: // Vibrato
            chan_efx[c].vibrato_enable = true;
            break;

        case 0x5: // TonePortamento + VolumeSlide
            channels[c].set_volume((int)channels[c].get_volume() +
                ((int)U8_HI(chan_efx[c].vol_slide_par) - (int)U8_LO(chan_efx[c].vol_slide_par))
            );
            chan_efx[c].base_volume = channels[c].get_volume();

            apply_tone_portamento(c);
            chan_efx[c].base_period = channels[c].get_period();
            break;

        case 0x6: // Vibrato + VolumeSlide
            channels[c].set_volume((int)channels[c].get_volume() +
                ((int)U8_HI(chan_efx[c].vol_slide_par) - (int)U8_LO(chan_efx[c].vol_slide_par))
            );
            chan_efx[c].base_volume = channels[c].get_volume();
            chan_efx[c].vibrato_enable = true;
            break;

        case 0x7: // Tremolo
            chan_efx[c].tremolo_enable = true;
            break;

        case 0xA: // VolumeSlide
            channels[c].set_volume((int)channels[c].get_volume() +
                ((int)U8_HI(chan_efx[c].vol_slide_par) - (int)U8_LO(chan_efx[c].vol_slide_par))
            );
            break;

        default:
            break;
        }

        if (chan_efx[c].note_cut_tick) {
            chan_efx[c].note_cut_tick--;
            if (chan_efx[c].note_cut_tick == 0) {
                channels[c].set_volume(0);
                // printf("C%d: NOTE CUT\n", c);
            }
        } else if (chan_efx[c].note_delay_tick) {
            chan_efx[c].note_delay_tick--;
            if (chan_efx[c].note_delay_tick == 0) {
                if (chan_efx[c].note_delay_period) {
                    channels[c].set_period(chan_efx[c].note_delay_period);
                    chan_efx[c].note_delay_period = 0;
                }
                if (chan_efx[c].note_delay_sample) {
                    channels[c].set_sample(mod->get_sample(chan_efx[c].note_delay_sample));
                    chan_efx[c].note_delay_sample = 0;
                }
                // printf("C%d: DELAY NOTE TRIG\n", c);
            }
        } else if ((chan_efx[c].cmd == 0xE) && (U8_HI(chan_efx[c].par) == 0x9)) { // RETRIG NOTE
            chan_efx[c].retrig_note_tick--;
            if (chan_efx[c].retrig_note_tick == 0) {
                channels[c].start();
                chan_efx[c].retrig_note_tick = chan_efx[c].retrig_note;
                // printf("C%d: RETRIG\n", c);
            }
        }

        if ((chan_efx[c].arp_status_last == true) && (chan_efx[c].arp_status == false)) { // falling edge
            chan_efx[c].arp_pos = 0;
            channels[c].set_period(chan_efx[c].base_period);
            // printf("C%d: ARP END\n", c);
        }
        // Fucking the vibrato and tremolo....XD
        // Anyway, this is a handler for vibrato
        if ((chan_efx[c].vibrato_status_last == true) && (chan_efx[c].vibrato_enable == false)) { // falling edge
            chan_efx[c].vibrato.reset_phase();
            channels[c].set_period(chan_efx[c].base_period);
            // printf("C%d: VIBRATE END\n", c);
        }
        if (chan_efx[c].vibrato_enable) {
            int16_t delta = chan_efx[c].vibrato.next_vibrato_delta();
            channels[c].set_period(chan_efx[c].base_period + (float)delta);
            // printf("VIBRATO: %f + %d = %f\n", chan_efx[c].base_period, delta, channels[c].get_period());
        }
        // , and this one for tremolo.
        if ((chan_efx[c].tremolo_status_last == true) && (chan_efx[c].tremolo_enable == false)) { // falling edge
            chan_efx[c].tremolo.reset_phase();
            channels[c].set_volume(chan_efx[c].base_volume);
            // printf("C%d: TREMOLO END\n", c);
        }
        if (chan_efx[c].tremolo_enable) {
            int16_t delta = chan_efx[c].tremolo.next_tremolo_delta();
            channels[c].set_volume(chan_efx[c].base_volume + delta);
            // printf("TREMOLO: %d + %d = %d\n", chan_efx[c].base_volume, delta, channels[c].get_volume());
        }
    }

    void next_tick() {
        if (tick_count == 0) {
            next_row();
        } else {
            for (int c = 0; c < get_num_channel(); c++) {
                process_tick_efx(c);
            }
        }

        for (int c = 0; c < get_num_channel(); c++) {
            channels[c].process_invert_loop_tick();
        }

        tick_count++;

        if (tick_count >= ticks_row) {
            tick_count = 0;
        }
    }

    void process_row_efx(int c, uint8_t cmd, uint8_t par) {
        chan_efx[c].cmd = cmd, chan_efx[c].par = par;
        if (!(cmd || par)) {
            return;
        }
        // printf("C%d: EFX %1X%02X\n", c, cmd, par);
        switch (chan_efx[c].cmd)
        {
        case 0x0: // SET ARP
            if (par == 0) {
                break;
            }
            chan_efx[c].arp_pos = 1;
            break;
        case 0x1: // SET PORTAMENTO UP
            if (par) {
                chan_efx[c].porta_up = par;
                // printf("C%d: SET PORTAMENTO UP -> %d\n", c, par);
            }
            break;

        case 0x2: // SET PORTAMENTO DOWN
            if (par) {
                chan_efx[c].porta_down = par;
                // printf("C%d: SET PORTAMENTO DOWN -> %d\n", c, par);
            }
            break;

        case 0x3: // SET TONE PORTAMENTO
            if (par) {
                chan_efx[c].tone_porta_speed = par;
                // printf("C%d: SET TONE PORTAMENTO, SPEED -> %d\n", c, par);
            }
            break;

        case 0x4: // SET VIBRATO
            chan_efx[c].vibrato_enable = true;
            if (par) {
                chan_efx[c].vibrato.set_param(par);
                // printf("C%d: SET VIBRATO, RATE -> %d, DEPTH -> %d\n", c, U8_HI(par), U8_LO(par));
            }
            break;

        case 0x5: // SET VOLUME SLIDE
            // if (par)
                chan_efx[c].vol_slide_par = par;
                // printf("C%d: SET VOLUME SLIDE -> +%d -%d, TONE PORTAMENTO -> CONTINUE\n", c, U8_HI(par), U8_LO(par));
            break;

        case 0x6: // SET VOLUME SLIDE + VIBRATO
            chan_efx[c].vibrato_enable = true;
            // if (par)
                chan_efx[c].vol_slide_par = par;
                // printf("C%d: SET VOLUME SLIDE -> +%d -%d, VIBRATO -> CONTINUE\n", c, U8_HI(par), U8_LO(par));
            break;

        case 0x7: // SET TREMOLOS
            chan_efx[c].tremolo_enable = true;
            chan_efx[c].base_volume = channels[c].get_volume();
            if (par) {
                chan_efx[c].tremolo.set_param(par);
                // printf("C%d: SET VIBRATO, RATE -> %d, DEPTH -> %d\n", c, U8_HI(par), U8_LO(par));
            }
            break;

        case 0x8: // Set Panning, PC MOD extension
            chan_pan[c] = par;
            // printf("C%d: SET PAN -> %d\n", c, par);
            break;

        case 0x9: // SET SAMPLE OFFSET
            channels[c].start();
            channels[c].set_sample_offset((int)par * 256);
            // printf("C%d: SET SAMPLE OFFSET -> %d\n", c, par << 8);
            break;

        case 0xA: // SET VOLUME SLIDE
            // if (par)
                chan_efx[c].vol_slide_par = par;
                // printf("C%d: SET VOLUME SLIDE -> +%d -%d\n", c, U8_HI(par), U8_LO(par));
            break;

        case 0xC: // SET VOLUME
            channels[c].set_volume(par);
            // printf("C%d: SET VOLUME -> %d\n", c, par);
            break;

        case 0xD: // SET PATTERN BREAK
            pattern_break = par;
            printf("GLOBAL: PATTERN BREAK -> %d\n", par);
            break;

        case 0xE: // Sub command
            {
                uint8_t E_par = U8_LO(par);
                switch (U8_HI(par)) {
                case 0x1: // FINE SLIDE UP
                    channels[c].set_period(channels[c].get_period() - E_par);
                    chan_efx[c].base_period = channels[c].get_period();
                    // printf("C%d: FINE SLIDE UP -> %d\n", c, E_par);
                    break;

                case 0x2: // FINE SLIDE DOWN
                    channels[c].set_period(channels[c].get_period() + E_par);
                    chan_efx[c].base_period = channels[c].get_period();
                    // printf("C%d: FINE SLIDE DOWN -> %d\n", c, E_par);
                    break;

                case 0x4: // SET VIBRATO WAVEFORM
                    chan_efx[c].vibrato.set_waveform(E_par);
                    // printf("C%d: SET VIBRATO WAVEFORM -> %d\n", c, E_par);
                    break;

                case 0x5: // SET FINETUNE
                    channels[c].set_finetune((int8_t)(E_par ^ 0x08) - 0x08);
                    // printf("C%d: SET FINETUNE -> %d\n", c, channels[c].get_finetune());
                    break;

                case 0x6: // PATTERN LOOP
                    if (E_par == 0) {
                        pattern_loop_start = row;
                        printf("Pattern Loop Start: %d\n", pattern_loop_start);
                    } else {
                        if (pattern_loop_count == 0) {
                            pattern_loop_count = E_par;
                        } else {
                            pattern_loop_count--;
                        }

                        if (pattern_loop_count != 0) {
                            pattern_loop = pattern_loop_start;
                            printf("Pattern Loop Jump -> row %d, count left: %d\n",
                                    pattern_loop, pattern_loop_count);
                        } else {
                            printf("Pattern Loop End\n");
                        }
                    }

                    printf("C%d: SET PATTERN LOOP -> %d\n", c, E_par);
                    break;

                case 0x7: // SET TREMOLO WAVEFORM
                    chan_efx[c].tremolo.set_waveform(E_par);
                    // printf("C%d: SET TREMOLO WAVEFORM -> %d\n", c, E_par);
                    break;

                case 0x9: // RETRIG NOTE
                    chan_efx[c].retrig_note = E_par;
                    chan_efx[c].retrig_note_tick = E_par;
                    // printf("C%d: SET RETRIG NOTE -> %d\n", c, E_par);
                    break;

                case 0xA: // FINE VOLUME SLIDE UP
                    channels[c].set_volume(channels[c].get_volume() + E_par);
                    // printf("C%d: FINE VOLUME SLIDE UP -> %d\n", c, E_par);
                    break;

                case 0xB: // FINE VOLUME SLIDE DOWN
                    channels[c].set_volume(channels[c].get_volume() - E_par);
                    // printf("C%d: FINE VOLUME SLIDE DOWN -> %d\n", c, E_par);
                    break;

                case 0xC: // SET NOTE CUT TICK
                    chan_efx[c].note_cut_tick = E_par;
                    if (E_par == 0) {
                        channels[c].set_volume(0);
                    }
                    // printf("C%d: SET NOTE CUT TICK -> %d\n", c, E_par);
                    break;

                case 0xD: // NOTE DELAY
                    chan_efx[c].note_delay_tick = E_par;
                    // printf("C%d: SET NOTE DELAY -> %d\n", c, E_par);
                    break;

                case 0xE: // PATTERN DELAY
                    printf("C%d: UNSUPPORTED PATTERN DELAY :P\n", c);
                    // I analyzed over 10000 .mod files,
                    // and found that only about 2% of them used the EEx command,
                    // so I decided not to bother with it XD
                    break;

                case 0xF: // INVERT LOOP
                    channels[c].set_invert_loop(E_par);
                    // printf("C%d: SET INVERT LOOP -> %d\n", c, E_par);
                    break;

                default:
                    break;
                }
            }
            break;

        case 0xF:
            if (par >= 0x20) {
                set_tempo(par);
                printf("GLOBAL: SET TEMPO -> %d\n", par);
            } else {
                set_ticks_row(par);
                printf("GLOBAL: SET TICKS/ROW -> %d\n", par);
            }
            break;

        default:
            // // printf("C%d: UNKNOW EFX %1X%02X\n", c, cmd, par);
            break;
        }
    }

    void next_row() {
        if (pattern_break >= 0) {
            pos++;
            row = pattern_break;
            pattern_break = -1;
        }
        if (pattern_loop >= 0) {
            row = pattern_loop;
            pattern_loop = -1;
        }
        printf("%02d:%02d | ", pos, row);
        for (int c = 0; c < get_num_channel(); c++) {
            note_t *note = mod->get_note_order(pos, c, row);
            if (note == NULL) {
                printf("WARN: Null note on P=%d, C=%d, R=%d\n", pos, c, row);
                continue;
            }
            if (note->period) {
                char note_str[4];
                mod_period_to_note_str(note->period, note_str);
                printf("%.3s ", note_str);
            } else {
                printf("    ");
            }
            if (note->sample) {
                printf("%02d ", note->sample+1);
            } else {
                printf("   ");
            }
            if (note->efx_cmd) {
                printf("%.1X", note->efx_cmd);
            } else {
                printf(" ");
            }
            if (note->efx_par) {
                printf("%02X ", note->efx_par);
            } else {
                printf("   ");
            }
            printf(" ");
        }
        printf("\n");
        for (int c = 0; c < get_num_channel(); c++) {
            note_t *note = mod->get_note_order(pos, c, row);
            if (note == NULL) {
                // printf("WARN: Null note on P=%d, C=%d, R=%d\n", pos, c, row);
                continue;
            }

            if (note->period) {
                if ((note->efx_cmd == 0x3) || (note->efx_cmd == 0x5)) {
                    chan_efx[c].tone_porta_target_period = note->period;
                    // printf("C%d: SET TONEPORTAMENTO TARGET -> %d\n", c, note->period);
                } else if ((note->efx_cmd == 0xE) && (U8_HI(note->efx_par) == 0xD)) {
                    chan_efx[c].note_delay_period = note->period;
                    continue;
                } else {
                    channels[c].set_period(note->period);
                    channels[c].start();
                }

                chan_efx[c].vibrato.reset_phase_if_needed();
                chan_efx[c].base_period = channels[c].get_period();
                chan_efx[c].tremolo.reset_phase_if_needed();
                chan_efx[c].base_volume = channels[c].get_volume();
            }
            if (note->sample) {
                if ((note->efx_cmd == 0xE) && (U8_HI(note->efx_par) == 0xD)) {
                    chan_efx[c].note_delay_sample = note->sample - 1;
                    continue;
                } else {
                    channels[c].set_sample(mod->get_sample(note->sample - 1));
                }
            }
            process_row_efx(c, note->efx_cmd, note->efx_par);
        }
        row++;
        if (row >= mod->get_pattern_size(mod->get_order(pos))) {
            row = 0;
            pos++;

            if (pos >= mod->get_order_size()) {
                if (song_loop < 0) {
                    // infinite loop
                    pos = 0;
                    row = 0;
                } else if (song_loop > 0) {
                    // finite loop
                    pos = 0;
                    row = 0;
                    song_loop--;
                } else {
                    // no loop
                    finish_song();
                    return;
                }
            }

            pattern_loop_start = 0;
            pattern_loop_count = 0;
            pattern_loop = -1;

            printf("frame(%d)\033[2J\033[H", pos);
        }
    }

    void mixer(audio16_t *buf, size_t frames) {
        for (int c = 0; c < get_num_channel(); c++) {
            if (mix_buf[c].size() != frames) {
                mix_buf[c].resize(frames);
            }

            memset(mix_buf[c].data(), 0, frames * sizeof(int16_t));
            channels[c].process_block(mix_buf[c].data(), frames);
        }

        for (size_t i = 0; i < frames; i++) {
            int32_t mix_l = 0;
            int32_t mix_r = 0;

            for (int c = 0; c < mix_buf.size(); c++) {
                if (!channels[c].get_active() || !channels[c].get_volume())
                    continue;

                int32_t s = mix_buf[c][i];

                /*
                    pan:
                    0   = left
                    128 = center
                    255 = right
                */
                int16_t pan = chan_pan[c];

                int16_t gain_l = 256 - pan;
                int16_t gain_r = pan;

                mix_l += (s * gain_l) >> 8;
                mix_r += (s * gain_r) >> 8;
            }

            buf[i].l = softclip(mix_l);
            buf[i].r = softclip(mix_r);
        }
    }

    void process_block(audio16_t *buf, size_t frames) {
        if (!active || mod == nullptr || samples_tick == 0) {
            memset(buf, 0, frames * sizeof(audio16_t));
            return;
        }

        size_t done = 0;

        while (done < frames) {
            if (samples_left_tick == 0) {
                next_tick();
                samples_left_tick = samples_tick;
            }

            size_t n = frames - done;
            if (n > samples_left_tick)
                n = samples_left_tick;

            mixer(buf + done, n);

            done += n;
            samples_left_tick -= n;
        }
    }
};
