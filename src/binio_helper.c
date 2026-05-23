#include "binio_helper.h"

void write_u8(FILE *file, uint8_t value) {
    fwrite(&value, 1, 1, file);
}

void write_u16_le(FILE *file, uint16_t value) {
    uint8_t buf[2];

    buf[0] = (uint8_t)(value >> 0);
    buf[1] = (uint8_t)(value >> 8);

    fwrite(buf, 1, 2, file);
}

void write_u32_le(FILE *file, uint32_t value) {
    uint8_t buf[4];

    buf[0] = (uint8_t)(value >> 0);
    buf[1] = (uint8_t)(value >> 8);
    buf[2] = (uint8_t)(value >> 16);
    buf[3] = (uint8_t)(value >> 24);

    fwrite(buf, 1, 4, file);
}

void write_u16_be(FILE *file, uint16_t value) {
    uint8_t buf[2];

    buf[0] = (uint8_t)(value >> 8);
    buf[1] = (uint8_t)(value >> 0);

    fwrite(buf, 1, 2, file);
}

void write_u32_be(FILE *file, uint32_t value) {
    uint8_t buf[4];

    buf[0] = (uint8_t)(value >> 24);
    buf[1] = (uint8_t)(value >> 16);
    buf[2] = (uint8_t)(value >> 8);
    buf[3] = (uint8_t)(value >> 0);

    fwrite(buf, 1, 4, file);
}

uint8_t read_u8(FILE *file) {
    uint8_t value = 0;

    fread(&value, 1, 1, file);

    return value;
}

uint16_t read_u16_le(FILE *file) {
    uint8_t buf[2] = {0};

    fread(buf, 1, 2, file);

    return ((uint16_t)buf[0] << 0) |
           ((uint16_t)buf[1] << 8);
}

uint32_t read_u32_le(FILE *file) {
    uint8_t buf[4] = {0};

    fread(buf, 1, 4, file);

    return ((uint32_t)buf[0] << 0)  |
           ((uint32_t)buf[1] << 8)  |
           ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[3] << 24);
}

uint16_t read_u16_be(FILE *file) {
    uint8_t buf[2] = {0};

    fread(buf, 1, 2, file);

    return ((uint16_t)buf[0] << 8) |
           ((uint16_t)buf[1] << 0);
}

uint32_t read_u32_be(FILE *file) {
    uint8_t buf[4] = {0};

    fread(buf, 1, 4, file);

    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  |
           ((uint32_t)buf[3] << 0);
}