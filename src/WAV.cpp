#include "WAV.h"

#include <string.h>

WAV::WAV() {
    init();
}

WAV::~WAV() {
    close();
}

void WAV::init() {
    fp = NULL;

    mode = WAV_MODE_NONE;

    audio_format = 0;
    channels = 0;
    sample_rate = 0;
    byte_rate = 0;
    block_align = 0;
    bits_per_sample = 0;

    data_offset = 0;
    data_size = 0;
    data_pos = 0;
    data_written = 0;
}

uint16_t WAV::read_u16_le(FILE *fp) {
    uint8_t b[2];

    if (fread(b, 1, 2, fp) != 2)
        return 0;

    return (uint16_t)b[0] |
           ((uint16_t)b[1] << 8);
}

uint32_t WAV::read_u32_le(FILE *fp) {
    uint8_t b[4];

    if (fread(b, 1, 4, fp) != 4)
        return 0;

    return (uint32_t)b[0] |
           ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
}

int WAV::write_u16_le(FILE *fp, uint16_t v) {
    uint8_t b[2];

    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);

    return fwrite(b, 1, 2, fp) == 2 ? WAV_OK : WAV_ERR_IO;
}

int WAV::write_u32_le(FILE *fp, uint32_t v) {
    uint8_t b[4];

    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);

    return fwrite(b, 1, 4, fp) == 4 ? WAV_OK : WAV_ERR_IO;
}

int WAV::read_fourcc(FILE *fp, char out[4]) {
    return fread(out, 1, 4, fp) == 4 ? WAV_OK : WAV_ERR_IO;
}

int WAV::write_fourcc(FILE *fp, const char *s) {
    return fwrite(s, 1, 4, fp) == 4 ? WAV_OK : WAV_ERR_IO;
}

int WAV::fourcc_eq(const char a[4], const char *b) {
    return a[0] == b[0] &&
           a[1] == b[1] &&
           a[2] == b[2] &&
           a[3] == b[3];
}

int WAV::skip(FILE *fp, uint32_t size) {
    if (fseek(fp, (long)size, SEEK_CUR) != 0)
        return WAV_ERR_IO;

    return WAV_OK;
}

int WAV::open_read(const char *path) {
    char id[4];
    uint32_t riff_size;
    int found_fmt = 0;
    int found_data = 0;

    if (!path)
        return WAV_ERR_PARAM;

    close();
    init();

    fp = fopen(path, "rb");
    if (!fp)
        return WAV_ERR_OPEN;

    if (read_fourcc(fp, id) != WAV_OK)
        goto fmt_err;

    if (!fourcc_eq(id, "RIFF"))
        goto fmt_err;

    riff_size = read_u32_le(fp);
    (void)riff_size;

    if (read_fourcc(fp, id) != WAV_OK)
        goto fmt_err;

    if (!fourcc_eq(id, "WAVE"))
        goto fmt_err;

    while (!found_fmt || !found_data) {
        uint32_t chunk_size;
        uint32_t chunk_data_pos;
        uint32_t next_chunk_pos;

        if (read_fourcc(fp, id) != WAV_OK)
            break;

        chunk_size = read_u32_le(fp);
        chunk_data_pos = (uint32_t)ftell(fp);

        if (fourcc_eq(id, "fmt ")) {
            if (chunk_size < 16)
                goto fmt_err;

            audio_format = read_u16_le(fp);
            channels = read_u16_le(fp);
            sample_rate = read_u32_le(fp);
            byte_rate = read_u32_le(fp);
            block_align = read_u16_le(fp);
            bits_per_sample = read_u16_le(fp);

            if (chunk_size > 16) {
                if (skip(fp, chunk_size - 16) != WAV_OK)
                    goto io_err;
            }

            found_fmt = 1;
        } else if (fourcc_eq(id, "data")) {
            data_offset = (uint32_t)ftell(fp);
            data_size = chunk_size;
            found_data = 1;

            if (skip(fp, chunk_size) != WAV_OK)
                goto io_err;
        } else {
            if (skip(fp, chunk_size) != WAV_OK)
                goto io_err;
        }

        next_chunk_pos = chunk_data_pos + chunk_size + (chunk_size & 1);

        if ((uint32_t)ftell(fp) < next_chunk_pos) {
            if (fseek(fp, (long)next_chunk_pos, SEEK_SET) != 0)
                goto io_err;
        }
    }

    if (!found_fmt || !found_data)
        goto fmt_err;

    if (audio_format != 1)
        goto unsupported;

    if (bits_per_sample != 16)
        goto unsupported;

    if (channels == 0 || sample_rate == 0 || block_align == 0)
        goto fmt_err;

    if (fseek(fp, (long)data_offset, SEEK_SET) != 0)
        goto io_err;

    data_pos = 0;
    mode = WAV_MODE_READ;

    return WAV_OK;

unsupported:
    fclose(fp);
    init();
    return WAV_ERR_UNSUPPORTED;

fmt_err:
    fclose(fp);
    init();
    return WAV_ERR_FORMAT;

io_err:
    fclose(fp);
    init();
    return WAV_ERR_IO;
}

int WAV::open_write(const char *path, uint32_t sr, uint16_t ch) {
    if (!path)
        return WAV_ERR_PARAM;

    if (sr == 0 || ch == 0)
        return WAV_ERR_PARAM;

    close();
    init();

    fp = fopen(path, "wb");
    if (!fp)
        return WAV_ERR_OPEN;

    mode = WAV_MODE_WRITE;
    audio_format = 1;
    channels = ch;
    sample_rate = sr;
    bits_per_sample = 16;
    block_align = (uint16_t)(channels * 2);
    byte_rate = sample_rate * block_align;
    data_written = 0;

    if (write_fourcc(fp, "RIFF") != WAV_OK) goto io_err;
    if (write_u32_le(fp, 0) != WAV_OK) goto io_err;
    if (write_fourcc(fp, "WAVE") != WAV_OK) goto io_err;

    if (write_fourcc(fp, "fmt ") != WAV_OK) goto io_err;
    if (write_u32_le(fp, 16) != WAV_OK) goto io_err;
    if (write_u16_le(fp, audio_format) != WAV_OK) goto io_err;
    if (write_u16_le(fp, channels) != WAV_OK) goto io_err;
    if (write_u32_le(fp, sample_rate) != WAV_OK) goto io_err;
    if (write_u32_le(fp, byte_rate) != WAV_OK) goto io_err;
    if (write_u16_le(fp, block_align) != WAV_OK) goto io_err;
    if (write_u16_le(fp, bits_per_sample) != WAV_OK) goto io_err;

    if (write_fourcc(fp, "data") != WAV_OK) goto io_err;
    if (write_u32_le(fp, 0) != WAV_OK) goto io_err;

    data_offset = 44;

    return WAV_OK;

io_err:
    fclose(fp);
    init();
    return WAV_ERR_IO;
}

size_t WAV::read_frames_s16(int16_t *buf, size_t frames) {
    size_t i;
    size_t c;
    size_t done = 0;

    if (!buf)
        return 0;

    if (!fp || mode != WAV_MODE_READ)
        return 0;

    if (bits_per_sample != 16)
        return 0;

    for (i = 0; i < frames; i++) {
        if (data_pos + block_align > data_size)
            break;

        for (c = 0; c < channels; c++) {
            uint8_t b[2];

            if (fread(b, 1, 2, fp) != 2)
                return done;

            buf[i * channels + c] =
                (int16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));

            data_pos += 2;
        }

        done++;
    }

    return done;
}

size_t WAV::write_frames_s16(const int16_t *buf, size_t frames) {
    size_t i;
    size_t c;
    size_t done = 0;

    if (!buf)
        return 0;

    if (!fp || mode != WAV_MODE_WRITE)
        return 0;

    for (i = 0; i < frames; i++) {
        for (c = 0; c < channels; c++) {
            int16_t s = buf[i * channels + c];

            if (write_u16_le(fp, (uint16_t)s) != WAV_OK)
                return done;

            data_written += 2;
        }

        done++;
    }

    return done;
}

int WAV::close() {
    uint32_t riff_size;

    if (!fp) {
        init();
        return WAV_OK;
    }

    if (mode == WAV_MODE_WRITE) {
        riff_size = 36 + data_written;

        if (fseek(fp, 4, SEEK_SET) != 0)
            goto io_err;

        if (write_u32_le(fp, riff_size) != WAV_OK)
            goto io_err;

        if (fseek(fp, 40, SEEK_SET) != 0)
            goto io_err;

        if (write_u32_le(fp, data_written) != WAV_OK)
            goto io_err;
    }

    fclose(fp);
    init();

    return WAV_OK;

io_err:
    fclose(fp);
    init();
    return WAV_ERR_IO;
}

int WAV::get_mode() {
    return mode;
}

uint16_t WAV::get_audio_format() {
    return audio_format;
}

uint16_t WAV::get_channels() {
    return channels;
}

uint32_t WAV::get_sample_rate() {
    return sample_rate;
}

uint32_t WAV::get_byte_rate() {
    return byte_rate;
}

uint16_t WAV::get_block_align() {
    return block_align;
}

uint16_t WAV::get_bits_per_sample() {
    return bits_per_sample;
}

uint32_t WAV::get_data_bytes() {
    if (mode == WAV_MODE_WRITE)
        return data_written;

    return data_size;
}

uint32_t WAV::get_total_frames() {
    if (block_align == 0)
        return 0;

    return data_size / block_align;
}

uint32_t WAV::get_data_pos() {
    return data_pos;
}

const char *WAV::result_string(int r) {
    switch (r) {
    case WAV_OK:
        return "OK";

    case WAV_ERR_OPEN:
        return "Open failed";

    case WAV_ERR_IO:
        return "I/O error";

    case WAV_ERR_FORMAT:
        return "Invalid WAV format";

    case WAV_ERR_UNSUPPORTED:
        return "Unsupported WAV format";

    case WAV_ERR_PARAM:
        return "Invalid parameter";

    default:
        return "Unknown error";
    }
}
