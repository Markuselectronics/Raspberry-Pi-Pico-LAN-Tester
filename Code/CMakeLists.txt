# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# Generated Cmake Pico project file
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ==========================================================

set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK
include(pico_sdk_import.cmake)

project(LANTester C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(LANTester LANTester.c)

target_compile_definitions(LANTester PRIVATE
    _WIZCHIP_=5500
)

# -------------------------------------------------------------------------
# 1. ADD EXTERNAL LIBRARIES (Manual Source Inclusion)
# -------------------------------------------------------------------------

# Define where the header files (.h) are located
target_include_directories(LANTester PRIVATE 
    lib/pico-ssd1306               # <-- Here is the ssd1306.h
    lib/ioLibrary_Driver/Ethernet
    lib/ioLibrary_Driver/Internet
    ${CMAKE_CURRENT_LIST_DIR}
)

# Define which C files (.c) to compile
target_sources(LANTester PRIVATE
    lib/pico-ssd1306/ssd1306.c     # <-- Compile the display driver directly!
    lib/ioLibrary_Driver/Ethernet/wizchip_conf.c
    lib/ioLibrary_Driver/Ethernet/W5500/w5500.c
    lib/ioLibrary_Driver/Ethernet/socket.c
    lib/ioLibrary_Driver/Internet/DHCP/dhcp.c
)

# -------------------------------------------------------------------------
# 2. LINK EVERYTHING TOGETHER
# -------------------------------------------------------------------------
target_link_libraries(LANTester PRIVATE
    pico_stdlib
    pico_unique_id
    hardware_spi       
    hardware_i2c       
    hardware_adc       
    hardware_timer
    hardware_clocks     
)

# -------------------------------------------------------------------------
# 3. PROJECT SETTINGS
# -------------------------------------------------------------------------
pico_set_program_name(LANTester "LANTester")
pico_set_program_version(LANTester "0.1")

# Output over USB enabled, UART disabled
pico_enable_stdio_uart(LANTester 0)
pico_enable_stdio_usb(LANTester 1)

pico_add_extra_outputs(LANTester)
