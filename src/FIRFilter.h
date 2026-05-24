#ifndef FIR_FILTER_H
#define FIR_FILTER_H

#include <vector>
#include <stdint.h>

class FIRFilter {
public:
    FIRFilter();

    void reset_state(void);
    void rebuild_lowpass(size_t new_taps, uint32_t os_factor);
    float process(float x);

private:
    std::vector<float> coeff;
    std::vector<float> hist;

    size_t taps;
    size_t hist_pos;

    static float sinc_norm(float x);
};

#endif
