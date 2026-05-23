#ifndef THINMIX_PORT_H
#define THINMIX_PORT_H

/*
    ThinMix platform port interface.

    ThinMix.cpp calls this interface when tm_config_t::use_output_thread is nonzero.
    Platform differences, hardware setup, DMA details, and host-audio-library calls
    should live in exactly one ThinMixPort_*.cpp file.

    The core mixer never includes ALSA/WASAPI/CoreAudio/PortAudio/I2S/HAL headers.
*/

#include "ThinMix.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tm_port_device tm_port_device_t;

typedef struct tm_port_config {
    tm_format_t format;
    uint16_t block_frames;
    uint16_t reserved0;
    uint32_t flags;
    void *user;
} tm_port_config_t;

typedef struct tm_port_caps {
    uint32_t preferred_sample_rate;
    uint32_t min_sample_rate;
    uint32_t max_sample_rate;
    uint16_t max_channels;
    uint16_t preferred_block_frames;
    uint32_t flags;
} tm_port_caps_t;

/* Optional capability query. Return 0 on success. */
int tm_port_get_caps(tm_port_caps_t *caps);

/* Human-readable static name of this port implementation. */
const char *tm_port_name(void);

/*
    Open/start/write/stop/close backend.

    tm_port_write() receives interleaved signed 16-bit PCM. It should normally
    block until the device or device queue accepts the requested frame count,
    because this paces ThinMix's output thread.

    Return 0 on success and a negative value on failure.
*/
int tm_port_open(tm_port_device_t **out_device, const tm_port_config_t *config);
int tm_port_start(tm_port_device_t *device);
int tm_port_write(tm_port_device_t *device, const int16_t *interleaved, uint32_t frames);
int tm_port_stop(tm_port_device_t *device);
int tm_port_close(tm_port_device_t *device);

#ifdef __cplusplus
}
#endif

#endif /* THINMIX_PORT_H */
