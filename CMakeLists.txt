# PIOKMbox - Raspberry Pi Pico USB HID Project
# CMake configuration file
#
# This project implements a USB HID keyboard/mouse bridge for Raspberry Pi Pico
# Supporting both RP2040 and RP2350 chips with various board configurations

cmake_minimum_required(VERSION 3.13)

# ==============================================================================
# Project Version and Metadata
# ==============================================================================
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 1)
set(PROJECT_VERSION_PATCH 0)
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

# ==============================================================================
# CMake Configuration and Policies
# ==============================================================================
# Enable useful CMake features
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_VERBOSE_MAKEFILE OFF)

# Set CMake policies for consistent behavior
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)  # Use timestamps from archives
endif()

# ==============================================================================
# Build Configuration Options
# ==============================================================================
# Define build type with validation
set(VALID_BUILD_TYPES Debug Release MinSizeRel RelWithDebInfo)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING 
        "Build type: ${VALID_BUILD_TYPES}" FORCE)
endif()

# Validate build type
if(NOT CMAKE_BUILD_TYPE IN_LIST VALID_BUILD_TYPES)
    message(FATAL_ERROR "Invalid build type: ${CMAKE_BUILD_TYPE}. Valid types: ${VALID_BUILD_TYPES}")
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ==============================================================================
# Compiler Configuration
# ==============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags based on build type
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(
        -O3                    # Maximum optimization
        -flto                  # Link-time optimization
        -ffunction-sections    # Place functions in separate sections
        -fdata-sections        # Place data in separate sections
        -fno-exceptions        # Disable C++ exceptions
        -fno-rtti             # Disable RTTI
        -fno-threadsafe-statics # Disable thread-safe statics
    )
    add_link_options(
        -Wl,--gc-sections     # Remove unused sections
        -flto                 # Link-time optimization
    )
    add_compile_definitions(NDEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(
        -Og                   # Optimize for debugging
        -g3                   # Maximum debug info
        -ggdb                 # GDB-specific debug info
        -fno-omit-frame-pointer
    )
    add_compile_definitions(DEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    add_compile_options(
        -Os                   # Optimize for size
        -flto
        -ffunction-sections
        -fdata-sections
    )
    add_link_options(
        -Wl,--gc-sections
        -flto
    )
endif()

# Common warning flags
add_compile_options(
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-unused-variable
)

# ==============================================================================
# SDK Configuration
# ==============================================================================
# Platform-specific home directory
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()

# SDK versions (can be overridden)
set(PICO_SDK_VERSION "2.1.1" CACHE STRING "Pico SDK version")
set(PICO_TOOLCHAIN_VERSION "14_2_Rel1" CACHE STRING "Toolchain version")
set(PICO_PICOTOOL_VERSION "2.1.1" CACHE STRING "Picotool version")

# Include Pico VS Code integration if available
set(PICO_VSCODE_CMAKE "${USERHOME}/.pico/pico-vscode.cmake" CACHE STRING 
    "Path to Pico VS Code integration CMake file")
if(EXISTS ${PICO_VSCODE_CMAKE})
    include(${PICO_VSCODE_CMAKE})
    message(STATUS "Pico VS Code integration loaded")
endif()

# ==============================================================================
# Board Configuration
# ==============================================================================
# Define board configurations in a structured way
set(BOARD_CONFIGS
    "pico:RP2040:Raspberry Pi Pico:STANDARD"
    "pico2:RP2350:Raspberry Pi Pico 2:STANDARD"
    "adafruit_feather_rp2040_usb_host:RP2040:Adafruit Feather RP2040 USB Host:STANDARD"
    "adafruit_feather_rp2350:RP2350:Adafruit Feather RP2350:STANDARD"
    "adafruit_feather_rp2350_max3421e:RP2350:Adafruit Feather RP2350 with MAX3421E:MAX3421E"
    "seeseeed_xiao_rp2350:RP2350:Seeed XIAO RP2350:STANDARD"
)

# Extract supported board names
set(SUPPORTED_BOARDS "")
foreach(config ${BOARD_CONFIGS})
    string(REPLACE ":" ";" config_parts ${config})
    list(GET config_parts 0 board_name)
    list(APPEND SUPPORTED_BOARDS ${board_name})
endforeach()

# Set default board if not specified
if(NOT DEFINED TARGET_BOARD)
    set(TARGET_BOARD "pico" CACHE STRING 
        "Target board type. Options: ${SUPPORTED_BOARDS}")
endif()

# Parse board configuration
set(BOARD_FOUND FALSE)
foreach(config ${BOARD_CONFIGS})
    string(REPLACE ":" ";" config_parts ${config})
    list(GET config_parts 0 board_name)
    list(GET config_parts 1 chip_type)
    list(GET config_parts 2 board_desc)
    list(GET config_parts 3 usb_mode)
    
    if(TARGET_BOARD STREQUAL board_name)
        set(BOARD_FOUND TRUE)
        set(TARGET_CHIP ${chip_type})
        set(BOARD_DESCRIPTION ${board_desc})
        
        # Special handling for MAX3421E board
        if(board_name STREQUAL "adafruit_feather_rp2350_max3421e")
            set(PICO_BOARD "adafruit_feather_rp2350" CACHE STRING "Board type" FORCE)
            set(USB_HOST_MODE "USB_HOST_MAX3421E" CACHE STRING "USB host mode" FORCE)
        else()
            set(PICO_BOARD ${board_name} CACHE STRING "Board type" FORCE)
            if(usb_mode STREQUAL "MAX3421E")
                set(USB_HOST_MODE "USB_HOST_MAX3421E" CACHE STRING "USB host mode" FORCE)
            else()
                set(USB_HOST_MODE "USB_HOST_PIO" CACHE STRING "USB host mode" FORCE)
            endif()
        endif()
        
        message(STATUS "Target board: ${BOARD_DESCRIPTION}")
        message(STATUS "Target chip: ${TARGET_CHIP}")
        message(STATUS "USB host mode: ${USB_HOST_MODE}")
        break()
    endif()
endforeach()

if(NOT BOARD_FOUND)
    message(FATAL_ERROR 
        "Unsupported board type: ${TARGET_BOARD}\n"
        "Supported boards: ${SUPPORTED_BOARDS}")
endif()

# ==============================================================================
# SDK Import and Validation
# ==============================================================================
# Import Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# Validate SDK path
if(NOT PICO_SDK_PATH)
    message(FATAL_ERROR 
        "Pico SDK path not found!\n"
        "Please set PICO_SDK_PATH environment variable or CMake variable")
endif()

if(NOT EXISTS "${PICO_SDK_PATH}/CMakeLists.txt")
    message(FATAL_ERROR 
        "Invalid Pico SDK path: ${PICO_SDK_PATH}\n"
        "The path does not contain a valid Pico SDK")
endif()

# ==============================================================================
# Project Declaration
# ==============================================================================
project(PIOKMbox 
    VERSION ${PROJECT_VERSION}
    DESCRIPTION "USB HID Keyboard/Mouse Bridge for Raspberry Pi Pico"
    LANGUAGES C CXX ASM
)

# Initialize Pico SDK after project declaration
pico_sdk_init()

# ==============================================================================
# Source File Management
# ==============================================================================
# Core source files (always included)
set(CORE_SOURCES
    PIOKMbox.c
    led_control.c
    watchdog.c
    init_state_machine.c
    state_management.c
    dma_handlers.c
    dma_manager.c
    usb_locks.c
)

# USB HID source files
set(USB_HID_SOURCES
    usb_hid.c
    usb_hid_init.c
    usb_hid_device.c
    usb_hid_host.c
    usb_hid_reports.c
    usb_hid_descriptors.c
    usb_hid_strings.c
    usb_hid_callbacks.c
)

# Combine core sources
set(PROJECT_SOURCES ${CORE_SOURCES} ${USB_HID_SOURCES})

# Chip-specific sources
if(TARGET_CHIP STREQUAL "RP2350")
    set(RP2350_SOURCES
        rp2350_hw_accel.c
        rp2350_tuh_task.c
        rp2350_dma_handler.c
    )
    list(APPEND PROJECT_SOURCES ${RP2350_SOURCES})
    message(STATUS "Including RP2350-specific sources: ${RP2350_SOURCES}")
endif()

# USB host controller specific sources
if(USB_HOST_MODE STREQUAL "USB_HOST_MAX3421E")
    list(APPEND PROJECT_SOURCES max3421e_impl.c)
    message(STATUS "Including MAX3421E USB host controller support")
endif()

# Validate source files exist
foreach(source ${PROJECT_SOURCES})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        message(WARNING "Source file not found: ${source}")
    endif()
endforeach()

# ==============================================================================
# Executable Target
# ==============================================================================
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

# Set program metadata
pico_set_program_name(${PROJECT_NAME} "PIOKMbox")
pico_set_program_version(${PROJECT_NAME} "${PROJECT_VERSION}")
pico_set_program_description(${PROJECT_NAME} "${PROJECT_DESCRIPTION}")

# ==============================================================================
# I/O Configuration Options
# ==============================================================================
option(PIOKMBOX_ENABLE_UART "Enable UART output for debugging" ON)
option(PIOKMBOX_ENABLE_USB_STDIO "Enable USB CDC for stdio (conflicts with HID)" OFF)
option(PIOKMBOX_ENABLE_SEMIHOSTING "Enable semihosting for debugging" OFF)

# Validate I/O configuration
if(PIOKMBOX_ENABLE_USB_STDIO)
    message(WARNING 
        "USB stdio is enabled. This may conflict with USB HID functionality.\n"
        "Consider using UART for debugging instead.")
endif()

# Apply I/O configuration
pico_enable_stdio_uart(${PROJECT_NAME} ${PIOKMBOX_ENABLE_UART})
pico_enable_stdio_usb(${PROJECT_NAME} ${PIOKMBOX_ENABLE_USB_STDIO})
pico_enable_stdio_semihosting(${PROJECT_NAME} ${PIOKMBOX_ENABLE_SEMIHOSTING})

# ==============================================================================
# PIO Configuration
# ==============================================================================
# Generate PIO headers
set(PIO_FILES ws2812.pio)
foreach(pio_file ${PIO_FILES})
    pico_generate_pio_header(${PROJECT_NAME} 
        ${CMAKE_CURRENT_LIST_DIR}/${pio_file}
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated
    )
endforeach()

# ==============================================================================
# Include Directories
# ==============================================================================
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated  # For generated PIO headers
)

# ==============================================================================
# External Dependencies
# ==============================================================================
# PIO USB library
set(PIO_USB_PATH "${CMAKE_CURRENT_LIST_DIR}/lib/pio_usb")
if(NOT EXISTS "${PIO_USB_PATH}/CMakeLists.txt")
    message(FATAL_ERROR 
        "PIO USB library not found at: ${PIO_USB_PATH}\n"
        "Please run: git submodule update --init --recursive")
endif()

add_subdirectory(${PIO_USB_PATH} pio_usb)
message(STATUS "PIO USB library included from: ${PIO_USB_PATH}")

# ==============================================================================
# USB Host Controller Configuration
# ==============================================================================
if(USB_HOST_MODE STREQUAL "USB_HOST_MAX3421E")
    # MAX3421E host controller
    target_sources(${PROJECT_NAME} PRIVATE
        ${PICO_TINYUSB_PATH}/src/portable/analog/max3421/hcd_max3421.c
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        USB_HOST_MODE=1
        USE_MAX3421E=1
        CFG_TUH_MAX3421E=1
        CFG_TUH_RPI_PIO_USB=0
    )
else()
    # PIO USB host controller
    target_sources(${PROJECT_NAME} PRIVATE
        ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/dcd_pio_usb.c
        ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        USB_HOST_MODE=0
        USE_MAX3421E=0
        CFG_TUH_MAX3421E=0
        CFG_TUH_RPI_PIO_USB=1
    )
endif()

# ==============================================================================
# Compile Definitions
# ==============================================================================
# Base definitions for all configurations
target_compile_definitions(${PROJECT_NAME} PRIVATE
    # TinyUSB configuration
    CFG_TUSB_CONFIG_FILE="tusb_config.h"
    BOARD_TUH_RHPORT=1
    BOARD_TUD_RHPORT=0
    
    # PIO USB configuration
    PIO_USB_USE_TINYUSB
    PIO_USB_AVAILABLE=1
    
    # Project configuration
    BUILD_CONFIG=BUILD_CONFIG_DEVELOPMENT
    USE_DMA_MANAGER=1
    PROJECT_VERSION="${PROJECT_VERSION}"
    PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# Chip-specific definitions
if(TARGET_CHIP STREQUAL "RP2350")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        TARGET_RP2350=1
        PICO_RP2350=1
        RP2350=1
        PIN_USB_HOST_DP=10
        PIN_USB_HOST_DM=11
        USE_HARDWARE_ACCELERATION=1
    )
elseif(TARGET_CHIP STREQUAL "RP2040")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        TARGET_RP2040=1
        PICO_RP2040=1
        RP2040=1
    )
endif()

# ==============================================================================
# Library Dependencies
# ==============================================================================
# Core Pico SDK libraries
set(PICO_SDK_LIBS
    pico_stdlib
    pico_unique_id
    pico_multicore
    pico_pio_usb
)

# Add RP2350-specific libraries
if(TARGET_CHIP STREQUAL "RP2350")
    list(APPEND PICO_SDK_LIBS pico_float)
endif()

# Hardware libraries
set(HARDWARE_LIBS
    hardware_pio
    hardware_dma
    hardware_watchdog
    hardware_spi
    hardware_flash
    hardware_sync
)

# TinyUSB libraries
set(TINYUSB_LIBS
    tinyusb_device
    tinyusb_host
    tinyusb_board
)

# System libraries
set(SYSTEM_LIBS
    m  # Math library
)

# Combine all libraries
set(ALL_LIBRARIES
    ${PICO_SDK_LIBS}
    ${HARDWARE_LIBS}
    ${TINYUSB_LIBS}
    ${SYSTEM_LIBS}
)

# Link libraries with error checking
foreach(lib ${ALL_LIBRARIES})
    if(NOT TARGET ${lib})
        message(WARNING "Library target not found: ${lib}")
    endif()
endforeach()

target_link_libraries(${PROJECT_NAME} PRIVATE ${ALL_LIBRARIES})

# ==============================================================================
# Build Output Configuration
# ==============================================================================
# Generate additional output formats
pico_add_extra_outputs(${PROJECT_NAME})

# Custom target for size analysis
add_custom_target(size
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    DEPENDS ${PROJECT_NAME}
    COMMENT "Analyzing binary size"
)

# Custom target for disassembly
add_custom_target(disassembly
    COMMAND ${CMAKE_OBJDUMP} -d $<TARGET_FILE:${PROJECT_NAME}> > ${PROJECT_NAME}.dis
    DEPENDS ${PROJECT_NAME}
    COMMENT "Generating disassembly"
)

# ==============================================================================
# Installation Rules
# ==============================================================================
install(FILES 
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.uf2
    DESTINATION ${CMAKE_INSTALL_PREFIX}/firmware
    OPTIONAL
)

install(FILES
    README.md
    DESTINATION ${CMAKE_INSTALL_PREFIX}/doc
)

# ==============================================================================
# Summary Output
# ==============================================================================
message(STATUS "")
message(STATUS "=== PIOKMbox Build Configuration ===")
message(STATUS "Version:          ${PROJECT_VERSION}")
message(STATUS "Build type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "Target board:     ${TARGET_BOARD} (${BOARD_DESCRIPTION})")
message(STATUS "Target chip:      ${TARGET_CHIP}")
message(STATUS "USB host mode:    ${USB_HOST_MODE}")
message(STATUS "UART output:      ${PIOKMBOX_ENABLE_UART}")
message(STATUS "USB stdio:        ${PIOKMBOX_ENABLE_USB_STDIO}")
message(STATUS "Compiler:         ${CMAKE_C_COMPILER}")
message(STATUS "SDK path:         ${PICO_SDK_PATH}")
message(STATUS "===================================")
message(STATUS "")

# ==============================================================================
# Developer Helpers
# ==============================================================================
# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add a custom target to format code (requires clang-format)
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${PROJECT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting source code"
    )
endif()

# Add a custom target for static analysis (requires clang-tidy)
find_program(CLANG_TIDY clang-tidy)
if(CLANG_TIDY)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        CXX_CLANG_TIDY "${CLANG_TIDY};-checks=*,-fuchsia-*,-google-*,-llvm-*"
    )
endif()
