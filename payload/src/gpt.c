/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 R0rt1z2
 */

#include <gpt.h>
#include <debug.h>
#include <libc.h>

#define GUID_IS_ZERO(g) (memcmp((g), "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) == 0)

static uint8_t gpt_buf[GPT_BLOCK_SIZE] __attribute__((aligned(64)));

int gpt_parse(struct gpt_context *gpt, gpt_read_block_fn read_block, void *read_ctx)
{
    gpt->count = 0;

    if (read_block(GPT_HEADER_LBA, gpt_buf, read_ctx) != 0) {
        printf("GPT header read failed\n");
        return -1;
    }

    struct gpt_header *hdr = (struct gpt_header *)gpt_buf;

    if (memcmp(hdr->signature, "EFI PART", 8) != 0) {
        printf("Bad GPT signature\n");
        return -1;
    }

    uint32_t entry_count = hdr->entry_count;
    uint32_t entry_size  = hdr->entry_size;
    uint64_t entry_start = hdr->entry_start;

    if (entry_count > GPT_MAX_PARTS)
        entry_count = GPT_MAX_PARTS;

    uint32_t entries_per_block = GPT_BLOCK_SIZE / entry_size;
    uint32_t blocks_needed = (entry_count + entries_per_block - 1) / entries_per_block;

    for (uint32_t blk = 0; blk < blocks_needed; blk++) {
        if (read_block((uint32_t)(entry_start + blk), gpt_buf, read_ctx) != 0) {
            printf("GPT entry read failed at block %lu\n", (unsigned long)(entry_start + blk));
            return -1;
        }

        for (uint32_t j = 0; j < entries_per_block && gpt->count < (int)entry_count; j++) {
            struct gpt_entry *e = (struct gpt_entry *)(gpt_buf + j * entry_size);

            if (GUID_IS_ZERO(e->type_guid))
                continue;

            struct part_info *p = &gpt->parts[gpt->count];
            strnarrow(e->name, p->name, GPT_NAME_MAX);
            p->start_block = (uint32_t)e->first_lba;
            p->size_blocks = (uint32_t)(e->last_lba - e->first_lba + 1);
            gpt->count++;
        }
    }

    printf("Found %d GPT partitions:\n", gpt->count);
    gpt_dump(gpt);
    printf("\n");

    return 0;
}

int gpt_find(const struct gpt_context *gpt, const char *name,
             struct part_info *out)
{
    for (int i = 0; i < gpt->count; i++) {
        if (streq(gpt->parts[i].name, name)) {
            if (out)
                *out = gpt->parts[i];
            return 0;
        }
    }
    return -1;
}

uint32_t gpt_get_start(const struct gpt_context *gpt, const char *name)
{
    struct part_info info;
    if (gpt_find(gpt, name, &info) != 0)
        return 0;
    return info.start_block;
}

uint32_t gpt_get_size(const struct gpt_context *gpt, const char *name)
{
    struct part_info info;
    if (gpt_find(gpt, name, &info) != 0)
        return 0;
    return info.size_blocks;
}

void gpt_dump(const struct gpt_context *gpt)
{
    for (int i = 0; i < gpt->count; i++) {
        printf("  [%2d] %-20s start=%-8lu size=%lu\n",
               i, gpt->parts[i].name,
               gpt->parts[i].start_block,
               gpt->parts[i].size_blocks);
    }
}
