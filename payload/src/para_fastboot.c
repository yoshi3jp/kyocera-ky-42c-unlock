/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Emergency KY-42C rescue payload.
 *
 * Writes only the first 32-byte bootloader_message command field in the GPT
 * "para" partition, setting it to "bootonce-bootloader" while preserving the
 * rest of the first 512-byte sector byte-for-byte. The sector is read back and
 * verified before a watchdog reset is requested.
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

/* Keep BSS inside the payload image, matching the existing rescue payload. */
__attribute__((used, section(".payload_end")))
u32 dummy = 0;

#define EXPECTED_PARA_START_BLOCK 67648u
#define EXPECTED_PARA_BLOCKS      1024u
#define BCB_COMMAND_SIZE           32u

static const char kFastbootCommand[] = "bootonce-bootloader";

static struct mmc_dev g_mmc_dev;
static u8 sector_buf[MMC_BLOCK_SZ] __attribute__((aligned(64)));
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
    struct part_info para;
    uint32_t para_lba;

    memset(__bss_start, 0, __bss_end - __bss_start);
    printf_register_cb(uart_putc);

    printf("\nKY-42C para -> fastboot rescue payload\n");
    printf("This payload writes ONLY para sector 0, command[0..31].\n\n");

    mmc_dev_setup(&g_mmc_dev, MSDC0_BASE_ADDR, 1, NULL);

    if (gpt_parse(&gpt, mmc_read_block_cb, &g_mmc_dev) != 0)
        fatal("GPT parse failed");

    if (gpt_find(&gpt, "para", &para) != 0)
        fatal("para partition not found");

    printf("para: start=%lu blocks=%lu\n",
           (unsigned long)para.start_block,
           (unsigned long)para.size_blocks);

    /* Strict identity checks against the captured KY-42C GPT. */
    if (para.start_block != EXPECTED_PARA_START_BLOCK)
        fatal("unexpected para start LBA; refusing to write");

    if (para.size_blocks != EXPECTED_PARA_BLOCKS)
        fatal("unexpected para partition size; refusing to write");

    if (sizeof(kFastbootCommand) > BCB_COMMAND_SIZE)
        fatal("fastboot command does not fit bootloader_message.command");

    para_lba = para.start_block;

    if (mmc_read_block(&g_mmc_dev, para_lba, sector_buf) != 0)
        fatal("failed to read para sector 0");

    /* Preserve status/recovery/stage/reserved and all other sector bytes. */
    memset(sector_buf, 0, BCB_COMMAND_SIZE);
    memcpy(sector_buf, kFastbootCommand, sizeof(kFastbootCommand));

    printf("Writing BCB command: %s\n", kFastbootCommand);

    if (mmc_write_block(&g_mmc_dev, para_lba, sector_buf) != 0)
        fatal("failed to write para sector 0");

    if (mmc_read_block(&g_mmc_dev, para_lba, verify_buf) != 0)
        fatal("failed to read back para sector 0");

    if (memcmp(sector_buf, verify_buf, MMC_BLOCK_SZ) != 0)
        fatal("para sector read-back verification mismatch");

    if (memcmp(verify_buf, kFastbootCommand, sizeof(kFastbootCommand)) != 0)
        fatal("BCB command verification mismatch");

    printf("\nSUCCESS: para command set and full sector verified.\n");
    printf("Rebooting; LK should consume bootonce-bootloader and enter fastboot.\n");

    platform_wdt_reset();
    for (;;)
        ;
}
