#ifndef THINMIX_PORT_ESP32S3_I2S_H
#define THINMIX_PORT_ESP32S3_I2S_H

#include "ThinMix.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TM_ESP32S3_I2S_PORT_AUTO (-1)
#define TM_ESP32S3_I2S_GPIO_UNUSED (-1)

/*
    Optional ESP32-S3 I2S TX port configuration.
    Assign its address to tm_config_t::port_user before tm_create().

    This port uses ESP-IDF v5's standard I2S TX driver. ThinMix's output thread
    calls tm_mix(), then this port writes interleaved signed 16-bit PCM to I2S.

    Typical external-DAC wiring:
        bclk_gpio -> BCLK / SCK
        ws_gpio   -> LRCLK / WS
        dout_gpio -> DIN / SD
        mclk_gpio -> MCLK, or TM_ESP32S3_I2S_GPIO_UNUSED when not needed

    dma_frame_num is the number of audio frames per DMA buffer. 0 chooses
    config->block_frames. dma_desc_num is the DMA buffer count. 0 chooses 4.

    write_chunk_frames controls how many frames this port gives to one
    i2s_channel_write() call. 0 writes the whole ThinMix block in one call,
    which is the lowest-overhead mode. Smaller values can make tm_stop()
    observe the stop flag sooner when using a long write timeout.
*/
typedef struct tm_esp32s3_i2s_config {
    int port_id;              /* TM_ESP32S3_I2S_PORT_AUTO, 0, or 1 */
    int bclk_gpio;            /* bit clock GPIO */
    int ws_gpio;              /* word-select / LRCLK GPIO */
    int dout_gpio;            /* serial data output GPIO */
    int mclk_gpio;            /* MCLK GPIO, or TM_ESP32S3_I2S_GPIO_UNUSED */

    uint32_t dma_desc_num;    /* 0: automatic, currently 4 */
    uint32_t dma_frame_num;   /* 0: use ThinMix block_frames */
    uint32_t write_timeout_ms;/* per i2s_channel_write wait, 0: portMAX_DELAY */
    uint32_t write_chunk_frames; /* 0: whole ThinMix block per write call */

    uint8_t use_apll;         /* nonzero: prefer APLL clock source */
    uint8_t auto_clear;       /* nonzero: clear DMA buffer on underrun */
    uint16_t reserved0;
} tm_esp32s3_i2s_config_t;

void tm_esp32s3_i2s_config_default(tm_esp32s3_i2s_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* THINMIX_PORT_ESP32S3_I2S_H */
