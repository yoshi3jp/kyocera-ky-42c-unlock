/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 R0rt1z2
 */

#pragma once

#include <inttypes.h>

#define GPT_MAX_PARTS    128
#define GPT_NAME_MAX     36
#define GPT_HEADER_LBA   1
#define GPT_ENTRY_LBA    2
#define GPT_BLOCK_SIZE   512

struct gpt_header {
    uint8_t  signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable;
    uint64_t last_usable;
    uint8_t  disk_guid[16];
    uint64_t entry_start;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t entry_crc;
};

struct gpt_entry {
    uint8_t  type_guid[16];
    uint8_t  part_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name[GPT_NAME_MAX];
};

struct part_info {
    char     name[GPT_NAME_MAX + 1];
    uint32_t start_block;
    uint32_t size_blocks;
};

struct gpt_context {
    struct part_info parts[GPT_MAX_PARTS];
    int              count;
};

typedef int (*gpt_read_block_fn)(uint32_t blk, void *buf, void *ctx);

static inline int streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a++ != *b++)
            return 0;
    }
    return *a == *b;
}

static inline void strnarrow(const uint16_t *src, char *dst, int max) {
    int i;
    for (i = 0; i < max && src[i]; i++)
        dst[i] = (char)(src[i] & 0x7F);
    dst[i] = '\0';
}

int      gpt_parse(struct gpt_context *gpt, gpt_read_block_fn read_block, void *read_ctx);
int      gpt_find(const struct gpt_context *gpt, const char *name,
                  struct part_info *out);
uint32_t gpt_get_start(const struct gpt_context *gpt, const char *name);
uint32_t gpt_get_size(const struct gpt_context *gpt, const char *name);
void     gpt_dump(const struct gpt_context *gpt);
