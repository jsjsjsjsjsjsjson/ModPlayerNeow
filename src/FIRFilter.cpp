#include "FIRFilter.h"

#include <math.h>

FIRFilter::FIRFilter() {
    taps = 0;
    hist_pos = 0;
}

void FIRFilter::reset_state(void) {
    for (size_t i = 0; i < hist.size(); i++) {
        hist[i] = 0.0f;
    }

    hist_pos = 0;
}

float FIRFilter::sinc_norm(float x) {
    if (fabsf(x) < 1.0e-6f) {
        return 1.0f;
    }

    return sinf(M_PI * x) / (M_PI * x);
}

void FIRFilter::rebuild_lowpass(size_t new_taps, uint32_t os_factor) {
    if (new_taps < 1) {
        new_taps = 1;
    }

    /*
     * FIR low-pass normally wants odd tap count.
     * If even taps are passed, force it to odd.
     */
    if ((new_taps & 1) == 0) {
        new_taps++;
    }

    taps = new_taps;

    coeff.resize(taps);
    hist.resize(taps);

    reset_state();

    for (size_t i = 0; i < taps; i++) {
        coeff[i] = 0.0f;
    }

    /*
     * os_factor == 0 is invalid.
     * os_factor == 1 means no oversampling, so bypass.
     */
    if (os_factor <= 1) {
        coeff[0] = 1.0f;
        return;
    }

    /*
     * Cutoff normalized to oversampled Nyquist.
     *
     * 0.5 / os_factor is the final output Nyquist.
     * 0.45 / os_factor gives a small transition band.
     */
    const float cutoff = 0.45f / (float)os_factor;
    const float mid = (float)(taps - 1) * 0.5f;

    float sum = 0.0f;

    for (size_t n = 0; n < taps; n++) {
        const float x = (float)n - mid;

        /*
         * Ideal low-pass:
         * h[n] = 2fc * sinc(2fc * n)
         */
        const float ideal = 2.0f * cutoff * sinc_norm(2.0f * cutoff * x);

        /*
         * Hamming window.
         */
        const float window =
            0.54f - 0.46f * cosf((2.0f * M_PI * (float)n) / (float)(taps - 1));

        coeff[n] = ideal * window;
        sum += coeff[n];
    }

    /*
     * Normalize DC gain to 1.0.
     */
    if (fabsf(sum) > 1.0e-12f) {
        for (size_t n = 0; n < taps; n++) {
            coeff[n] /= sum;
        }
    }
}

float FIRFilter::process(float x) {
    if (taps == 0 || coeff.size() == 0 || hist.size() == 0) {
        return x;
    }

    hist[hist_pos] = x;

    float y = 0.0f;
    size_t pos = hist_pos;

    for (size_t i = 0; i < taps; i++) {
        y += coeff[i] * hist[pos];

        if (pos == 0) {
            pos = taps - 1;
        } else {
            pos--;
        }
    }

    hist_pos++;

    if (hist_pos >= taps) {
        hist_pos = 0;
    }

    return y;
}
