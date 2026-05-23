#ifndef THINMIX_PORT_PORTAUDIO_H
#define THINMIX_PORT_PORTAUDIO_H

#include "ThinMix.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TM_PA_DEVICE_DEFAULT (-1)

/*
    Optional PortAudio port configuration.
    Assign its address to tm_config_t::port_user before tm_create().

    This port uses PortAudio blocking output, not a PortAudio callback.
    ThinMix's own output thread calls tm_mix() and then Pa_WriteStream().

    write_chunk_frames limits each individual Pa_WriteStream() call. Smaller
    chunks make tm_stop() return sooner because the output thread checks the
    stop flag between chunks. 0 means automatic, currently 64 frames.

    output_buffer_frames is kept only for source compatibility with ThinMix
    0.5.1 callback-FIFO code. It is ignored by this blocking-write port.
*/
typedef struct tm_portaudio_config {
    int output_device_index;          /* TM_PA_DEVICE_DEFAULT or PortAudio device index */
    double suggested_latency_seconds; /* <= 0: use device defaultLowOutputLatency */
    uint32_t output_buffer_frames;    /* deprecated; ignored in blocking-write mode */
    uint32_t write_chunk_frames;      /* 0: automatic, max frames per Pa_WriteStream call */
} tm_portaudio_config_t;

void tm_portaudio_config_default(tm_portaudio_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* THINMIX_PORT_PORTAUDIO_H */
