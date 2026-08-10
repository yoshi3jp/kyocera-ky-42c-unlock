/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * KY-42C staged boot transport verifier.
 *
 * The host first sends a known-good full 32 MiB boot partition image using
 * Preloader CMD_SEND_IMAGE with image name "boot". Public MT6761 preloader
 * source places that buffer at 0x40080000. This payload is then sent as "lk"
 * and executed. It performs no eMMC writes: it validates the staged buffer's
 * Android boot/AVB markers and exact SHA-256, then watchdog-resets only on
 * success. The watchdog is disabled at entry so a later reboot is an explicit
 * success signal rather than a timeout.
 */

#include <stdbool.h>
#include <types.h>
#include <libc.h>
#include <debug.h>
#include <drivers/uart.h>
#include <crypto/sha256.h>
#include <kyocera.h>

extern uint8_t __bss_start[], __bss_end[];

__attribute__((used, section(".payload_end")))
u32 dummy = 0;

#define STAGED_BOOT_ADDR 0x40080000u
#define STAGED_BOOT_SIZE 0x02000000u
#define HASH_CHUNK_SIZE  0x00010000u

static const u8 kExpectedSha256[SHA256_DIGEST_SIZE] = {
    0x11, 0xa8, 0x86, 0xe7, 0x46, 0x06, 0x5e, 0xd8,
    0x07, 0x10, 0x25, 0xdc, 0x68, 0xa6, 0x74, 0x99,
    0xb1, 0x4f, 0x94, 0x76, 0xce, 0x4b, 0x23, 0x02,
    0x33, 0x5e, 0x68, 0x82, 0x79, 0x71, 0x61, 0x63,
};

static void platform_wdt_disable(void)
{
    volatile u32 *wdt = (volatile u32 *)WDT_BASE_ADDR;
    u32 mode = wdt[0];

    mode &= ~1u;          /* MTK_WDT_MODE_ENABLE */
    mode |= 0x22000000u;  /* MTK_WDT_MODE_KEY */
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
    printf("Watchdog remains disabled; this payload will NOT reboot on failure.\n");
    for (;;)
        ;
}

static int hash_staged_boot(u8 digest[SHA256_DIGEST_SIZE])
{
    const u8 *base = (const u8 *)STAGED_BOOT_ADDR;
    sha256_t sha;
    u32 off;

    sha256_init(&sha);
    for (off = 0; off < STAGED_BOOT_SIZE; off += HASH_CHUNK_SIZE)
        sha256_update(&sha, base + off, HASH_CHUNK_SIZE);
    sha256_final(&sha, digest);

    return memcmp(digest, kExpectedSha256, SHA256_DIGEST_SIZE);
}

__attribute__((section(".text.main"), used)) int main(void)
{
    const u8 *boot = (const u8 *)STAGED_BOOT_ADDR;
    u8 digest[SHA256_DIGEST_SIZE];

    memset(__bss_start, 0, __bss_end - __bss_start);
    printf_register_cb(uart_putc);
    platform_wdt_disable();

    printf("\nKY-42C staged boot transport verifier\n");
    printf("No eMMC writes will be performed.\n");
    printf("buffer=0x%08lx size=%lu bytes\n",
           (unsigned long)STAGED_BOOT_ADDR,
           (unsigned long)STAGED_BOOT_SIZE);

    if (memcmp(boot, "ANDROID!", 8) != 0)
        fatal("staged buffer does not begin with ANDROID! boot magic");

    if (memcmp(boot + STAGED_BOOT_SIZE - 64u, "AVBf", 4) != 0)
        fatal("staged full-partition image does not end with AVB footer magic");

    printf("Hashing staged 32 MiB buffer...\n");
    if (hash_staged_boot(digest) != 0)
        fatal("staged boot SHA-256 mismatch");

    printf("\nSUCCESS: staged boot image SHA-256 matches the reviewed image.\n");
    printf("Explicit watchdog reset now proves the large transport completed.\n");

    platform_wdt_reset();
    for (;;)
        ;
}
