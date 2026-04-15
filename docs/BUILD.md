# Building rk_image_tool

## Prerequisites

- C11 compiler (gcc, clang or MSVC 2015+).
- CMake 3.16 or newer.

No external libraries. MD5, RC4, CRC32, MBR/GPT and FAT32 are all self-contained.

## Linux / macOS (native)

```
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/rk_image_tool --help
```

Install to `/usr/local`:

```
cmake --install build
```

## Windows (native, MSVC)

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release -j
```

Binary ends up at `build\Release\rk_image_tool.exe`.

## Windows (cross, MinGW from Linux)

Install MinGW: `apt install mingw-w64`, then use the bundled toolchain file:

```
cmake -S . -B build-mingw64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw64 -j
```

## Running

- On Linux, writing to `/dev/sdX` requires `sudo`.
- On macOS, run `diskutil unmountDisk /dev/diskN` first, then run as root.
- On Windows, run from an Administrator command prompt.

Start with `--image-out <file>` instead of `--device` to verify everything works without touching a physical disk:

```
./build/rk_image_tool upgrade \
  --image path/to/update.img \
  --sdboot path/to/SDBoot.bin \
  --image-out /tmp/test_sd.img --size-gb 16
```

Then check the result with `fdisk -l /tmp/test_sd.img` or mount its partitions via loop device.
