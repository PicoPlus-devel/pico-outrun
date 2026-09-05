# ---------------------------------------------------------------------------
# OutRunPartition.cmake — the flash map for picoOutRun.
#
# The game data (decoded tiles/sprites/roads + the 68k, Z80 and PCM regions,
# ~2.25 MB) does not live in the application image. It is packed by
# tools/mkoutrundata into a separate RP2350 DATA-family .uf2 and flashed to a
# fixed address, the way pico-doom ships its WAD.
#
# THREE CONSUMERS MUST AGREE on that address:
#   1. OUTRUN_DATA_ADDR as a compile definition   (port/outrun_data.c casts it)
#   2. the -o <addr> on the picotool line          (tools/mkoutrundata.sh)
#   3. the application size cap                    (OUTRUN_APP_SIZE, below)
# All three are derived from the variables here so they cannot drift apart.
# tools/mkoutrundata.sh reads the value back out of the build directory rather
# than hardcoding it.
#
# Board overrides go on the cmake line, e.g. a 16 MB Fruit Jam:
#   EXTRA_CMAKE_ARGS=-DOUTRUN_FLASH_TOTAL=0x1000000 ./bld.sh -c 8 -2
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

set(OUTRUN_XIP_BASE    "0x10000000" CACHE STRING "RP2350 XIP flash base")
set(OUTRUN_APP_SIZE    "0x100000"   CACHE STRING "Bytes reserved for the picoOutRun application (1 MB)")
set(OUTRUN_FLASH_TOTAL "0x400000"   CACHE STRING "Total external flash on the board (default 4 MB = plain Pico 2)")

# Phase 2 (pico-bootLoader) will build with BUILD_FOR_BOOTLOADER, which puts the
# application behind the loader's 512 KB reservation and slides the data blob up
# with it. Kept here so the address stays a single derived value; NOT exercised
# or verified in Phase 1.
if(BUILD_FOR_BOOTLOADER)
    # Mirrors FRENS_APP_BASE in pico_shared/BootPartition.cmake.
    math(EXPR _outrun_app_base "${OUTRUN_XIP_BASE} + 0x80000" OUTPUT_FORMAT HEXADECIMAL)
else()
    set(_outrun_app_base "${OUTRUN_XIP_BASE}")
endif()

math(EXPR OUTRUN_DATA_ADDR "${_outrun_app_base} + ${OUTRUN_APP_SIZE}" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR _outrun_flash_end "${OUTRUN_XIP_BASE} + ${OUTRUN_FLASH_TOTAL}" OUTPUT_FORMAT HEXADECIMAL)

if(_outrun_app_base GREATER_EQUAL _outrun_flash_end OR OUTRUN_DATA_ADDR GREATER_EQUAL _outrun_flash_end)
    message(FATAL_ERROR
        "OutRun flash map does not fit: app base ${_outrun_app_base}, data ${OUTRUN_DATA_ADDR}, "
        "flash ends ${_outrun_flash_end}. Lower OUTRUN_APP_SIZE or raise OUTRUN_FLASH_TOTAL.")
endif()

math(EXPR _outrun_data_room "${_outrun_flash_end} - ${OUTRUN_DATA_ADDR}")
message(STATUS "OutRun flash map: app ${_outrun_app_base} (+${OUTRUN_APP_SIZE}), "
               "data ${OUTRUN_DATA_ADDR}, room for data = ${_outrun_data_room} bytes")

# Applied to the target so port/ can cast it, and written to a file that
# tools/mkoutrundata.sh reads so the picotool -o can never disagree.
function(outrun_apply_flash_map TARGET)
    target_compile_definitions(${TARGET} PRIVATE
        OUTRUN_DATA_ADDR=${OUTRUN_DATA_ADDR}
        OUTRUN_DATA_MAX_SIZE=${_outrun_data_room}
    )
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/outrun_flashmap.env"
         CONTENT "OUTRUN_DATA_ADDR=${OUTRUN_DATA_ADDR}\nOUTRUN_DATA_MAX_SIZE=${_outrun_data_room}\n")
endfunction()
