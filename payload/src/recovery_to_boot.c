/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Emergency KY-42C rescue payload.
 *
 * Copies the entire GPT "recovery" partition to GPT "boot", one 512-byte
 * sector at a time, and immediately reads every written sector back for
 * verification. No other partition is modified.
 *
 * Designed to run through the existing Preloader CMD_SEND_IMAGE/CMD_BOOT_IMAGE
 * path used by kyocera-ky-42c-unlock.
 */

#include <stdbool.h>
#include <types.h>
#include <libc.h>
#include <debug.h>
#include <drivers/uart.h>
#include <storage/mmc/mmc.h>
#include <kyocera.h>
#include <gpt.h>

extern uint8_t __bss_start[], __bss_end[];

/* Keep BSS inside the payload image, matching the existing unlock payload. */
__attribute__((used, section(".payload_end")))
u32 dummy = 0;

#define EXPECTED_BOOT_BLOCKS     65536u
#define PROGRESS_INTERVAL_BLOCKS 1024u

static struct mmc_dev g_mmc_dev;
static u8 src_buf[MMC_BLOCK_SZ] __attribute__((aligned(64)));
static u8 verify_buf[MMC_BLOCK_SZ] __attribute__((aligned(64)));

static int mmc_read_block_cb(uint32_t blk, void *buf, void *ctx)
{
    return mmc_read_block((struct mmc_dev *)ctx, blk, buf);
}

static void platform_wdt_reset(void)
{
    volatile u32 *wdt = (volatile u32 *)WDT_BASE_ADDR;
    wdt[6] = 0x1971;
    wdt[0] = 0x22000014;
    wdt[5] = 0x1209;
}

static void fatal(const char *msg)
{
    printf("\nFATAL: %s\n", msg);
    printf("No automatic reboot will be attempted.\n");
    printf("Power-cycle the device and run the rescue payload again.\n");
    for (;;)
        ;
}

__attribute__((section(".text.main"), used)) int main(void)
{
    static struct gpt_context gpt;
    struct part_info recovery;
    struct part_info boot;
    uint32_t i;

    memset(__bss_start, 0, __bss_end - __bss_start);
    printf_register_cb(uart_putc);

    printf("\nKY-42C recovery -> boot rescue payload\n");
    printf("This payload writes ONLY the GPT boot partition.\n\n");

    mmc_dev_setup(&g_mmc_dev, MSDC0_BASE_ADDR, 1, NULL);

    if (gpt_parse(&gpt, mmc_read_block_cb, &g_mmc_dev) != 0)
        fatal("GPT parse failed");

    if (gpt_find(&gpt, "recovery", &recovery) != 0)
        fatal("recovery partition not found");

    if (gpt_find(&gpt, "boot", &boot) != 0)
        fatal("boot partition not found");

    printf("recovery: start=%lu blocks=%lu\n",
           (unsigned long)recovery.start_block,
           (unsigned long)recovery.size_blocks);
    printf("boot:     start=%lu blocks=%lu\n",
           (unsigned long)boot.start_block,
           (unsigned long)boot.size_blocks);

    if (recovery.size_blocks != boot.size_blocks)
        fatal("recovery/boot partition size mismatch");

    if (boot.size_blocks != EXPECTED_BOOT_BLOCKS)
        fatal("unexpected boot partition size; refusing to write");

    if (recovery.start_block == boot.start_block)
        fatal("recovery and boot resolve to the same LBA; refusing to write");

    printf("\nCopying %lu sectors (%lu MiB), with read-back verification...\n",
           (unsigned long)boot.size_blocks,
           (unsigned long)((boot.size_blocks * MMC_BLOCK_SZ) / (1024u * 1024u)));

    for (i = 0; i < boot.size_blocks; i++) {
        uint32_t src_lba = recovery.start_block + i;
        uint32_t dst_lba = boot.start_block + i;

        if (mmc_read_block(&g_mmc_dev, src_lba, src_buf) != 0) {
            printf("read failed: recovery LBA %lu\n", (unsigned long)src_lba);
            fatal("source read failure");
        }

        if (mmc_write_block(&g_mmc_dev, dst_lba, src_buf) != 0) {
            printf("write failed: boot LBA %lu\n", (unsigned long)dst_lba);
            fatal("destination write failure");
        }

        if (mmc_read_block(&g_mmc_dev, dst_lba, verify_buf) != 0) {
            printf("verify read failed: boot LBA %lu\n", (unsigned long)dst_lba);
            fatal("destination read-back failure");
        }

        if (memcmp(src_buf, verify_buf, MMC_BLOCK_SZ) != 0) {
            printf("verify mismatch: recovery LBA %lu -> boot LBA %lu\n",
                   (unsigned long)src_lba, (unsigned long)dst_lba);
            fatal("read-back verification mismatch");
        }

        if (((i + 1) % PROGRESS_INTERVAL_BLOCKS) == 0 ||
            (i + 1) == boot.size_blocks) {
            printf("verified %lu / %lu sectors\n",
                   (unsigned long)(i + 1),
                   (unsigned long)boot.size_blocks);
        }
    }

    printf("\nSUCCESS: recovery has been copied to boot and verified sector-by-sector.\n");
    printf("Rebooting into the copied recovery image...\n");

    platform_wdt_reset();
    for (;;)
        ;
}
