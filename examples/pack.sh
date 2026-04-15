#!/usr/bin/env bash
# Example: pack a firmware directory back to update.img using the legacy
# rk2918_tools until native `pack` lands in rk_image_tool v0.2.
#
#   $1 : path to an Image/ tree previously produced by `rk_image_tool unpack`
#   $2 : desired output update.img
set -euo pipefail

SRC=${1:?usage: pack.sh <unpack-dir> <out.img>}
OUT=${2:?usage: pack.sh <unpack-dir> <out.img>}
AFPTOOL=${AFPTOOL:-afptool}
IMG_MAKER=${IMG_MAKER:-img_maker}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cp "$SRC/Image/MiniLoaderAll.bin" "$TMP/loader.img"
"$AFPTOOL" -pack "$SRC" "$TMP/rkaf.img"
"$IMG_MAKER" "$TMP/loader.img" "$TMP/rkaf.img" "$OUT"
echo "packed -> $OUT"
