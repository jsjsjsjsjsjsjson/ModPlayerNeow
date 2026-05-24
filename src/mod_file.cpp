#include "mod_file.h"

void detect_mod_type(const char magic[4], int *samples, int *channels) {
    *samples = 15;
    *channels = 4;

    if (memcmp(magic, "M.K.", 4) == 0 ||
        memcmp(magic, "FLT4", 4) == 0) {
        *samples = 31;
        *channels = 4;
    }
    else if (memcmp(magic, "FLT8", 4) == 0) {
        *samples = 31;
        *channels = 8;
    }
    else if (isdigit((unsigned char)magic[0]) &&
             magic[1] == 'C' &&
             magic[2] == 'H' &&
             magic[3] == 'N') {
        *samples = 31;
        *channels = magic[0] - '0';
    }
    else if (isdigit((unsigned char)magic[0]) &&
             isdigit((unsigned char)magic[1]) &&
             magic[2] == 'C' &&
             magic[3] == 'H') {
        *samples = 31;
        *channels = (magic[0] - '0') * 10 + (magic[1] - '0');
    }
}

void write_mod_magic(char magic[4], int samples, int channels) {
    if (samples == 15) {
        memcpy(magic, "    ", 4);
        return;
    }

    if (samples != 31) {
        memcpy(magic, "    ", 4);
        return;
    }

    if (channels == 4) {
        memcpy(magic, "M.K.", 4);
    }
    else if (channels >= 0 && channels <= 9) {
        magic[0] = '0' + channels;
        magic[1] = 'C';
        magic[2] = 'H';
        magic[3] = 'N';
    }
    else if (channels >= 10 && channels <= 99) {
        magic[0] = '0' + channels / 10;
        magic[1] = '0' + channels % 10;
        magic[2] = 'C';
        magic[3] = 'H';
    }
    else {
        memcpy(magic, "    ", 4);
    }
}

int read_note_helper(FILE *file, note_t *note) {
    uint8_t b[4];

    if (fread(b, 1, 4, file) != 4) return -1;

    note->sample  = (uint8_t)((b[0] & 0xF0) | (b[2] >> 4));
    note->period  = (uint16_t)(((b[0] & 0x0F) << 8) | b[1]);
    note->efx_cmd = (uint8_t)(b[2] & 0x0F);
    note->efx_par = b[3];

    return 0;
}

uint8_t find_highest(const uint8_t *orders, uint8_t length) {
    uint8_t max = 0;

    for (uint8_t i = 0; i < length; i++) {
        if (orders[i] > max) max = orders[i];
    }

    return max;
}

int MOD_FILE::read_sample_metadata() {
    printf("Reading Sample metadata...\n");
    for (int i = 0; i < samples.size(); i++) {
        mod_sample_t &sample = samples[i];
        printf("Sample %d/%zu:\n", i+1, samples.size());

        fread(sample.name, 1, 22, f);
        printf("Name: %.22s\n", sample.name);

        sample.data.resize((uint32_t)read_u16_be(f) * 2);
        printf("Length: %zu\n", sample.data.size());

        sample.finetune = (int8_t)((read_u8(f) ^ 0x08) - 0x08);
        printf("Finetune: %d\n", sample.finetune);

        sample.volume = read_u8(f);
        printf("Default volume: %d\n", sample.volume);

        sample.loop_start = (uint32_t)read_u16_be(f) * 2;
        sample.loop_length = (uint32_t)read_u16_be(f) * 2;
        printf("Loop: Start on %u, length %u\n", sample.loop_start, sample.loop_length);

        printf("\n");
    }
    return 0;
}

int MOD_FILE::read_pattern() {
    for (int p = 0; p < patterns.size(); p++) {
        for (int r = 0; r < 64; r++) {
            for (int c = 0; c < patterns[p].size(); c++) {
                if (read_note_helper(f, &patterns[p][c][r])) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

int MOD_FILE::read_sample_data() {
    for (int i = 0; i < samples.size(); i++) {
        mod_sample_t &sample = samples[i];
        printf("Reading Sample #%02d... ", i+1);
        fflush(stdout);
        if (sample.data.size() == 0) {
            printf("EMPTY\n");
            continue;
        }
        size_t read_size = fread(sample.data.data(), 1, sample.data.size(), f);
        if (read_size != sample.data.size()) {
            printf("%zu FAIL\n", read_size);
            return -1;
        }
        printf("%zu OK\n", read_size);
    }
    return 0;
}

int MOD_FILE::open(const char *filename) {
    f = fopen(filename, "rb");
    if (f == NULL) {
        printf("fopen failed: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

int MOD_FILE::read(char *errbuf, size_t errbuf_size) {
    // detect mod file type
    int sample_count = 0;
    int channel_count = 0;
    fseek(f, 1080, SEEK_SET);
    if (fread(mod_magic, 1, 4, f) != 4) {
        snprintf(errbuf, errbuf_size, "Incomplete MOD file");
        return -1;
    }
    detect_mod_type(mod_magic, &sample_count, &channel_count);
    if ((!sample_count) || (!channel_count)) {
        snprintf(errbuf, errbuf_size, "Unsupported or unknown MOD file");
        return -1;
    }
    samples.resize(sample_count);
    printf("Magic = %.4s | %d Channels, up to %d Samples\n", mod_magic, channel_count, sample_count);
    fseek(f, 0, SEEK_SET);

    // read song name
    fread(songname, 1, 20, f);
    printf("Name: %.20s\n", songname);

    printf("\n");
    // read sample metadata
    read_sample_metadata();

    // read order list
    printf("Order:\n");
    order_list.resize(read_u8(f));
    read_u8(f);
    printf("Lenght: %zu\n", order_list.size());
    if (fread(order_list.data(), 1, order_list.size(), f) != order_list.size()) {
        snprintf(errbuf, errbuf_size, "Incomplete MOD file");
        return -1;
    }
    patterns.resize(find_highest(order_list.data(), order_list.size()) + 1);
    fseek(f, 128 - order_list.size(), SEEK_CUR);
    // print order list
    printf("[");
    for (uint8_t i = 0; i < order_list.size()-1; i++) {
        printf("%d, ", order_list[i]);
    }
    printf("%d]\n", order_list[order_list.size()-1]);
    printf("Number of Pattern: %zu\n\n", patterns.size());

    fseek(f, 4, SEEK_CUR);

    printf("ftell = %ld\n", ftell(f));
    // read patterns
    printf("Reading Patterns...\n");
    for (uint8_t p = 0; p < patterns.size(); p++) {
        patterns[p].resize(channel_count);
        for (int c = 0; c < channel_count; c++) {
            patterns[p][c].resize(64);
        }
    }
    if (read_pattern()) {
        snprintf(errbuf, errbuf_size, "Incomplete patterns data");
        return -1;
    }
    printf("\n");

    // read samples data
    printf("Reading samples data...\n");
    if (read_sample_data()) {
        snprintf(errbuf, errbuf_size, "Incomplete samples data");
        return -1;
    }
    printf("CUR: %ld, ", ftell(f));
    fseek(f, 0, SEEK_END);
    printf("END: %ld\n", ftell(f));
    printf("\n");

    // print all name of the samples
    printf("Internal Texts *\n");
    for (int i = 0; i < samples.size(); i++) {
        printf("#%02d: %.22s\n", i+1, samples[i].name);
    }

    return 0;
}

mod_sample_t *MOD_FILE::get_sample(int i) {
    if (i < samples.size()) {
        return &samples[i];
    } else {
        return NULL;
    }
}

uint32_t MOD_FILE::get_num_channel() {
    if (patterns.size() == 0) {
        return 0;
    }
    return patterns[0].size();
}

int MOD_FILE::get_pattern_size(int p) {
    if (p >= patterns.size()) {
        return -1;
    }
    return patterns[p][0].size();
}

note_t *MOD_FILE::get_note_raw(int p, int c, int r) {
    if ((p >= patterns.size())
            || (c >= patterns[p].size())
                || (r >= patterns[p][c].size()))
    {
        return NULL;
    }
    return &patterns[p][c][r];
}

note_t *MOD_FILE::get_note_order(int p, int c, int r) {
    if ((p >= order_list.size()) || (order_list[p] >= patterns.size())
            || (c >= patterns[order_list[p]].size())
                || (r >= patterns[order_list[p]][c].size()))
    {
        return NULL;
    }
    return &patterns[order_list[p]][c][r];
}

int MOD_FILE::get_order(int i) {
    if (i >= order_list.size()) return -1;
    return order_list[i];
}

int MOD_FILE::get_order_size() {
    return order_list.size();
}

void MOD_FILE::print_pattern_data(int p) {
    if (p < 0 || (size_t)p >= patterns.size()) {
        printf("invalid pattern: %d\n", p);
        return;
    }

    const auto& pattern = patterns[p];

    if (pattern.empty()) {
        printf("pattern %d is empty\n", p);
        return;
    }

    size_t channels = pattern.size();
    size_t rows = pattern[0].size();

    printf("Pattern %d\n", p);
    printf("Row |");

    for (size_t ch = 0; ch < channels; ch++) {
        printf(" Ch%02zu       |", ch);
    }

    printf("\n");

    for (size_t row = 0; row < rows; row++) {
        printf("%02zu  |", row);

        for (size_t ch = 0; ch < channels; ch++) {
            const note_t& n = pattern[ch][row];

            if (n.period)
                printf(" %03d", n.period);
            else
                printf(" ---");

            if (n.sample)
                printf(" %02d", n.sample);
            else
                printf(" --");

            if (n.efx_cmd || n.efx_par)
                printf(" %X%02X", n.efx_cmd, n.efx_par);
            else
                printf(" ---");

            printf(" |");
        }

        printf("\n");
    }
}
