#!/bin/bash
# Build the host-side tools and tests.
#
#   ./hosttest/build.sh              build everything
#   ./hosttest/build.sh verify       build only verify_decode
#   ./hosttest/build.sh lutgen       regenerate port/ym2151_luts.h
#   ./hosttest/build.sh check        build, then run the full check against
#                                    $OUTRUN_ROMS (default ~/roms/outrun)
#   ./hosttest/build.sh host         build only outrun_host, which runs the
#                                    engine and dumps frames (see its header)
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

build_host() {
    echo "== building hosttest/outrun_host"
    # The whole engine and video.cpp, as the firmware builds them. Only
    # port/glue.cpp, render.cpp and alloc.cpp are replaced, by outrun_host.cpp.
    # outrun_data.c needs a flash address to compile; the harness never probes
    # it, and adopts the image through outrun_data_adopt_psram() instead.
    gcc -O1 -g -std=c11 -fsanitize=address -fno-omit-frame-pointer \
        -DOUTRUN_DATA_ADDR=0 -DOUTRUN_DATA_MAX_SIZE=0 -Iport \
        -c port/outrun_data.c -o "$OUT/outrun_data.o"
    g++ -O1 -g -std=c++17 -fsanitize=address -fno-omit-frame-pointer \
        -Wno-unused-but-set-variable -Wno-sign-compare \
        -DOUTRUN_GFX_IN_FLASH=1 \
        -Iport -Icannonball/src/main \
        hosttest/outrun_host.cpp \
        port/config.cpp port/romloader.cpp port/input.cpp \
        cannonball/src/main/engine/*.cpp \
        cannonball/src/main/engine/audio/*.cpp \
        cannonball/src/main/hwvideo/*.cpp \
        cannonball/src/main/hwaudio/*.cpp \
        cannonball/src/main/video.cpp \
        cannonball/src/main/roms.cpp \
        cannonball/src/main/trackloader.cpp \
        cannonball/src/main/utils.cpp \
        "$OUT/outrun_data.o" \
        -o "$OUT/outrun_host" -lm
}

case "${1:-all}" in
host)
    build_host
    ;;
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
    echo "usage: $0 [all|packer|verify|lutgen|check|host]" >&2
    exit 2
    ;;
esac
