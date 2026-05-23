#include "LFO.h"
#include "mod_helper.h"

void MOD_LFO::set_param(uint8_t param) {
    uint8_t x = U8_HI(param);
    uint8_t y = U8_LO(param);

    if (x) speed = x;
    if (y) depth = y;
}

void MOD_LFO::set_speed(uint8_t s) {
    speed = s;
}

void MOD_LFO::set_depth(uint8_t d) {
    depth = d;
}

uint8_t MOD_LFO::get_speed() {
    return speed;
}

uint8_t MOD_LFO::get_depth() {
    return depth;
}

void MOD_LFO::set_waveform(uint8_t wf) {
    waveform = wf & 7;
}

void MOD_LFO::reset_phase_if_needed() {
    if ((waveform & 4) == 0)
        phase = 0;
}

void MOD_LFO::reset_phase() {
    phase = 0;
}

int16_t MOD_LFO::next_raw() {
    int16_t v = 0;

    switch (waveform & 3) {
    case MOD_LFO_SINE:
        v = sine_value();
        break;

    case MOD_LFO_RAMP_DOWN:
        v = (int16_t)((255 - ((int)phase * 510 / 255)) * (int)depth);
        break;

    case MOD_LFO_SQUARE:
        v = (phase < 128)
            ? (int16_t)(255 * depth)
            : (int16_t)(-255 * depth);
        break;

    case MOD_LFO_RANDOM:
        v = (int16_t)((rand() % 511) - 255);
        v = (int16_t)(v * (int)depth);
        break;
    }

    phase += speed << 2;
    return v;
}

int16_t MOD_LFO::next_vibrato_delta() {
    return next_raw() >> 7; // /128
}

int16_t MOD_LFO::next_tremolo_delta() {
    return next_raw() >> 6; // /64
}

int16_t MOD_LFO::sine_value() const {
    float x = (float)phase * (2 * M_PI) / 256.0f;
    return (int16_t)roundf(sinf(x) * 255.0f * (float)depth);
}
