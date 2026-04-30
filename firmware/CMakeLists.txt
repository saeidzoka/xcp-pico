# ============================================================================
# xcp-pico firmware build configuration
# ----------------------------------------------------------------------------
# Target:   Raspberry Pi Pico 2 (RP2350, dual Cortex-M33)
# Toolchain: arm-none-eabi-gcc (provided via PICO_SDK_PATH)
# ============================================================================

cmake_minimum_required(VERSION 3.13)

# Pull in the Pico SDK (must come before project() call).
# The SDK is located via the PICO_SDK_PATH environment variable.
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(xcp_pico
    VERSION 0.1.0
    DESCRIPTION "Open-source XCP slave for Raspberry Pi Pico 2"
    LANGUAGES C CXX ASM
)

# C standard: C11 is widely supported by arm-none-eabi-gcc and gives us
# _Static_assert, anonymous structs, and other useful features.
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK. This sets up the toolchain, board configuration,
# and core SDK targets. Must be called after project().
pico_sdk_init()

# ----------------------------------------------------------------------------
# Main firmware executable
# ----------------------------------------------------------------------------
add_executable(xcp_pico
    src/main.c
    src/xcp_transport_usb.c
)

# Public headers shared across modules live in include/.
# Module-private headers stay next to their .c files in src/.
target_include_directories(xcp_pico PRIVATE
    include
    src
)

# Link against the Pico SDK runtime. pico_stdlib pulls in the basics:
# clock setup, stdio, GPIO, and so on.
target_link_libraries(xcp_pico PRIVATE
    pico_stdlib
)

# ----------------------------------------------------------------------------
# stdio routing
# ----------------------------------------------------------------------------
# Route printf/getchar over USB CDC (so we can talk to the host without UART).
# UART stdio is disabled to keep the USB CDC endpoint as the single channel
# for both diagnostic logging and (eventually) XCP transport.
#
# NOTE: In later milestones, XCP traffic and stdio will share the CDC interface
# during development, then move to a dedicated CDC interface for XCP only.
pico_enable_stdio_usb(xcp_pico  1)
pico_enable_stdio_uart(xcp_pico 0)

# ----------------------------------------------------------------------------
# Build outputs
# ----------------------------------------------------------------------------
# Generate .uf2, .hex, .bin, .map, .dis in addition to the .elf.
# .uf2 is what gets flashed via BOOTSEL drag-and-drop.
pico_add_extra_outputs(xcp_pico)