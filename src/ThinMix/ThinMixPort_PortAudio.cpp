/*
    ThinMix Linux desktop port using PortAudio blocking output.

    ThinMix's output thread calls tm_port_write(), and this port writes to
    PortAudio with Pa_WriteStream(). To keep tm_stop() responsive, long writes
    are split into small chunks and tm_port_stop() aborts the PortAudio stream
    before ThinMix joins the output thread.

    Build example on Debian/Ubuntu:
        sudo apt install portaudio19-dev
        g++ -std=c++11 -O2 -pthread ThinMix.cpp ThinMixPort_PortAudio.cpp your_app.cpp -lportaudio -o your_app
*/

#include "ThinMixPort.h"
#include "ThinMixPort_PortAudio.h"

#include <portaudio.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <new>
#include <thread>

void tm_portaudio_config_default(tm_portaudio_config_t *cfg) {
    if (!cfg) return;
    cfg->output_device_index = TM_PA_DEVICE_DEFAULT;
    cfg->suggested_latency_seconds = 0.0;
    cfg->output_buffer_frames = 0;
    cfg->write_chunk_frames = 64;
}

struct tm_port_device {
    PaStream *stream;
    bool pa_initialized;

    tm_port_config_t cfg;
    uint16_t channels;
    uint32_t write_chunk_frames;

    std::atomic<bool> started;
    std::atomic<bool> stopping;
};

static uint32_t tm_pa_min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

static int tm_pa_ok(PaError err) {
    return (err == paNoError) ? 0 : -1;
}

static int tm_pa_is_nonfatal_write_result(PaError err) {
    return err == paNoError || err == paOutputUnderflowed;
}

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
    return "ThinMix PortAudio blocking-write port";
}

int tm_port_open(tm_port_device_t **out_device, const tm_port_config_t *config) {
    if (!out_device || !config) return -1;
    if (config->format.sample_rate == 0 || config->format.channels == 0) return -1;
    if (config->block_frames == 0) return -1;

    *out_device = 0;

    tm_port_device_t *dev = new (std::nothrow) tm_port_device_t;
    if (!dev) return -1;

    dev->stream = 0;
    dev->pa_initialized = false;
    dev->cfg = *config;
    dev->channels = config->format.channels;
    dev->write_chunk_frames = 64;
    dev->started.store(false, std::memory_order_release);
    dev->stopping.store(false, std::memory_order_release);

    const tm_portaudio_config_t *pa_cfg = (const tm_portaudio_config_t *)config->user;
    if (pa_cfg && pa_cfg->write_chunk_frames > 0) {
        dev->write_chunk_frames = pa_cfg->write_chunk_frames;
    }
    if (dev->write_chunk_frames == 0) dev->write_chunk_frames = 64;
    if (dev->write_chunk_frames > config->block_frames) {
        dev->write_chunk_frames = config->block_frames;
    }
    if (dev->write_chunk_frames == 0) dev->write_chunk_frames = 1;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        delete dev;
        return -1;
    }
    dev->pa_initialized = true;

    PaDeviceIndex device_index = paNoDevice;
    if (pa_cfg && pa_cfg->output_device_index >= 0) {
        device_index = (PaDeviceIndex)pa_cfg->output_device_index;
    } else {
        device_index = Pa_GetDefaultOutputDevice();
    }

    if (device_index == paNoDevice) {
        Pa_Terminate();
        delete dev;
        return -1;
    }

    const PaDeviceInfo *device_info = Pa_GetDeviceInfo(device_index);
    if (!device_info || config->format.channels > (uint16_t)device_info->maxOutputChannels) {
        Pa_Terminate();
        delete dev;
        return -1;
    }

    PaStreamParameters out_params;
    std::memset(&out_params, 0, sizeof(out_params));
    out_params.device = device_index;
    out_params.channelCount = (int)config->format.channels;
    out_params.sampleFormat = paInt16;
    out_params.suggestedLatency =
        (pa_cfg && pa_cfg->suggested_latency_seconds > 0.0)
            ? pa_cfg->suggested_latency_seconds
            : device_info->defaultLowOutputLatency;
    out_params.hostApiSpecificStreamInfo = 0;

    err = Pa_OpenStream(&dev->stream,
                        0,
                        &out_params,
                        (double)config->format.sample_rate,
                        (unsigned long)config->block_frames,
                        paClipOff,
                        0,
                        0);

    if (err != paNoError) {
        Pa_Terminate();
        delete dev;
        return -1;
    }

    *out_device = dev;
    return 0;
}

int tm_port_start(tm_port_device_t *device) {
    if (!device || !device->stream) return -1;

    device->stopping.store(false, std::memory_order_release);
    device->started.store(true, std::memory_order_release);

    PaError err = Pa_StartStream(device->stream);
    if (err != paNoError) {
        device->started.store(false, std::memory_order_release);
        return -1;
    }
    return 0;
}

int tm_port_write(tm_port_device_t *device, const int16_t *interleaved, uint32_t frames) {
    if (!device || !interleaved || frames == 0) return -1;
    if (!device->stream) return -1;
    if (!device->started.load(std::memory_order_acquire) ||
        device->stopping.load(std::memory_order_acquire)) {
        return -1;
    }

    const uint16_t channels = device->channels;
    const uint32_t max_chunk = device->write_chunk_frames ? device->write_chunk_frames : frames;
    uint32_t done = 0;

    while (done < frames) {
        if (!device->started.load(std::memory_order_acquire) ||
            device->stopping.load(std::memory_order_acquire)) {
            return -1;
        }

        const signed long writable = Pa_GetStreamWriteAvailable(device->stream);
        if (writable < 0) {
            return -1;
        }
        if (writable == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        uint32_t chunk = tm_pa_min_u32(max_chunk, frames - done);
        chunk = tm_pa_min_u32(chunk, (uint32_t)writable);
        const int16_t *src = &interleaved[(size_t)done * channels];

        PaError err = Pa_WriteStream(device->stream, src, (unsigned long)chunk);

        if (device->stopping.load(std::memory_order_acquire) ||
            !device->started.load(std::memory_order_acquire)) {
            return -1;
        }

        if (!tm_pa_is_nonfatal_write_result(err)) {
            return -1;
        }

        done += chunk;
    }

    return 0;
}

int tm_port_stop(tm_port_device_t *device) {
    if (!device) return -1;

    device->stopping.store(true, std::memory_order_release);
    device->started.store(false, std::memory_order_release);

    if (device->stream) {
        PaError err = Pa_AbortStream(device->stream);
        if (err == paNoError || err == paStreamIsStopped) return 0;
        return tm_pa_ok(err);
    }

    return 0;
}

int tm_port_close(tm_port_device_t *device) {
    if (!device) return -1;

    device->stopping.store(true, std::memory_order_release);
    device->started.store(false, std::memory_order_release);

    if (device->stream) {
        /* Ensure that close is not asked to drain pending audio. */
        Pa_AbortStream(device->stream);
        Pa_CloseStream(device->stream);
        device->stream = 0;
    }
    if (device->pa_initialized) {
        Pa_Terminate();
        device->pa_initialized = false;
    }
    delete device;
    return 0;
}
