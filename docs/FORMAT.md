# `update.img` format

A Rockchip `update.img` is usually an **RKFW** container holding an **RKAF** archive plus a bootloader blob and a trailing MD5.

```
+-----------------+  offset 0
| RKFW header     |  0x66 bytes
+-----------------+  loader_offset (0x66)
| MiniLoader      |  loader_length bytes
+-----------------+  image_offset
| RKAF archive    |  image_length bytes
+-----------------+
| ASCII MD5 (32)  |  optional tail (present in vendor builds)
+-----------------+
```

## RKFW header (`struct rkfw_header`, 0x66 bytes, little-endian)

| Offset | Size | Field            | Description |
|-------:|-----:|------------------|-------------|
| 0x00   | 4    | `head_code`      | `RKFW` |
| 0x04   | 2    | `head_len`       | Usually `0x66`. |
| 0x06   | 4    | `version`        | `(major<<24) | (minor<<16) | patch` |
| 0x0A   | 4    | `code`           | Internal code (often `0x01030000`). |
| 0x0E   | 7    | build time       | year(u16), month, day, hour, min, sec |
| 0x15   | 4    | `chip`           | Chip ID (`0x50` for RK29xx, larger values for newer SoCs). |
| 0x19   | 4    | `loader_offset`  | Usually `0x66`. |
| 0x1D   | 4    | `loader_length`  | Size of the embedded MiniLoader. |
| 0x21   | 4    | `image_offset`   | Start of the RKAF archive. |
| 0x25   | 4    | `image_length`   | Size of the RKAF archive. |
| 0x29   | 12   | flags            | `unknown1`, `unknown2`, `system_fstype`. |
| 0x35   | 4    | `backup_endpos`  | Sector-address end of the `backup` partition. |
| 0x39   | 0x2D | reserved         | Zero padding to 0x66. |

## RKAF archive

The RKAF archive is the actual "update" payload. Header layout:

| Offset | Size  | Field         |
|-------:|------:|---------------|
| 0x00   | 4     | `RKAF` magic  |
| 0x04   | 4     | `length`      |
| 0x08   | 0x22  | `model`       |
| 0x2A   | 0x1E  | `id`          |
| 0x48   | 0x38  | `manufacturer`|
| 0x80   | 4     | unknown       |
| 0x84   | 4     | `version`     |
| 0x88   | 4     | `num_parts`   |
| 0x8C   | 16*N  | partition table (see below) |

Each partition entry (`struct rkaf_part`, 136 bytes) has:

| Offset | Size | Field        |
|-------:|-----:|--------------|
| 0x00   | 32   | `name`       |
| 0x20   | 60   | `filename`   |
| 0x5C   | 4    | `nand_size`  |
| 0x60   | 4    | `pos`        |
| 0x64   | 4    | `nand_addr`  |
| 0x68   | 4    | `padded_size`|
| 0x6C   | 4    | `size`       |

The partition data is stored at `rkaf_offset + pos`. The special entry with filename `RESERVED` represents the `backup` partition; its contents are the whole `update.img` itself (populated by the device at flash time).

## parameter partition

`parameter.img` is wrapped as:

```
"PARM" <le32 length> <raw parameter.txt bytes> <le32 RKCRC>
```

The raw text uses the Android/Linux kernel `mtdparts=rk29xxnand:` style in the `CMDLINE` line. Example:

```
CMDLINE:mtdparts=rk29xxnand:
  0x00002000@0x00002000(uboot),
  0x00000800@0x0000c000(vbmeta),
  0x00020000@0x0000c800(boot),
  ...
  -@0x00e25400(userdata:grow)
```

All offsets and sizes are in **512-byte sectors**, hex. The size `-` means "grow to the end of the disk". The optional `:grow` modifier has the same effect.

## MD5 tail

Vendor tools append a 32-byte ASCII MD5 of everything before the MD5 itself. `rk_image_tool info` will check either `file_size - 32` (most builds) or `image_offset + image_length` (some older builds).
