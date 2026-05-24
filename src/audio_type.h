#ifndef AUDIO_TYPE_H
#define AUDIO_TYPE_H

#include <stdint.h>

typedef struct {
    int32_t l;
    int32_t r;
} audio32_t;

typedef int32_t audio32_mono_t;

typedef struct {
    int16_t l;
    int16_t r;
} audio16_t;

typedef int16_t audio16_mono_t;

typedef struct {
    int8_t l;
    int8_t r;
} audio8_t;

typedef int8_t audio8_mono_t;

typedef struct {
    uint8_t l;
    uint8_t r;
} audiou8_t;

typedef uint8_t audiou8_mono_t;

#endif
