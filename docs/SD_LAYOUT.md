# SD card layout

This document describes the exact byte layout that `rk_image_tool` writes to an SD card, matching what Rockchip's SD_Firmware_Tool v1.69 produces. Offsets are 512-byte sectors ("LBA") unless noted.

## Big picture

```
LBA 0           Protective MBR (type 0xEE)
LBA 1           GPT primary header
LBA 2..33       GPT primary entries (128 x 128 B = 16 KiB)
LBA 64..        IDBlock (RC4-encoded loader) - single copy
LBA <per parameter.txt> firmware partitions (parameter, uboot, misc,
                  dtbo, vbmeta, boot, recovery, backup)
LBA <userdata>  FAT32 volume containing sdupdate.img
LBA total-33..total-1  GPT backup entries, then backup header at total-1
```

## 1. Clear

The tool first zeros `sector 0..1` (0x400 bytes) to kill the old MBR, then zeros `sector 34..63` to wipe the old GPT-entry region ahead of the loader, and the last 33 sectors for the old backup GPT.

## 2. Protective MBR (LBA 0)

```
0x000..0x1BD  zeros
0x1BE         0x00           boot indicator
0x1BF..0x1C1  0x00 0x02 0x00 CHS start (0,0,2)
0x1C2         0xEE           GPT protective
0x1C3..0x1C5  0xFE 0xFF 0xFF CHS end (max)
0x1C6..0x1C9  0x00000001     LBA start
0x1CA..0x1CD  min(total-1, 0xFFFFFFFF)
0x1CE..0x1FD  zeros
0x1FE..0x1FF  0x55 0xAA
```

## 3. GPT (LBA 1..33 primary, mirrored at tail)

Standard EFI GPT with:

- 128 partition entries of 128 B each (= 16 KiB = 32 sectors).
- Disk GUID randomly generated each run.
- FirstUsableLBA = 34.
- LastUsableLBA = `(total_sectors - 34) & ~63` (64-sector aligned, per v1.65 revision note).

Each partition entry uses the Microsoft Basic Data type GUID (`EBD0A0A2-B9E5-4433-87C0-68B6B72699C7`) and is populated from the `mtdparts=rk29xxnand:` CMDLINE in `parameter.txt`.

## 4. IDBlock (LBA 64, single copy)

The Rockchip BootROM reads sector 64 looking for an IDBlock. `rk_image_tool` writes the input loader (`SDBoot.bin` for pure SD-boot, or `MiniLoaderAll.bin` from `update.img` if `--use-fw-loader` is given) as a single sector-encoded IDB:

- Loader is padded up to the next multiple of 512 bytes.
- Each 512-byte sector is independently RC4-encrypted with the fixed key:
  ```
  7C 4E 03 04 55 05 09 07 2D 2C 7B 38 17 0D 17 11
  ```
  (KSA + PRGA reinitialised per sector - *not* a streaming cipher.)
- The result is written in one contiguous block at byte offset `64 * 512 = 0x8000`.

The BootROM decrypts the sector at 0x8000 with the same key, parses the embedded `FlashHead`/`FlashData`/`FlashBoot` headers and runs the DDR-init + SPL chain.

> Note: earlier Rockchip tooling writes 5 identical IDBlock copies at 0x8000, 0x88000, 0x108000... SD_Firmware_Tool v1.69 writes only one, because the card-side SDBoot reads from sector 64 only.

## 5. Firmware partitions (upgrade mode)

For every name present in both `parameter.txt` *and* the update image, the tool writes the raw partition payload at `offset_lba * 512`:

```
parameter, uboot, misc, dtbo, vbmeta, boot, recovery, backup
```

## 6. Misc command block (upgrade mode only)

After writing the original `misc.img`, the tool overwrites the first 8 KiB (`0x2000` bytes) of the misc partition with an Android bootloader message laid out exactly as the vendor tool does:

```
offset 0x0000..0x07FF  zero
offset 0x0800          "boot-recovery" (+NULs up to 32 B)
offset 0x0840          "recovery\n--rk_fwupdate\n"
offset 0x0B40..0x1FFF  zero
```

Rockchip's on-device recovery reads this and, seeing the `rk_fwupdate` flag, mounts the FAT32 user volume and re-flashes from `/sdupdate.img`.

## 7. FAT32 user volume (upgrade mode only)

The `userdata:grow` partition is formatted as FAT32 (32 KiB clusters for >32 GB volumes, 4-16 KiB otherwise), with volume label `RK_UPDATE` by default. Then:

- `sdupdate.img` is placed in the root with the full contents of `update.img`.
  **The filename matters** - the on-device SDBoot expects exactly `sdupdate.img`.
- Optional: `sd_boot_config.config` template (feed flags to the SDBoot run-mode).
- Optional: a demo/asset file.

## 8. Restore

`rk_image_tool restore` reverts the card by:

1. Zeroing LBA 0..33 (old MBR + primary GPT header/entries).
2. Zeroing LBA 64..(64+0x400) (old IDBlock).
3. Zeroing last 33 sectors (backup GPT).
4. Writing a plain DOS MBR with one FAT32 partition at LBA 2048 covering the rest of the card.
5. Formatting that partition as FAT32 with label `RK_RESTORE`.

The card then appears as a regular removable disk again.

## Cross-reference: SD_Firmware_Tool.exe v1.69 binary

| Stage          | Function (VA)      | Notes |
|----------------|--------------------|-------|
| Overall orchestration | `CreateSDCard @ 0x408810` | |
| Clear MBR      | `ClearMbr @ 0x409A90`     | `WriteDisk(0, 0x400, zeros)` |
| Build protective MBR + primary GPT | `SetGpt @ 0x409FF0` -> `build_gpt @ 0x410EF0` | Writes 0x4200 bytes at sector 0 |
| Build backup GPT | inside `SetGpt`         | 0x4200 bytes at `(total-0x21)*0x200` |
| Write IDBlock  | `WriteLoader @ 0x40A7B0`  | Single copy at `IDBLOCK_POS * 0x200` (default IDBLOCK_POS=64) |
| Per-partition  | `WriteImageItem @ 0x40B540` | Reads RKAF, writes at `parameter.txt` offsets |
| Misc command   | `WriteMiscItem @ 0x40C400`| 0x2000-byte buffer as above |
| FAT32 format   | `FormatDrive` -> fmifs `FormatEx` | Quick format |
| Copy files     | `CopyFirmwareAndDemo @ 0x40CFC0` | CopyFileW for sdupdate.img, Demo\, boot config |
| Build boot config | `CreateBootConfigFile @ 0x40E1B0` | Token replacement in template |
| RC4 per sector | `@ 0x401000`              | KSA re-init per 512-byte block |
| Parameter parse | `parse_parameter @ 0x4101E0` | |
| Raw disk I/O   | `WriteDisk @ 0x45E810`, `ReadDisk @ 0x45E770` | |
