#ifndef BINIO_H
#define BINIO_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void write_u8(FILE *file, uint8_t value);

void write_u16_le(FILE *file, uint16_t value);
void write_u32_le(FILE *file, uint32_t value);

void write_u16_be(FILE *file, uint16_t value);
void write_u32_be(FILE *file, uint32_t value);

uint8_t  read_u8(FILE *file);

uint16_t read_u16_le(FILE *file);
uint32_t read_u32_le(FILE *file);

uint16_t read_u16_be(FILE *file);
uint32_t read_u32_be(FILE *file);

#ifdef __cplusplus
}
#endif

#endif