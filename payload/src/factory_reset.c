/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * KY-42C factory-reset filesystem-signature wipe payload.
 *
 * This payload intentionally does NOT erase whole partitions. It zeros only
 * the leading sectors needed to make Android treat the three formattable
 * filesystems as wiped on the next boot:
 *
 *   cache     /cache     ext4  : first  8 sectors (4 KiB)
 *   userdata  /data      f2fs  : first 16 sectors (8 KiB)
 *   md_udc    /metadata  ext4  : first  8 sectors (4 KiB)
 *
 * The F2FS range covers both superblock-containing 4 KiB blocks. The ext4
 * ranges cover the primary superblock and make the first 4 KiB all-zero.
 *
 * Safety properties:
 *   - GPT is parsed at runtime.
 *   - Every target must exactly match the captured KY-42C start LBA and size.
 *   - The separate GPT partition named "metadata" is validated but untouched.
 *   - FRP is validated but untouched.
 *   - Every written sector is read back and required to be all-zero.
 *   - The watchdog is disabled at entry; any error hangs without reboot.
 *   - Only after all three ranges verify does the payload watchdog-reset.
 *
 * This permanently destroys Android user data by invalidating /data and is
 * intended only for devices whose owner has explicitly chosen a factory reset.
 */

#include <types.h>
#include <libc.h>
#include <debug.h>
#include <drivers/uart.h>
#include <storage/mmc/mmc.h>
#include <kyocera.h>
#include <gpt.h>

extern uint8_t __bss_start[], __bss_end[];

__attribute__((used, section(".payload_end")))
u32 dummy = 0;

#define EXPECTED_MD_UDC_START       341056u
#define EXPECTED_MD_UDC_BLOCKS       46288u
#define EXPECTED_METADATA_START     387344u
#define EXPECTED_METADATA_BLOCKS     65536u
#define EXPECTED_FRP_START          142400u
#define EXPECTED_FRP_BLOCKS           2048u
#define EXPECTED_CACHE_START       5849088u
#define EXPECTED_CACHE_BLOCKS       720896u
#define EXPECTED_USERDATA_START    6569984u
#define EXPECTED_USERDATA_BLOCKS   8579039u

#define EXT4_WIPE_BLOCKS                 8u
#define F2FS_WIPE_BLOCKS                16u

struct wipe_spec {
    const char *name;
    u32 expected_start;
    u32 expected_blocks;
    u32 wipe_blocks;
};

/*
 * Do the least consequential target first and /metadata last. If external
 * power disappears during the operation, this ordering avoids invalidating
 * /metadata before /data has been invalidated.
 */
static const struct wipe_spec kTargets[] = {
    { "cache",    EXPECTED_CACHE_START,    EXPECTED_CACHE_BLOCKS,    EXT4_WIPE_BLOCKS },
    { "userdata", EXPECTED_USERDATA_START, EXPECTED_USERDATA_BLOCKS, F2FS_WIPE_BLOCKS },
    { "md_udc",   EXPECTED_MD_UDC_START,   EXPECTED_MD_UDC_BLOCKS,   EXT4_WIPE_BLOCKS },
};

#define TARGET_COUNT (sizeof(kTargets) / sizeof(kTargets[0]))

static struct mmc_dev g_mmc_dev;
static u8 zero_buf[MMC_BLOCK_SZ] __attribute__((aligned(64)));
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

static void require_geometry(const struct gpt_context *gpt,
                             const char *name,
                             u32 expected_start,
                             u32 expected_blocks,
                             struct part_info *out)
{
    struct part_info p;

    if (gpt_find(gpt, name, &p) != 0) {
        printf("required GPT partition '%s' not found\n", name);
        fatal("GPT identity check failed");
    }

    printf("%-8s start=%lu blocks=%lu\n",
           name,
           (unsigned long)p.start_block,
           (unsigned long)p.size_blocks);

    if (p.start_block != expected_start || p.size_blocks != expected_blocks) {
        printf("expected start=%lu blocks=%lu\n",
               (unsigned long)expected_start,
               (unsigned long)expected_blocks);
        fatal("unexpected KY-42C partition geometry; refusing to write");
    }

    if (out)
        *out = p;
}

static void wipe_and_verify(const struct part_info *p,
                            const struct wipe_spec *spec)
{
    u32 i;

    if (spec->wipe_blocks == 0 || spec->wipe_blocks > p->size_blocks)
        fatal("invalid wipe range");

    printf("\nZERO %-8s: first %lu sectors (%lu bytes)\n",
           spec->name,
           (unsigned long)spec->wipe_blocks,
           (unsigned long)(spec->wipe_blocks * MMC_BLOCK_SZ));

    for (i = 0; i < spec->wipe_blocks; i++) {
        u32 lba = p->start_block + i;

        if (mmc_write_block(&g_mmc_dev, lba, zero_buf) != 0) {
            printf("write failed at LBA %lu\n", (unsigned long)lba);
            fatal("eMMC write failure");
        }

        if (mmc_read_block(&g_mmc_dev, lba, verify_buf) != 0) {
            printf("read-back failed at LBA %lu\n", (unsigned long)lba);
            fatal("eMMC read-back failure");
        }

        if (memcmp(zero_buf, verify_buf, MMC_BLOCK_SZ) != 0) {
            printf("verify mismatch at LBA %lu\n", (unsigned long)lba);
            fatal("zero-sector verification mismatch");
        }
    }

    printf("%-8s: %lu sectors zeroed and verified\n",
           spec->name, (unsigned long)spec->wipe_blocks);
}

static void verify_zero_range(const struct part_info *p,
                              const struct wipe_spec *spec)
{
    u32 i;

    for (i = 0; i < spec->wipe_blocks; i++) {
        u32 lba = p->start_block + i;

        if (mmc_read_block(&g_mmc_dev, lba, verify_buf) != 0) {
            printf("final read-back failed at LBA %lu\n", (unsigned long)lba);
            fatal("final eMMC read-back failure");
        }

        if (memcmp(zero_buf, verify_buf, MMC_BLOCK_SZ) != 0) {
            printf("final verify mismatch at LBA %lu\n", (unsigned long)lba);
            fatal("final zero-range verification mismatch");
        }
    }
}

__attribute__((section(".text.main"), used)) int main(void)
{
    static struct gpt_context gpt;
    struct part_info targets[TARGET_COUNT];
    struct part_info preserved;
    u32 i;

    memset(__bss_start, 0, __bss_end - __bss_start);
    printf_register_cb(uart_putc);
    platform_wdt_disable();

    printf("\nKY-42C factory-reset filesystem-signature wipe\n");
    printf("DESTRUCTIVE: Android /data will be irrecoverably invalidated.\n");
    printf("FRP, GPT 'metadata', NVRAM/NVDATA/NVCFG, persist and firmware are untouched.\n\n");

    mmc_dev_setup(&g_mmc_dev, MSDC0_BASE_ADDR, 1, NULL);

    if (gpt_parse(&gpt, mmc_read_block_cb, &g_mmc_dev) != 0)
        fatal("GPT parse failed");

    /* Validate preserved partitions explicitly to prevent metadata-name mixups. */
    require_geometry(&gpt, "frp",
                     EXPECTED_FRP_START, EXPECTED_FRP_BLOCKS, &preserved);
    require_geometry(&gpt, "metadata",
                     EXPECTED_METADATA_START, EXPECTED_METADATA_BLOCKS, &preserved);

    for (i = 0; i < TARGET_COUNT; i++) {
        require_geometry(&gpt,
                         kTargets[i].name,
                         kTargets[i].expected_start,
                         kTargets[i].expected_blocks,
                         &targets[i]);
    }

    printf("\nGeometry checks passed. Beginning signature wipe.\n");

    for (i = 0; i < TARGET_COUNT; i++)
        wipe_and_verify(&targets[i], &kTargets[i]);

    printf("\nPerforming final read-back verification of all modified sectors...\n");
    for (i = 0; i < TARGET_COUNT; i++)
        verify_zero_range(&targets[i], &kTargets[i]);

    printf("\nSUCCESS: reset-trigger ranges are zero and verified.\n");
    printf("Modified only: cache[0..7], userdata[0..15], md_udc[0..7].\n");
    printf("FRP and GPT partition 'metadata' were preserved.\n");
    printf("Resetting now; stock Android should recreate formattable filesystems.\n");

    platform_wdt_reset();
    for (;;)
        ;
}
