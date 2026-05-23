#include "ThinMixPort.h"

#include <chrono>
#include <cstring>
#include <new>
#include <thread>

struct tm_port_device {
    tm_port_config_t cfg;
    bool started;
};

int tm_port_get_caps(tm_port_caps_t *caps) {
    if (!caps) return -1;
    std::memset(caps, 0, sizeof(*caps));
    caps->preferred_sample_rate = 48000;
    caps->min_sample_rate = 8000;
    caps->max_sample_rate = 192000;
    caps->max_channels = TM_MAX_CHANNELS;
    caps->preferred_block_frames = 128;
    return 0;
}

const char *tm_port_name(void) {
    return "ThinMix null timing port";
}

int tm_port_open(tm_port_device_t **out_device, const tm_port_config_t *config) {
    if (!out_device || !config || config->format.sample_rate == 0 || config->format.channels == 0) return -1;
    *out_device = 0;

    tm_port_device_t *dev = new (std::nothrow) tm_port_device_t;
    if (!dev) return -1;

    dev->cfg = *config;
    dev->started = false;
    *out_device = dev;
    return 0;
}

int tm_port_start(tm_port_device_t *device) {
    if (!device) return -1;
    device->started = true;
    return 0;
}

int tm_port_write(tm_port_device_t *device, const int16_t *interleaved, uint32_t frames) {
    (void)interleaved;
    if (!device || !device->started || frames == 0) return -1;

    const uint32_t sr = device->cfg.format.sample_rate;
    uint32_t ms = (uint32_t)(((uint64_t)frames * 1000u) / sr);
    if (ms == 0) ms = 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return 0;
}

int tm_port_stop(tm_port_device_t *device) {
    if (!device) return -1;
    device->started = false;
    return 0;
}

int tm_port_close(tm_port_device_t *device) {
    if (!device) return -1;
    delete device;
    return 0;
}
