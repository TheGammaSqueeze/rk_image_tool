# Roadmap

## v0.1 (this release)

- [x] Parse RKFW / RKAF (`info`, `unpack`).
- [x] Parse `parameter.txt` CMDLINE.
- [x] MBR + GPT generation.
- [x] IDBlock builder (RC4 + 5-copy layout).
- [x] Misc `--rk_fwupdate` command writer.
- [x] Minimal FAT32 formatter and single-file writer.
- [x] CLI commands: `info`, `unpack`, `sd-boot`, `upgrade`, `restore`, `list-disks`.
- [x] Linux / macOS / Windows raw-disk backends.
- [x] Image-file (`--image-out`) target for offline verification.
- [x] Self-tests (`ctest`).

## v0.2

- [ ] Native packer (`pack`): build a valid RKAF + RKFW without calling the vendor `afptool`/`img_maker`.
- [ ] Long-file-name (VFAT LFN) support in FAT32 writer so `update.img` appears with its full filename.
- [ ] Full verify pass: read back the SD card, check loader/partition bytes against source.
- [ ] TUI / curses front-end.
- [ ] Progress bar on long writes.

## v0.3+

- [ ] GUI (Qt or Dear ImGui) matching SD_Firmware_Tool's layout.
- [ ] Android "PCBA test" mode wiring.
- [ ] USB loader / maskrom mode flashing (replacement for `upgrade_tool`).
- [ ] Batch mode (multiple cards at once).
