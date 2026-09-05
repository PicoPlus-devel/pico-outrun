# pico-outrun
# OutRun arcade port (Cannonball engine) for RP2350 boards with DVI/HDMI output.

cmake_minimum_required(VERSION 3.13)

set(projectname picoOutRun)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug
        CACHE STRING "Choose the type of build : None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif()
message("* Current build type is : ${CMAKE_BUILD_TYPE}")

# ---------------------------------------------------------------------------
# Hardware / board selection
# ---------------------------------------------------------------------------
if(NOT HW_CONFIG)
    set(HW_CONFIG 8 CACHE STRING "Select the hardware configuration for your board")
endif()

if(NOT USE_HSTX)
    set(USE_HSTX 1 CACHE STRING "Use HSTX for display output when possible")
endif()

include("pico_shared/BoardConfigs.cmake")

if(NOT PICO_BOARD)
    set(PICO_BOARD pico2 CACHE STRING "Board type")
    message("PICO_BOARD not set, using default: ${PICO_BOARD}")
endif()

# RP2040 is not supported: Cannonball's writable state (tile_ram 64 KB, palette,
# engine state) plus a framebuffer does not fit in 264 KB of SRAM. HW_CONFIG 3
# and 4 are RP2040-only parts; every other config must be built with bld.sh -2.
if(PICO_PLATFORM STREQUAL "rp2040")
    message(FATAL_ERROR
        "picoOutRun is RP2350-only - build with './bld.sh -2 -c <HW_CONFIG>'. "
        "HW_CONFIG 3 and 4 (RP2040-only boards) are not supported.")
endif()

add_definitions(-DNDEBUG)

# ---------------------------------------------------------------------------
# Funding the SRAM for the hot render path (port/outrun_hot.hpp, ~10 KB).
#
# The HSTX data-island ring defaults to 256 packets (36,864 bytes of SRAM) with
# a 200-packet high watermark - about 18 ms of HDMI audio buffering. Half that
# is plenty now that audio runs on core1 and the logs show 44,100 samples/sec
# with zero starvation and zero drops; the buffer only has to cover jitter, not
# a shortfall. Halving it returns 18,432 bytes.
#
# Both are #ifndef-guarded in pico_shared, so this needs no change there. They
# go in add_definitions() rather than target_compile_definitions() because the
# ring lives in the pico_shared library target, not ours - and both values must
# agree across the two, since port/audio.cpp reads the watermark too.
#
# If audio ever starts dropping out, put these back before touching anything
# else.
# ---------------------------------------------------------------------------
# RESTORED TO STOCK. Do not shrink these again without re-measuring.
#
# I halved them (128/96) to fund the SRAM render path, and audio has been bad
# ever since - `pushed` sitting around 32,000 against the 44,100 the sink
# consumes, with `dropped` climbing because the ring backed up behind it. 192/144
# did not recover it either. The queue has to absorb a bursty refill: at
# config.fps=30 the engine delivers 1,470 ring frames 30 times a second, and the
# data-island queue is what smooths that into a continuous stream. Undersize it
# and it underruns between bursts, emitting silence packets.
#
# The stock values are 256 packets / 200 watermark, i.e. ~18 ms of buffering.
# Leaving them at the default costs nothing but SRAM, and audio is worth more
# than the ~9 KB.
# ---------------------------------------------------------------------------

include("cmake/OutRunPartition.cmake")

# ---------------------------------------------------------------------------
# TinyUSB family support + Pico SDK init
# Include family_support.cmake BEFORE pico_sdk_init() so that TinyUSB picks up
# the board-specific configuration (PIO USB VBUSEN pin, DP pin, etc.) from the
# BOARD variable set by BoardConfigs.cmake. The Pico SDK's own TinyUSB
# integration forcibly sets BOARD="pico_sdk" which loses these board-specific
# defines; including family_support.cmake first (with its include_guard)
# prevents the SDK from overriding the board configuration.
# ---------------------------------------------------------------------------
set(FAMILY rp2040)
include($ENV{PICO_SDK_PATH}/lib/tinyusb/hw/bsp/family_support.cmake)

# Some boards (e.g. adafruit_metro_rp2350) force CFG_TUH_RPI_PIO_USB=1 in their
# board.cmake, which TinyUSB's rp2040/family.cmake then bakes into
# tinyusb_common_base. When PIO USB is disabled for this build, strip that
# interface define so it doesn't fight our -DCFG_TUH_RPI_PIO_USB=0 and drag
# hcd_pio_usb.c / pio_usb.h into the compile.
if(ENABLE_PIO_USB EQUAL 0 AND TARGET tinyusb_common_base)
    get_target_property(_tu_defs tinyusb_common_base INTERFACE_COMPILE_DEFINITIONS)
    if(_tu_defs)
        list(REMOVE_ITEM _tu_defs "CFG_TUH_RPI_PIO_USB=1")
        set_target_properties(tinyusb_common_base PROPERTIES
            INTERFACE_COMPILE_DEFINITIONS "${_tu_defs}")
    endif()
endif()

project(${projectname} C CXX ASM)

pico_sdk_init()

# check_and_add_pico_pio_usb_support() needs pico_generate_pio_header, which is
# only available after pico_sdk_init().
if(ENABLE_PIO_USB EQUAL 1)
    message("Checking for PIO USB support...")
    check_and_add_pico_pio_usb_support()
endif()
message("PIO USB support: ${ENABLE_PIO_USB}, using PIO: ${PIO_USB_USE_PIO}")

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
# The vendored Cannonball engine. romloader.cpp and the sdl2/, frontend/ and
# directx/ directories are deliberately absent - port/ replaces them, and sits
# ahead of cannonball/src/main on the include path so the engine compiles
# unchanged. See cannonball/README.md.
file(GLOB OUTRUN_ENGINE_SRC
    cannonball/src/main/engine/*.cpp
    cannonball/src/main/engine/audio/*.cpp
    cannonball/src/main/hwvideo/*.cpp
    cannonball/src/main/hwaudio/*.cpp
)
list(APPEND OUTRUN_ENGINE_SRC
    cannonball/src/main/video.cpp
    cannonball/src/main/roms.cpp
    cannonball/src/main/trackloader.cpp
    cannonball/src/main/utils.cpp
)

set(OUTRUN_PORT_SRC
    port/outrun_data.c
    port/config.cpp
    port/romloader.cpp
    port/render.cpp
    port/input.cpp
    port/glue.cpp
    port/alloc.cpp
    port/audio.cpp
)

add_executable(${projectname}
    main.cpp
    splash.cpp
    ${OUTRUN_PORT_SRC}
    ${OUTRUN_ENGINE_SRC}
    DefaultSS444.c
    DefaultSS555.c
)

# OUTRUN_GFX_IN_FLASH: tiles/sprites/road come pre-decoded from the data .uf2.
#
# OUTRUN_YM_LUT_IN_FLASH is deliberately NOT set. Those ~70 KB of tables are
# indexed per operator per sample - about 23,500 lookups a frame into a 26 KB
# table - and in flash the XIP misses stopped core1 making its audio deadline
# (the ring ran dry ~40 times a second). They are worth 70 KB of SRAM, which is
# affordable now that Video::pixels lives in PSRAM. The generator and its
# --check mode stay in hosttest/ so the option can be turned back on.
target_compile_definitions(${projectname} PRIVATE
    OUTRUN_GFX_IN_FLASH=1
    # Put the OUTRUN_HOT-marked functions in SRAM. That is now the AUDIO path -
    # YM2151/SegaPCM stream_update, OSound::tick and the mix/pump - which runs on
    # core1 out of flash and is the remaining sound problem. Paid for by moving
    # hwtiles to PSRAM, NOT out of the HSTX audio queue (see add_definitions
    # above for why that was a mistake).
    OUTRUN_HOT_IN_SRAM=1
)

target_compile_options(${projectname} PRIVATE
    # The vendored engine is 2010-era C++ and warns freely; keep it quiet so
    # warnings from port/ and main.cpp stay visible.
    -Wno-unused-but-set-variable
    -Wno-sign-compare
)

pico_set_program_name(${projectname} "${projectname}")
pico_set_program_version(${projectname} "0.1")

outrun_apply_flash_map(${projectname})

# ---------------------------------------------------------------------------
# stdio routing
# ---------------------------------------------------------------------------
if(UART_ENABLED EQUAL 0)
    message("Disabling UART output for this board.")
    pico_enable_stdio_uart(${projectname} 0)
else()
    pico_enable_stdio_uart(${projectname} 1)
endif()
pico_enable_stdio_usb(${projectname} 0)

# ---------------------------------------------------------------------------
# What family_configure_target() used to do for us, inlined.
# ---------------------------------------------------------------------------
target_compile_definitions(${projectname} PRIVATE
    PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
)

target_compile_options(${projectname} PRIVATE
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-cast-function-type
    -Wno-maybe-uninitialized
)

# ---------------------------------------------------------------------------
# Board pins and features, from BoardConfigs.cmake
# ---------------------------------------------------------------------------
target_compile_definitions(${projectname} PUBLIC
    DVICONFIG=${DVICONFIG}
    SDCARD_PIN_CS=${SD_CS}
    SDCARD_PIN_SCK=${SD_SCK}
    SDCARD_PIN_MOSI=${SD_MOSI}
    SDCARD_PIN_MISO=${SD_MISO}
    SDCARD_PIO=pio1
    SDCARD_SPI=${SD_SPI}
    NES_PIN_CLK=${NES_CLK}
    NES_PIN_DATA=${NES_DATA}
    NES_PIN_LAT=${NES_LAT}
    NES_PIO=${NES_PIO}
    NES_PIN_CLK_1=${NES_CLK_1}
    NES_PIN_DATA_1=${NES_DATA_1}
    NES_PIN_LAT_1=${NES_LAT_1}
    NES_PIO_1=${NES_PIO_1}
    WII_PIN_SDA=${WII_SDA}
    WII_PIN_SCL=${WII_SCL}
    LED_GPIO_PIN=${LED_GPIO_PIN}
    HW_CONFIG=${HW_CONFIG}
    WIIPAD_I2C=${WIIPAD_I2C}
    USE_I2S_AUDIO=${USE_I2S_AUDIO}
    PICO_AUDIO_I2S_DATA_PIN=${PICO_AUDIO_I2S_DATA_PIN}
    PICO_AUDIO_I2S_CLOCK_PIN_BASE=${PICO_AUDIO_I2S_CLOCK_PIN_BASE}
    PICO_AUDIO_I2S_PIO=${PICO_AUDIO_I2S_PIO}
    PICO_AUDIO_I2S_DMA_IRQ=1
    PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED=${PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED}
    PICO_AUDIO_I2S_RESET_PIN=${PICO_AUDIO_I2S_RESET_PIN}
    PICO_AUDIO_I2S_INTERRUPT_PIN=${PICO_AUDIO_I2S_INTERRUPT_PIN}
    PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON=${PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON}
    CFG_TUH_RPI_PIO_USB=${ENABLE_PIO_USB}
    PIO_USB_USE_PIO=${PIO_USB_USE_PIO}
    PSRAM_CS_PIN=${PSRAM_CS_PIN}
    GPIOHSTXD0=${GPIOHSTXD0}
    GPIOHSTXD1=${GPIOHSTXD1}
    GPIOHSTXD2=${GPIOHSTXD2}
    GPIOHSTXCK=${GPIOHSTXCK}
    GPIOHSTXINVERTED=${GPIOHSTXINVERTED}
    ENABLE_VU_METER=${ENABLE_VU_METER}
    WIIPAD_DELAYED_START=${WIIPAD_DELAYED_START}
    ENABLEDVI=1
)

# NOTE: HSTX_CORE1_STACK_IN_SCRATCH_X is deliberately NOT set here, though the
# sibling emulators do set it.
#
# It puts core1's stack in SCRATCH_X (0x20080000-0x20081000), which is directly
# below core0's stack in SCRATCH_Y - and the two banks are contiguous. With
# PICO_STACK_SIZE=3072 core0 had 3 KB and a 1 KB gap beneath it, so any overflow
# of core0's stack landed straight in core1's, taking down the HSTX driver's DMA
# IRQ with a garbage return address and corrupting the heap on the way. That is
# what the two hard faults on 2026-09-04 looked like.
#
# Leaving it unset costs 4 KB of .bss (the driver's private core1 stack) and
# puts distance between the two stacks. Core0 gets the whole SCRATCH_Y bank.
target_compile_definitions(${projectname} PUBLIC
    PICO_CORE1_STACK_SIZE=4096
)

target_compile_definitions(${projectname} PRIVATE
    PICO_PIO_USE_GPIO_BASE=1
    # 4096, not the siblings' 3072: that leaves core0 the full SCRATCH_Y bank.
    # The engine's call chains are deeper than an emulator's, and an overflow
    # here lands in the next bank down rather than anywhere harmless.
    PICO_STACK_SIZE=4096
    # Diagnostic: MPU guard at the bottom of each stack, so an overflow traps
    # immediately and attributably instead of silently corrupting the neighbour.
    # Costs a little startup time; keep it until the port is stable.
    PICO_USE_STACK_GUARDS=1
)

if(DEFINED PICO_DEFAULT_UART_TX_PIN)
    message(STATUS "Using PICO_DEFAULT_UART_TX_PIN: ${PICO_DEFAULT_UART_TX_PIN}")
    target_compile_definitions(${projectname} PRIVATE
        PICO_DEFAULT_UART_TX_PIN=${PICO_DEFAULT_UART_TX_PIN}
        PICO_DEFAULT_UART=0
    )
endif()
if(DEFINED PICO_DEFAULT_UART_RX_PIN)
    message(STATUS "Using PICO_DEFAULT_UART_RX_PIN: ${PICO_DEFAULT_UART_RX_PIN}")
    target_compile_definitions(${projectname} PRIVATE
        PICO_DEFAULT_UART_RX_PIN=${PICO_DEFAULT_UART_RX_PIN}
    )
endif()
if(DEFINED PICO_DEFAULT_UART)
    message(STATUS "Using PICO_DEFAULT_UART: ${PICO_DEFAULT_UART}")
    target_compile_definitions(${projectname} PRIVATE
        PICO_DEFAULT_UART=${PICO_DEFAULT_UART}
    )
endif()

# ---------------------------------------------------------------------------
# PIO USB (optional)
# ---------------------------------------------------------------------------
if(ENABLE_PIO_USB EQUAL 1)
    if(NOT PIO_DP_PLUS_PIN EQUAL -1)
        message(STATUS "Using PIO USB DP pin: ${PIO_DP_PLUS_PIN}")
        target_compile_definitions(${projectname} PRIVATE
            PICO_DEFAULT_PIO_USB_DP_PIN=${PIO_DP_PLUS_PIN}
        )
    endif()
    message("Adding target link libraries for PIO USB support.")
    target_link_libraries(${projectname} PRIVATE tinyusb_pico_pio_usb)
endif()

# ---------------------------------------------------------------------------
# Link
# ---------------------------------------------------------------------------
target_link_libraries(${projectname} PRIVATE
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_pio
    hardware_i2c
    hardware_interp
    hardware_timer
    hardware_clocks
    hardware_pwm
    hardware_flash
    dvi
    util
    tinyusb_host
    tinyusb_board
    pico_shared
    xinput_host
)

if(PICO_BOARD STREQUAL "pico_w" OR PICO_BOARD STREQUAL "pico2_w")
    target_link_libraries(${projectname} PRIVATE pico_cyw43_arch_none)
endif()

target_include_directories(${projectname} PRIVATE
    pico_lib
    pico_shared
    tusb_xinput
    port                    # MUST precede cannonball/src/main: port/ shadows
    cannonball/src/main     # frontend/, sdl2/ and directx/ headers
    .
)

add_subdirectory(pico_lib/dvi)
add_subdirectory(pico_lib/util)
add_subdirectory(pico_shared)
add_subdirectory(tusb_xinput xinput_host)

target_link_options(${projectname} PRIVATE -Xlinker --print-memory-usage --data-sections)

# Optional: relink to run from the pico-bootLoader application partition.
# Phase 2 - present for skeleton parity with the sibling repos, not yet verified.
if(BUILD_FOR_BOOTLOADER)
    include("pico_shared/BootPartition.cmake")
    frens_offset_for_bootloader(${projectname})
endif()

pico_add_extra_outputs(${projectname})
