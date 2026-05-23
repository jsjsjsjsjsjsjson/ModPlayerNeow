#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <vector>

#include "mod_helper.h"
#include "LFO.h"
#include "mod_file.h"

#define NTSC_AMIGA

#ifdef NTSC_AMIGA
    #define MASTER_CLOCK_FREQ 3579545.0f
#else
    #define MASTER_CLOCK_FREQ 3546895.0f
#endif

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
    int os_factor = 4; // "os" means "Over Sample"

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

public:
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

    void process_block(int16_t *buf, size_t buf_size) {
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
        if (freq < 1.0f) {
            printf("WARN: Freq too low (%d)\n", (int)freq);
            goto zero_buf;
        }
        if (samp_rate < 1000) {
            printf("ERR: Sample rate too low (%d)\n", samp_rate);
            goto zero_buf;
        }
        if (os_factor == 0) {
            printf("ERR: OverSample factor too low (%d)\n", os_factor);
            goto zero_buf;
        }

        {
            // Phase accumulator
            float p_c_os = p_c / (float)os_factor; // Phase Count for OverSample
            for (size_t i = 0; i < buf_size; i++) {
                int32_t acc = 0; // Accumulator for OverSample
                for (uint32_t os = 0; os < os_factor; os++) {
                    if (!active) continue;
                    acc += (p_i < smp->data.size()) ? ((smp->data[p_i] * vol)) : 0; // To prevent potential array index out-of-bounds errors caused by oversampling
                    p_f += p_c_os;
                    if (p_f >= 1.0f) {
                        p_i += (uint32_t)p_f;
                        p_f -= (uint32_t)p_f; // Error accumulation
                    }
                    
                    if ((smp->loop_length > 2) && !disable_loop) { // Looping is enabled when the loop length is larger than 2
                        if (p_i >= (smp->loop_start + smp->loop_length)) {
                            p_i -= smp->loop_length; // Error accumulation
                        }
                    } else { // Otherwise, looping is disabled, therefore, set `active` to false after the sample has played to the end
                        if (p_i >= smp->data.size()) {
                            stop();
                        }
                    }
                }
                acc /= os_factor; // Average filtering (even if the quality is really poor xwq)
                buf[i] = softclip(acc);
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
    }

    void set_sample(mod_sample_t *s) {
        if (s == NULL) {
            printf("WARN: set_sample(NULL)\n");
            smp = NULL;
            return;
        }
        smp = s;
        vol = smp->volume;
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
};

class MOD_TRACKER {
private:
    MOD_FILE *mod = NULL;

    std::vector<MOD_CHANNEL> channels;
    std::vector<efx_status_t> chan_efx;

    uint8_t tempo = 125;
    uint8_t ticks_row = 6;
    uint32_t samples_tick = 0;
    uint32_t samp_rate = 0;

    int row = 0; // Row position
    int pos = 0; // Frame position

    uint32_t samples_left_tick = 0;
    uint32_t tick_count = 0;

    bool active = false;

    std::vector<std::vector<int16_t>> mix_buf;

public:
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
        for (int i = 0; i < channels.size(); i++) {
            channels[i].set_sample_rate(samp_rate);
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

    void start() {
        active = true;
    }

    void pause() {
        active = false;
    }

    void reset() {
        pause();
        samples_left_tick = 0;
        tick_count = 0;
        set_frame_pos(0);
        set_row(0);
        realloc_channels(mod->get_num_channel());
    }

    bool get_active() {
        return active;
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
        chan_efx[c].vibrato_status_last = chan_efx[c].vibrato_enable;
        chan_efx[c].vibrato_enable = false;
        chan_efx[c].tremolo_status_last = chan_efx[c].tremolo_enable;
        chan_efx[c].tremolo_enable = false;

        switch (chan_efx[c].cmd) {
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
        
        // Funking the vibrato and tremolo....XD
        // Anyway, this is a handler for vibrato
        if ((chan_efx[c].vibrato_status_last == true) && (chan_efx[c].vibrato_enable == false)) { // falling edge
            chan_efx[c].vibrato.reset_phase();
            channels[c].set_period(chan_efx[c].base_period);
            printf("C%d: VIBRATE END\n", c);
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
            printf("C%d: TREMOLO END\n", c);
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
        printf("C%d: EFX %1X%02X\n", c, cmd, par);
        switch (chan_efx[c].cmd)
        {
        case 0x1:
            if (par) {
                chan_efx[c].porta_up = par;
                printf("C%d: SET PORTAMENTO UP -> %d\n", c, par);
            }
            break;

        case 0x2:
            if (par) {
                chan_efx[c].porta_down = par;
                printf("C%d: SET PORTAMENTO DOWN -> %d\n", c, par);
            }
            break;

        case 0x3:
            if (par) {
                chan_efx[c].tone_porta_speed = par;
                printf("C%d: SET TONE PORTAMENTO, SPEED -> %d\n", c, par);
            }
            break;

        case 0x4:
            chan_efx[c].vibrato_enable = true;
            if (par) {
                chan_efx[c].vibrato.set_param(par);
                printf("C%d: SET VIBRATO, RATE -> %d, DEPTH -> %d\n", c, U8_HI(par), U8_LO(par));
            }
            break;

        case 0x5:
            // if (par)
                chan_efx[c].vol_slide_par = par;
                printf("C%d: SET VOLUME SLIDE -> +%d -%d, TONE PORTAMENTO -> CONTINUE\n", c, U8_HI(par), U8_LO(par));
            break;

        case 0x6:
            chan_efx[c].vibrato_enable = true;
            // if (par)
                chan_efx[c].vol_slide_par = par;
                printf("C%d: SET VOLUME SLIDE -> +%d -%d, VIBRATO -> CONTINUE\n", c, U8_HI(par), U8_LO(par));
            break;

        case 0x7:
            chan_efx[c].tremolo_enable = true;
            chan_efx[c].base_volume = channels[c].get_volume();
            if (par) {
                chan_efx[c].tremolo.set_param(par);
                printf("C%d: SET VIBRATO, RATE -> %d, DEPTH -> %d\n", c, U8_HI(par), U8_LO(par));
            }
            break;

        case 0x9:
            channels[c].set_sample_offset(par << 8);
            printf("C%d: SET SAMPLE OFFSET -> %d\n", c, par << 8);
            break;

        case 0xA:
            // if (par)
                chan_efx[c].vol_slide_par = par;
                printf("C%d: SET VOLUME SLIDE -> +%d -%d\n", c, U8_HI(par), U8_LO(par));
            break;

        case 0xC:
            channels[c].set_volume(par);
            printf("C%d: SET VOLUME -> %d\n", c, par);
            break;

        case 0xD:
            pos++;
            row = par;
            printf("GLOBAL: PATTERN BREAK -> %d\n", par);
            break;

        case 0xE:
            {
                uint8_t E_par = U8_LO(par);
                switch (U8_HI(par)) {
                case 0x1:
                    channels[c].set_period(channels[c].get_period() - E_par);
                    chan_efx[c].base_period = channels[c].get_period();
                    printf("C%d: FINE SLIDE UP -> %d\n", c, E_par);
                    break;

                case 0x2:
                    channels[c].set_period(channels[c].get_period() + E_par);
                    chan_efx[c].base_period = channels[c].get_period();
                    printf("C%d: FINE SLIDE DOWN -> %d\n", c, E_par);
                    break;

                case 0x4:
                    chan_efx[c].vibrato.set_waveform(E_par);
                    printf("C%d: SET VIBRATO WAVEFORM -> %d\n", c, E_par);
                    break;

                case 0x5:
                    channels[c].set_finetune((int8_t)(E_par ^ 0x08) - 0x08);
                    printf("C%d: SET FINETUNE -> %d\n", c, channels[c].get_finetune());
                    break;

                case 0x7:
                    chan_efx[c].tremolo.set_waveform(E_par);
                    printf("C%d: SET TREMOLO WAVEFORM -> %d\n", c, E_par);
                    break;

                case 0xA:
                    channels[c].set_volume(channels[c].get_volume() + E_par);
                    printf("C%d: FINE VOLUME SLIDE UP -> %d\n", c, E_par);
                    break;

                case 0xB:
                    channels[c].set_volume(channels[c].get_volume() - E_par);
                    printf("C%d: FINE VOLUME SLIDE DOWN -> %d\n", c, E_par);
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
            // printf("C%d: UNKNOW EFX %1X%02X\n", c, cmd, par);
            break;
        }
    }

    void next_row() {
        for (int c = 0; c < get_num_channel(); c++) {
            note_t *note = mod->get_note_order(pos, c, row);
            if (note == NULL) {
                printf("WARN: Null note on P=%d, C=%d, R=%d\n", pos, c, row);
                continue;
            }

            if (note->period) {
                if ((note->efx_cmd == 0x3) || (note->efx_cmd == 0x5)) {
                    chan_efx[c].tone_porta_target_period = note->period;
                    printf("C%d: SET TONEPORTAMENTO TARGET -> %d\n", c, note->period);
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
                channels[c].set_sample(mod->get_sample(note->sample - 1));
            }
            process_row_efx(c, note->efx_cmd, note->efx_par);
        }
        if (mod->get_pattern_size(pos) < 0) {
            printf("WARN: Pos (%d) out of the song\n", pos);
            row = 0;
            reset();
        } else {
            row++;
            if (row >= mod->get_pattern_size(pos)) {
                row = 0;
                pos++;
                printf("frame ()\n");
            }
        }
    }

    void mixer(int16_t *buf, size_t buf_size) {
        for (int c = 0; c < get_num_channel(); c++) {
            if (mix_buf[c].size() != buf_size) {
                mix_buf[c].resize(buf_size);
            }

            channels[c].process_block(mix_buf[c].data(), buf_size);
        }

        for (size_t i = 0; i < buf_size; i++) {
            int32_t mix = 0;
            for (int c = 0; c < mix_buf.size(); c++) {
                if (channels[c].get_active() && channels[c].get_volume()) mix += mix_buf[c][i];
            }
            buf[i] = softclip(mix);
        }
    }

    void process_block(int16_t *buf, size_t buf_size) {
        if (!active || mod == nullptr || samples_tick == 0) {
            memset(buf, 0, buf_size * sizeof(int16_t));
            return;
        }

        size_t done = 0;

        while (done < buf_size) {
            if (samples_left_tick == 0) {
                next_tick();
                samples_left_tick = samples_tick;
            }

            size_t n = buf_size - done;
            if (n > samples_left_tick)
                n = samples_left_tick;

            mixer(buf + done, n);

            done += n;
            samples_left_tick -= n;
        }
    }
};