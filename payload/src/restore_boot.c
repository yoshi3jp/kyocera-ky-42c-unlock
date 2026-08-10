/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * KY-42C hash-locked boot partition restoration payload.
 *
 * Host protocol:
 *   1. CMD_SEND_IMAGE("boot", exact 32 MiB known-good boot partition image)
 *   2. CMD_SEND_IMAGE("lk", this payload)
 *   3. CMD_BOOT_IMAGE("lk")
 *
 * Public MT6761 preloader source stages the image named "boot" at 0x40080000.
 * This payload refuses to touch eMMC until the entire staged 32 MiB buffer has
 * the exact compiled-in SHA-256. It then parses GPT, requires the captured
 * KY-42C boot geometry, writes all 65536 sectors, immediately verifies each
 * sector, computes a SHA-256 over the verified read-back stream, and only
 * watchdog-resets if that final digest also matches.
 *
 * The watchdog is disabled at entry. Any validation/read/write/verification
 * failure therefore hangs without an automatic reset.
 */

#include <stdbool.h>
#include <types.h>
#include <libc.h>
#include <debug.h>
#include <drivers/uart.h>
#include <storage/mmc/mmc.h>
#include <crypto/sha256.h>
#include <kyocera.h>
#include <gpt.h>

extern uint8_t __bss_start[], __bss_end[];

__attribute__((used, section(".payload_end")))
u32 dummy = 0;

#define STAGED_BOOT_ADDR          0x40080000u
#define STAGED_BOOT_SIZE          0x02000000u
#define EXPECTED_BOOT_START_BLOCK 1704960u
#define EXPECTED_BOOT_BLOCKS      65536u
#define HASH_CHUNK_SIZE           0x00010000u
#define PROGRESS_INTERVAL_BLOCKS  1024u

static const u8 kExpectedSha256[SHA256_DIGEST_SIZE] = {
    0x11, 0xa8, 0x86, 0xe7, 0x46, 0x06, 0x5e, 0xd8,
    0x07, 0x10, 0x25, 0xdc, 0x68, 0xa6, 0x74, 0x99,
    0xb1, 0x4f, 0x94, 0x76, 0xce, 0x4b, 0x23, 0x02,
    0x33, 0x5e, 0x68, 0x82, 0x79, 0x71, 0x61, 0x63,
};

static struct mmc_dev g_mmc_dev;
static u8 verify_buf[MMC_BLOCK_SZ] __attribute__((aligned(64)));

static int mmc_read_block_cb(uint32_t blk, void *buf, void *ctx)
{
    return mmc_read_block((struct mmc_dev *)ctx, blk, buf);
}

static void platform_wdt_disable(void)
{
    volatile u32 *wdt = (volatile u32 *)WDT_BASE_ADDR;
    u32 mode = wdt[0];

    mode &= ~1u;
    mode |= 0x22000000u;
    wdt[0] = mode;
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
    printf("Watchdog remains disabled; NO automatic reboot will occur.\n");
    for (;;)
        ;
}

static int hash_memory(const u8 *base, u32 size,
                       u8 digest[SHA256_DIGEST_SIZE])
{
    sha256_t sha;
    u32 off;

    sha256_init(&sha);
    for (off = 0; off < size; off += HASH_CHUNK_SIZE) {
        u32 chunk = size - off;
        if (chunk > HASH_CHUNK_SIZE)
            chunk = HASH_CHUNK_SIZE;
        sha256_update(&sha, base + off, chunk);
    }
    sha256_final(&sha, digest);

    return memcmp(digest, kExpectedSha256, SHA256_DIGEST_SIZE);
}

__attribute__((section(".text.main"), used)) int main(void)
{
    static struct gpt_context gpt;
    const u8 *staged = (const u8 *)STAGED_BOOT_ADDR;
    struct part_info boot;
    sha256_t readback_sha;
    u8 staged_digest[SHA256_DIGEST_SIZE];
    u8 readback_digest[SHA256_DIGEST_SIZE];
    u32 i;

    memset(__bss_start, 0, __bss_end - __bss_start);
    printf_register_cb(uart_putc);
    platform_wdt_disable();

    printf("\nKY-42C boot partition restoration payload\n");
    printf("staged boot=0x%08lx size=%lu bytes\n",
           (unsigned long)STAGED_BOOT_ADDR,
           (unsigned long)STAGED_BOOT_SIZE);

    if (memcmp(staged, "ANDROID!", 8) != 0)
        fatal("staged buffer lacks ANDROID! boot magic");

    if (memcmp(staged + STAGED_BOOT_SIZE - 64u, "AVBf", 4) != 0)
        fatal("staged full-partition image lacks AVB footer magic");

    printf("Validating staged image SHA-256 before any eMMC write...\n");
    if (hash_memory(staged, STAGED_BOOT_SIZE, staged_digest) != 0)
        fatal("staged boot SHA-256 mismatch; refusing to write");

    printf("Staged image SHA-256: MATCH\n");

    mmc_dev_setup(&g_mmc_dev, MSDC0_BASE_ADDR, 1, NULL);

    if (gpt_parse(&gpt, mmc_read_block_cb, &g_mmc_dev) != 0)
        fatal("GPT parse failed");

    if (gpt_find(&gpt, "boot", &boot) != 0)
        fatal("boot partition not found");

    printf("boot: start=%lu blocks=%lu\n",
           (unsigned long)boot.start_block,
           (unsigned long)boot.size_blocks);

    if (boot.start_block != EXPECTED_BOOT_START_BLOCK)
        fatal("unexpected boot start LBA; refusing to write");

    if (boot.size_blocks != EXPECTED_BOOT_BLOCKS)
        fatal("unexpected boot partition size; refusing to write");

    if ((boot.size_blocks * MMC_BLOCK_SZ) != STAGED_BOOT_SIZE)
        fatal("boot geometry does not equal staged image size");

    printf("\nWRITING exact reviewed boot image with per-sector read-back...\n");
    sha256_init(&readback_sha);

    for (i = 0; i < boot.size_blocks; i++) {
        const u8 *src = staged + (i * MMC_BLOCK_SZ);
        u32 dst_lba = boot.start_block + i;

        if (mmc_write_block(&g_mmc_dev, dst_lba, src) != 0) {
            printf("write failed at boot LBA %lu\n", (unsigned long)dst_lba);
            fatal("boot write failure");
        }

        if (mmc_read_block(&g_mmc_dev, dst_lba, verify_buf) != 0) {
            printf("read-back failed at boot LBA %lu\n", (unsigned long)dst_lba);
            fatal("boot read-back failure");
        }

        if (memcmp(src, verify_buf, MMC_BLOCK_SZ) != 0) {
            printf("verify mismatch at boot LBA %lu\n", (unsigned long)dst_lba);
            fatal("boot sector verification mismatch");
        }

        sha256_update(&readback_sha, verify_buf, MMC_BLOCK_SZ);

        if (((i + 1u) % PROGRESS_INTERVAL_BLOCKS) == 0u ||
            (i + 1u) == boot.size_blocks) {
            printf("verified %lu / %lu sectors\n",
                   (unsigned long)(i + 1u),
                   (unsigned long)boot.size_blocks);
        }
    }

    sha256_final(&readback_sha, readback_digest);

    if (memcmp(readback_digest, kExpectedSha256, SHA256_DIGEST_SIZE) != 0)
        fatal("final full-partition read-back SHA-256 mismatch");

    printf("\nSUCCESS: boot restored and exact 32 MiB read-back SHA-256 MATCHED.\n");
    printf("Explicit watchdog reset now.\n");

    platform_wdt_reset();
    for (;;)
        ;
}
