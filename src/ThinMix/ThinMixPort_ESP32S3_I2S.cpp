/*
    ThinMix ESP32-S3 I2S TX port using ESP-IDF v5 standard I2S driver.

    Compile this file instead of ThinMixPort_PortAudio.cpp in an ESP-IDF build.
    The port writes interleaved signed 16-bit PCM to an I2S TX channel and lets
    i2s_channel_write() pace ThinMix's output thread.
*/

#include "ThinMixPort.h"
#include "ThinMixPort_ESP32S3_I2S.h"

#if !defined(ESP_PLATFORM)
#error "ThinMixPort_ESP32S3_I2S.cpp must be compiled inside ESP-IDF."
#endif

#include "driver/gpio.h"
#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include <atomic>
#include <cstring>
#include <new>

struct tm_port_device {
    i2s_chan_handle_t tx_chan;
    tm_port_config_t cfg;
    tm_esp32s3_i2s_config_t i2s_cfg;
    size_t bytes_per_frame;
    size_t write_chunk_bytes;
    uint32_t write_timeout_ms;
    bool channel_created;
    std::atomic<bool> started;
    std::atomic<bool> stopping;
};

static uint32_t tm_i2s_default_u32(uint32_t value, uint32_t fallback) {
    return value ? value : fallback;
}

static bool tm_i2s_config_is_valid(const tm_port_config_t *config,
                                   const tm_esp32s3_i2s_config_t *i2s_cfg) {
    if (!config || !i2s_cfg) return false;
    if (config->format.sample_rate == 0) return false;
    if (config->format.channels != TM_CHANNELS_MONO &&
        config->format.channels != TM_CHANNELS_STEREO) {
        return false;
    }
    if (config->block_frames == 0) return false;
    if (i2s_cfg->port_id != TM_ESP32S3_I2S_PORT_AUTO &&
        i2s_cfg->port_id != 0 &&
        i2s_cfg->port_id != 1) {
        return false;
    }
    if (i2s_cfg->bclk_gpio < 0 ||
        i2s_cfg->ws_gpio < 0 ||
        i2s_cfg->dout_gpio < 0) {
        return false;
    }
    return true;
}

void tm_esp32s3_i2s_config_default(tm_esp32s3_i2s_config_t *cfg) {
    if (!cfg) return;
    std::memset(cfg, 0, sizeof(*cfg));
    cfg->port_id = TM_ESP32S3_I2S_PORT_AUTO;
    cfg->bclk_gpio = TM_ESP32S3_I2S_GPIO_UNUSED;
    cfg->ws_gpio = TM_ESP32S3_I2S_GPIO_UNUSED;
    cfg->dout_gpio = TM_ESP32S3_I2S_GPIO_UNUSED;
    cfg->mclk_gpio = TM_ESP32S3_I2S_GPIO_UNUSED;
    cfg->dma_desc_num = 4;
    cfg->dma_frame_num = 0;
    cfg->write_timeout_ms = 100;
    cfg->write_chunk_frames = 0;
    cfg->use_apll = 0;
    cfg->auto_clear = 1;
}

int tm_port_get_caps(tm_port_caps_t *caps) {
    if (!caps) return -1;
    std::memset(caps, 0, sizeof(*caps));
    caps->preferred_sample_rate = 48000;
    caps->min_sample_rate = 8000;
    caps->max_sample_rate = 96000;
    caps->max_channels = TM_CHANNELS_STEREO;
    caps->preferred_block_frames = 128;
    return 0;
}

const char *tm_port_name(void) {
    return "ThinMix ESP32-S3 I2S TX port";
}

int tm_port_open(tm_port_device_t **out_device, const tm_port_config_t *config) {
    if (!out_device || !config) return -1;
    *out_device = 0;

    tm_esp32s3_i2s_config_t local_i2s_cfg;
    if (config->user) {
        local_i2s_cfg = *(const tm_esp32s3_i2s_config_t *)config->user;
    } else {
        tm_esp32s3_i2s_config_default(&local_i2s_cfg);
    }

    if (!tm_i2s_config_is_valid(config, &local_i2s_cfg)) {
        return -1;
    }

    tm_port_device_t *dev = new (std::nothrow) tm_port_device_t;
    if (!dev) return -1;

    dev->tx_chan = 0;
    dev->cfg = *config;
    dev->i2s_cfg = local_i2s_cfg;
    dev->bytes_per_frame = (size_t)config->format.channels * sizeof(int16_t);
    dev->write_chunk_bytes =
        (size_t)tm_i2s_default_u32(local_i2s_cfg.write_chunk_frames,
                                   config->block_frames) *
        dev->bytes_per_frame;
    dev->write_timeout_ms =
        local_i2s_cfg.write_timeout_ms ? local_i2s_cfg.write_timeout_ms : portMAX_DELAY;
    dev->channel_created = false;
    dev->started.store(false, std::memory_order_release);
    dev->stopping.store(false, std::memory_order_release);

    const i2s_port_t port_id = (local_i2s_cfg.port_id == TM_ESP32S3_I2S_PORT_AUTO)
                                   ? I2S_NUM_AUTO
                                   : (i2s_port_t)local_i2s_cfg.port_id;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(port_id, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = (int)tm_i2s_default_u32(local_i2s_cfg.dma_desc_num, 4);
    chan_cfg.dma_frame_num = (int)tm_i2s_default_u32(local_i2s_cfg.dma_frame_num,
                                                    config->block_frames);
    chan_cfg.auto_clear = (local_i2s_cfg.auto_clear != 0);

    if (i2s_new_channel(&chan_cfg, &dev->tx_chan, 0) != ESP_OK) {
        delete dev;
        return -1;
    }
    dev->channel_created = true;

    i2s_std_config_t std_cfg;
    std::memset(&std_cfg, 0, sizeof(std_cfg));
    std_cfg.clk_cfg.sample_rate_hz = config->format.sample_rate;
    std_cfg.clk_cfg.clk_src = local_i2s_cfg.use_apll ? I2S_CLK_SRC_APLL : I2S_CLK_SRC_DEFAULT;
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    std_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
    std_cfg.slot_cfg.slot_mode = (config->format.channels == TM_CHANNELS_MONO)
                                     ? I2S_SLOT_MODE_MONO
                                     : I2S_SLOT_MODE_STEREO;
    std_cfg.slot_cfg.slot_mask = (config->format.channels == TM_CHANNELS_MONO)
                                     ? I2S_STD_SLOT_LEFT
                                     : I2S_STD_SLOT_BOTH;
    std_cfg.slot_cfg.ws_width = 16;
    std_cfg.slot_cfg.ws_pol = false;
    std_cfg.slot_cfg.bit_shift = true;
    std_cfg.slot_cfg.left_align = false;
    std_cfg.slot_cfg.big_endian = false;
    std_cfg.slot_cfg.bit_order_lsb = false;
    std_cfg.gpio_cfg.mclk = (gpio_num_t)local_i2s_cfg.mclk_gpio;
    std_cfg.gpio_cfg.bclk = (gpio_num_t)local_i2s_cfg.bclk_gpio;
    std_cfg.gpio_cfg.ws = (gpio_num_t)local_i2s_cfg.ws_gpio;
    std_cfg.gpio_cfg.dout = (gpio_num_t)local_i2s_cfg.dout_gpio;
    std_cfg.gpio_cfg.din = GPIO_NUM_NC;
    std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.ws_inv = false;

    if (i2s_channel_init_std_mode(dev->tx_chan, &std_cfg) != ESP_OK) {
        i2s_del_channel(dev->tx_chan);
        delete dev;
        return -1;
    }

    *out_device = dev;
    return 0;
}

int tm_port_start(tm_port_device_t *device) {
    if (!device || !device->tx_chan) return -1;
    if (device->started.load(std::memory_order_acquire)) return 0;
    if (i2s_channel_enable(device->tx_chan) != ESP_OK) return -1;
    device->stopping.store(false, std::memory_order_release);
    device->started.store(true, std::memory_order_release);
    return 0;
}

int tm_port_write(tm_port_device_t *device, const int16_t *interleaved, uint32_t frames) {
    if (!device || !device->tx_chan || !interleaved || frames == 0 ||
        !device->started.load(std::memory_order_acquire) ||
        device->stopping.load(std::memory_order_acquire)) {
        return -1;
    }

    const size_t bytes = (size_t)frames * device->bytes_per_frame;
    const uint8_t *src = (const uint8_t *)interleaved;
    size_t total_written = 0;

    while (total_written < bytes) {
        if (!device->started.load(std::memory_order_acquire) ||
            device->stopping.load(std::memory_order_acquire)) {
            return -1;
        }

        size_t chunk_bytes = bytes - total_written;
        if (chunk_bytes > device->write_chunk_bytes) {
            chunk_bytes = device->write_chunk_bytes;
        }

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(device->tx_chan,
                                          src + total_written,
                                          chunk_bytes,
                                          &bytes_written,
                                          device->write_timeout_ms);
        if (err != ESP_OK || bytes_written == 0) {
            return -1;
        }
        total_written += bytes_written;
    }
    return 0;
}

int tm_port_stop(tm_port_device_t *device) {
    if (!device) return -1;
    device->stopping.store(true, std::memory_order_release);
    if (device->tx_chan && device->started.exchange(false, std::memory_order_acq_rel)) {
        i2s_channel_disable(device->tx_chan);
    }
    return 0;
}

int tm_port_close(tm_port_device_t *device) {
    if (!device) return -1;
    tm_port_stop(device);
    if (device->tx_chan && device->channel_created) {
        i2s_del_channel(device->tx_chan);
        device->tx_chan = 0;
        device->channel_created = false;
    }
    delete device;
    return 0;
}
