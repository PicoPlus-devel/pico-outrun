#!/bin/bash
# Build outrun-data.uf2 from an OutRun revision B ROM set.
#
#   tools/mkoutrundata.sh <romset-dir> [-b build-dir] [-o outrun-data.uf2]
#
# The flash address is NOT hardcoded here. It is read from
# <build-dir>/outrun_flashmap.env, which cmake/OutRunPartition.cmake generates
# from the same variables that produce the OUTRUN_DATA_ADDR compiled into the
# application. That is the whole point: the picotool -o and the address the
# firmware casts can never disagree.
#
# The ROM set is supplied by the user and is copyright SEGA. Neither it nor the
# .uf2 produced here may be redistributed.
set -e
cd "$(dirname "$0")/.."

ROMS=""
BUILD_DIR="build"
OUT="outrun-data.uf2"

while [ $# -gt 0 ]; do
    case "$1" in
    -b) BUILD_DIR="$2"; shift 2 ;;
    -o) OUT="$2"; shift 2 ;;
    -h|--help)
        sed -n '2,15p' "$0" | sed 's/^# \?//'
        exit 0 ;;
    -*) echo "unknown option: $1" >&2; exit 2 ;;
    *)  ROMS="$1"; shift ;;
    esac
done

if [ -z "$ROMS" ]; then
    echo "usage: tools/mkoutrundata.sh <romset-dir> [-b build-dir] [-o out.uf2]" >&2
    exit 2
fi

ENV_FILE="$BUILD_DIR/outrun_flashmap.env"
if [ ! -f "$ENV_FILE" ]; then
    echo "$ENV_FILE not found - configure the firmware first, e.g.:" >&2
    echo "    ./bld.sh -m -c 8 -2" >&2
    exit 1
fi
# shellcheck disable=SC1090
. "$ENV_FILE"

if ! command -v picotool >/dev/null 2>&1; then
    echo "picotool not found in PATH" >&2
    exit 1
fi

if [ ! -x tools/mkoutrundata ]; then
    echo "== building tools/mkoutrundata"
    # port/outrun_pack.c holds the load table and the decoders, shared with the
    # firmware so that this image and the one the board builds from an SD card
    # romset cannot differ.
    gcc -O2 -Wall -Wextra -std=c11 -Iport \
        tools/mkoutrundata.c port/outrun_pack.c -o tools/mkoutrundata
fi

BIN="$(dirname "$OUT")/$(basename "$OUT" .uf2).bin"
./tools/mkoutrundata "$ROMS" -o "$BIN"

SIZE=$(stat -c%s "$BIN")
if [ "$SIZE" -gt "$OUTRUN_DATA_MAX_SIZE" ]; then
    echo "data image is $SIZE bytes but only $OUTRUN_DATA_MAX_SIZE are reserved at $OUTRUN_DATA_ADDR." >&2
    echo "Raise OUTRUN_FLASH_TOTAL or lower OUTRUN_APP_SIZE." >&2
    exit 1
fi

echo "== converting to UF2 at $OUTRUN_DATA_ADDR (RP2350 DATA family)"
picotool uf2 convert "$BIN" -t bin "$OUT" -o "$OUTRUN_DATA_ADDR" --family data

echo
echo "Wrote $OUT  ($SIZE bytes of data, $OUTRUN_DATA_MAX_SIZE reserved)"
echo "Flash it alongside the application; it only needs redoing when it changes."
