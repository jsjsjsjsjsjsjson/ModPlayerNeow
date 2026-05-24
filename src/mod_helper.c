#include "mod_helper.h"
#include <math.h>
#include <string.h>

void mod_period_to_note_str(int period, char *buf) {
    if (period > MOD_PERIOD_MAX) {
        strcpy(buf, "LOW");
        return;
    }

    if (period < MOD_PERIOD_MIN) {
        strcpy(buf, "HIG");
        return;
    }

    uint8_t ni = period_to_note_lut[period];
    if (ni == 0) {
        strcpy(buf, "UNK");
        return;
    }

    const char *n = NOTE_CHARS[ni % 12];

    buf[0] = n[0];
    buf[1] = n[1];
    buf[2] = '1' + (ni / 12);
    buf[3] = '\0';
}

float transpose_period(float period, int semitone) {
    return period * powf(2.0f, -(float)semitone / 12.0f);
}
