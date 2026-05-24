#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <vector>
#include "binio_helper.h"

void detect_mod_type(const char magic[4], int *samples, int *channels);
void write_mod_magic(char magic[4], int samples, int channels);

struct note_t {
    uint16_t period = 0;
    uint8_t sample = 0;
    uint8_t efx_cmd = 0;
    uint8_t efx_par = 0;
};

int read_note_helper(FILE *file, note_t *note);

struct mod_sample_t {
    char name[22] = "";
    int8_t finetune = 0;
    uint8_t volume = 0;
    uint32_t loop_start = 0;
    uint32_t loop_length = 0;
    std::vector<int8_t> data;
};

uint8_t find_highest(const uint8_t *orders, uint8_t length);

class MOD_FILE {
private:
    FILE *f = NULL;

    char mod_magic[4];

    char songname[20] = "NEW SONG";
    std::vector<mod_sample_t> samples;
    std::vector<uint8_t> order_list;
    std::vector<std::vector<std::vector<note_t>>> patterns; // patterns[pattern][channel][row]

    int read_sample_metadata();
    int read_pattern();
    int read_sample_data();

public:
    int open(const char *filename);
    int read(char *errbuf, size_t errbuf_size);

    mod_sample_t *get_sample(int i);
    uint32_t get_num_channel();
    int get_pattern_size(int p); // Even though the mod format is fixed at 64 rows per pattern, but i have OCD :P
    note_t *get_note_raw(int p, int c, int r); // Get note with raw patterns order
    note_t *get_note_order(int p, int c, int r); // Get note with Order list
    int get_order(int i); // Get the Order list, failed with return -1
    int get_order_size();

    void print_pattern_data(int p);
};
