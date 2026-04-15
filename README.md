# rk_image_tool

A cross-platform open-source reimplementation of Rockchip's `SD_Firmware_Tool.exe` and the related RK image-packing utilities.

It lets you work with Rockchip firmware images (`update.img`) and create bootable / upgrade SD cards on Windows, macOS and Linux without running the vendor GUI.

> **Status**: v0.1 - covers unpacking, parsing, SD boot card creation, SD upgrade card creation and restore. Packing (`update.img` creation) is still delegated to the existing `afptool`/`img_maker` pair. See [ROADMAP](docs/ROADMAP.md).

---

## Features

- Parse RKFW (`update.img`) and RKAF archives.
- Extract every partition (`boot`, `recovery`, `dtbo`, `super`, ...) into a directory.
- Create a Rockchip SD boot card (the "SD Boot" mode of SD_Firmware_Tool).
- Create an SD upgrade / flash card that auto-reflashes the device (the "Upgrade Firmware" mode).
- Restore a Rockchip SD card back to a normal FAT32 removable disk.
- Optionally write to a disk image file (`--image-out`) instead of a physical SD card, for offline verification or CI.
- Single static binary, no runtime dependencies. Builds with any C11 compiler.

## Supported platforms

| OS      | Notes                                                    |
|---------|----------------------------------------------------------|
| Linux   | Uses `BLKGETSIZE64` / `BLKRRPART`. Needs root to write.  |
| macOS   | Writes through `/dev/rdiskN`. User must `diskutil unmountDisk` first. |
| Windows | Uses `\\.\PhysicalDriveN` + volume lock/dismount. Run as Administrator. |

## Supported SoCs

Any Rockchip device whose stock upgrade tool is SD_Firmware_Tool v1.69 or earlier: RK3288, RK3328, RK3399, RK3566, RK3568, RK3576, RK356x, RK3399pro and similar. It parses `parameter.txt` from the update image so new partition layouts are handled automatically.

---

## Building

```
git clone git@github.com:TheGammaSqueeze/rk_image_tool.git
cd rk_image_tool
cmake -S . -B build
cmake --build build -j
```

Cross-compile to Windows with MinGW:

```
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw.cmake
cmake --build build-win -j
```

Run the test suite:

```
ctest --test-dir build --output-on-failure
```

---

## CLI reference

```
rk_image_tool <command> [options]

  info        print header info from an update.img
  unpack      extract partitions from an update.img
  pack        pack an Image/ directory back into update.img     (not yet)
  sd-boot     create a plain SD-boot card
  upgrade     create an SD upgrade / flash card
  restore     restore an SD card back to a plain FAT32 disk
  list-disks  list removable disks
```

### `info`

```
rk_image_tool info /path/to/update.img
```

Prints RKFW and RKAF header fields and the embedded partition table. Verifies the image MD5.

### `unpack`

```
rk_image_tool unpack /path/to/update.img /path/to/out
```

Extracts every partition into `out/` using the names from `package-file`. Generates a `package-file` so the tree is round-trippable with the reference `afptool`.

### `list-disks`

```
rk_image_tool list-disks           # only removable disks
rk_image_tool list-disks --all     # also show fixed disks
```

### `sd-boot`

Writes a Rockchip SD boot card. This makes the device boot from the SD card using the supplied `SDBoot.bin` (or `MiniLoader` from the update image on RK3288).

```
rk_image_tool sd-boot \
  --image   update.img \
  --sdboot  SDBoot.bin \
  --device  /dev/sdX
```

### `upgrade`

Writes an SD upgrade card. Plug the card into the target device and power on; it will self-flash then boot.

```
rk_image_tool upgrade \
  --image   update.img \
  --sdboot  SDBoot.bin \
  --device  /dev/sdX
```

Useful extra flags:

| Flag              | Effect                                                     |
|-------------------|------------------------------------------------------------|
| `--use-fw-loader` | Use the MiniLoader inside `update.img` instead of `SDBoot.bin` (RK3288). |
| `--no-format`     | Skip the FAT32 format of the userdata partition.           |
| `--demo <file>`   | Copy a demo/asset file alongside `update.img` on the user disk. |
| `--label <name>`  | FAT32 volume label (default `RK_UPDATE`).                  |
| `--image-out <f>` | Write to a disk image file instead of a device.            |
| `--size-gb <n>`   | Size for `--image-out` (default 16 GiB).                   |
| `--dry-run`       | Compute the layout and print what *would* be written.      |

### `restore`

Clears the Rockchip IDBlock and GPT area and reformats the whole card as FAT32. Reclaims the capacity lost to the hidden loader/firmware partitions.

```
rk_image_tool restore --device /dev/sdX
```

---

## Safety

`sd-boot`, `upgrade` and `restore` **will destroy all data** on the target disk. The tool refuses to run without `--device` or `--image-out`. Always double-check with `list-disks` or `lsblk` / `diskutil list` / Disk Management before flashing.

Image-file mode (`--image-out`) is completely non-destructive and is the recommended way to develop / experiment.

## License

Apache 2.0 - see [LICENSE](LICENSE). Portions incorporate code originally under 2-clause BSD (Rockchip CRC table by FUKAUMI Naoki). Those files retain their original notices.

## See also

- [`docs/SD_LAYOUT.md`](docs/SD_LAYOUT.md) - exact byte layout of an SD card produced by this tool.
- [`docs/FORMAT.md`](docs/FORMAT.md) - binary format of `update.img` (RKFW and RKAF).
- [`docs/BUILD.md`](docs/BUILD.md) - detailed build instructions.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) - what's left to do.
