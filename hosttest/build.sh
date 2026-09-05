#!/bin/bash
# Build the host-side tools and tests.
#
#   ./hosttest/build.sh              build everything
#   ./hosttest/build.sh verify       build only verify_decode
#   ./hosttest/build.sh lutgen       regenerate port/ym2151_luts.h
#   ./hosttest/build.sh check        build, then run the full check against
#                                    $OUTRUN_ROMS (default ~/roms/outrun)
#
# verify_decode links Cannonball's REAL hwtiles/hwsprites/hwroad and compares
# their output against what tools/mkoutrundata bakes into flash. It builds the
# firmware's own port/frontend/config.hpp - not a test copy - so the two cannot
# disagree about any setting that reaches a decoder. Only video.hpp is stubbed,
# in hosttest/stubs; both dirs go on the include path AHEAD of the vendored
# engine.
set -e
cd "$(dirname "$0")/.."

ROMS="${OUTRUN_ROMS:-$HOME/roms/outrun}"
OUT=hosttest/out
mkdir -p "$OUT"

build_packer() {
    echo "== building tools/mkoutrundata"
    # port/outrun_pack.c is the shared load table and decoders - the firmware
    # compiles the very same file, so verify_decode below certifies both.
    gcc -O2 -g -Wall -Wextra -std=c11 -Iport \
        tools/mkoutrundata.c port/outrun_pack.c -o tools/mkoutrundata
}

build_lutgen() {
    echo "== building hosttest/lutgen_ym2151"
    g++ -O2 -g -std=c++11 -DOUTRUN_LUTGEN \
        -Iport -Icannonball/src/main \
        hosttest/lutgen_ym2151.cpp port/config.cpp \
        cannonball/src/main/hwaudio/ym2151.cpp cannonball/src/main/hwaudio/soundchip.cpp \
        -o "$OUT/lutgen_ym2151" -lm
}

build_verify() {
    echo "== building hosttest/verify_decode"
    g++ -O1 -g -std=c++11 -fsanitize=address -fno-omit-frame-pointer \
        -Wall -Wno-unused-parameter \
        -Ihosttest/stubs -Iport -Icannonball/src/main \
        hosttest/verify_decode.cpp \
        port/config.cpp \
        cannonball/src/main/hwvideo/hwtiles.cpp \
        cannonball/src/main/hwvideo/hwsprites.cpp \
        cannonball/src/main/hwvideo/hwroad.cpp \
        -o "$OUT/verify_decode"
}

case "${1:-all}" in
packer)
    build_packer
    ;;
verify)
    build_verify
    ;;
lutgen)
    build_lutgen
    "$OUT/lutgen_ym2151" port/ym2151_luts.h
    ;;
check)
    build_packer
    build_verify
    build_lutgen
    if [ ! -d "$ROMS" ]; then
        echo "romset not found: $ROMS (set OUTRUN_ROMS)" >&2
        exit 1
    fi
    echo "== packing $ROMS"
    ./tools/mkoutrundata "$ROMS" -o "$OUT/outrun-data.bin"
    echo "== verifying against Cannonball's own decoders"
    "$OUT/verify_decode" "$ROMS" "$OUT/outrun-data.bin"
    echo "== verifying port/ym2151_luts.h against the vendored ym2151.cpp"
    "$OUT/lutgen_ym2151" port/ym2151_luts.h --check
    ;;
all)
    build_packer
    build_verify
    build_lutgen
    echo "ok: tools/mkoutrundata, $OUT/verify_decode, $OUT/lutgen_ym2151"
    ;;
*)
    echo "usage: $0 [all|packer|verify|lutgen|check]" >&2
    exit 2
    ;;
esac
