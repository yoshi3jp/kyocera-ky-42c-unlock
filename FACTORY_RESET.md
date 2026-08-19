# KY-42C preloader factory-reset payload

The `factory-reset` command is a deliberately small destructive reset path for a KYOCERA KY-42C whose owner accepts loss of all Android user data.

It does **not** erase entire partitions. Instead, the transient preloader payload parses GPT, validates the exact captured KY-42C geometry, and zeros only the filesystem-identification ranges required to make Android treat its formattable filesystems as wiped on the next boot:

| Partition | Android mount | Filesystem | Sectors zeroed | Bytes |
|---|---|---|---:|---:|
| `cache` | `/cache` | ext4 | first 8 | 4096 |
| `userdata` | `/data` | F2FS | first 16 | 8192 |
| `md_udc` | `/metadata` | ext4 | first 8 | 4096 |

The F2FS 8 KiB range covers both superblock-containing 4 KiB blocks. The ext4 4 KiB ranges cover the primary superblock and make the first 4096 bytes all-zero.

The payload explicitly validates but does **not** modify `frp` or the separate GPT partition named `metadata`. It also does not touch `nvram`, `nvdata`, `nvcfg`, `persist`, bootloader/firmware partitions, GPT, eMMC boot areas, or RPMB.

Every sector written is immediately read back and compared with a zero-filled sector. A second complete read-back pass is performed before reboot. The watchdog is disabled at payload entry; if any geometry, read, write, or verification check fails, the payload hangs rather than automatically rebooting.

Build it with:

```bash
make factory-reset
```

The resulting preloader image is:

```text
bin/factory-reset.bin
```

Run it with the explicit destructive confirmation flag:

```bash
python3 main.py factory-reset --yes-really-reset
```

Power the handset off and connect it so that the MediaTek preloader interface (`0E8D:2000`) appears. The host sends `factory-reset.bin` as image name `lk` and executes it with the existing `CMD_SEND_IMAGE` / `CMD_BOOT_IMAGE` path.

## Geometry lock

The payload refuses to write unless GPT reports the following exact values:

```text
frp       start 142400   blocks 2048       (preserved)
md_udc    start 341056   blocks 46288      (wipe first 8)
metadata  start 387344   blocks 65536      (preserved)
cache     start 5849088  blocks 720896     (wipe first 8)
userdata  start 6569984  blocks 8579039    (wipe first 16)
```

These values correspond to the project's CRC-verified KY-42C GPT capture. If a different storage layout is encountered, the payload stops before the first eMMC write.

## Expected next boot

Stock KY-42C fstab marks `/metadata`, `/data`, and `/cache` as `formattable`. The intended next-boot path is therefore that Android recognizes the blank filesystem signatures and recreates the filesystems. This has not yet been hardware-validated specifically for this new payload, so the first test should be treated as an engineering validation rather than an established recovery procedure.
