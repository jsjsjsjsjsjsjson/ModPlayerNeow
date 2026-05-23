#pragma once

#include <stdint.h>
#include <math.h>
#include <stdlib.h>

enum MOD_LFO_WAVE {
    MOD_LFO_SINE      = 0,
    MOD_LFO_RAMP_DOWN = 1,
    MOD_LFO_SQUARE    = 2,
    MOD_LFO_RANDOM    = 3,
};

class MOD_LFO {
private:
    uint8_t phase = 0;
    uint8_t speed = 0;
    uint8_t depth = 0;
    uint8_t waveform = MOD_LFO_SINE;

public:
    void set_param(uint8_t param);
    void set_speed(uint8_t s);
    void set_depth(uint8_t d);
    uint8_t get_speed();
    uint8_t get_depth();
    void set_waveform(uint8_t wf);
    void reset_phase_if_needed();
    void reset_phase();
    int16_t next_raw();
    int16_t next_vibrato_delta();
    int16_t next_tremolo_delta();

private:
    int16_t sine_value() const;
};
