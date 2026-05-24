#ifndef WAV_H
#define WAV_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define WAV_OK                 0
#define WAV_ERR_OPEN          -1
#define WAV_ERR_IO            -2
#define WAV_ERR_FORMAT        -3
#define WAV_ERR_UNSUPPORTED   -4
#define WAV_ERR_PARAM         -5

#define WAV_MODE_NONE          0
#define WAV_MODE_READ          1
#define WAV_MODE_WRITE         2

class WAV {
private:
    FILE *fp = NULL;

    int mode = WAV_MODE_NONE;

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint32_t byte_rate = 0;
    uint16_t block_align = 0;
    uint16_t bits_per_sample = 0;

    uint32_t data_offset = 0;
    uint32_t data_size = 0;
    uint32_t data_pos = 0;
    uint32_t data_written = 0;

    static uint16_t read_u16_le(FILE *fp);
    static uint32_t read_u32_le(FILE *fp);

    static int write_u16_le(FILE *fp, uint16_t v);
    static int write_u32_le(FILE *fp, uint32_t v);

    static int read_fourcc(FILE *fp, char out[4]);
    static int write_fourcc(FILE *fp, const char *s);
    static int fourcc_eq(const char a[4], const char *b);

    static int skip(FILE *fp, uint32_t size);

public:
    WAV();
    ~WAV();

    void init();

    int open_read(const char *path);
    int open_write(const char *path, uint32_t sr, uint16_t ch);

    size_t read_frames_s16(int16_t *buf, size_t frames);
    size_t write_frames_s16(const int16_t *buf, size_t frames);

    int close();

    int get_mode();

    uint16_t get_audio_format();
    uint16_t get_channels();
    uint32_t get_sample_rate();
    uint32_t get_byte_rate();
    uint16_t get_block_align();
    uint16_t get_bits_per_sample();

    uint32_t get_data_bytes();
    uint32_t get_total_frames();
    uint32_t get_data_pos();

    static const char *result_string(int r);
};

#endif
