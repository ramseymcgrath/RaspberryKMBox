# PIOKMbox - Raspberry Pi Pico USB HID Project
# CMake configuration file
#
# This project supports multiple Raspberry Pi Pico boards with USB HID functionality
# Build with: cmake -B build -DTARGET_BOARD=<board_name>
#            cmake --build build

cmake_minimum_required(VERSION 3.13)

# Include helper functions
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# ==============================================================================
# Project Configuration
# ==============================================================================

# Project metadata
set(PROJECT_NAME "PIOKMbox")
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 1)
set(PROJECT_VERSION_PATCH 0)
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

# ==============================================================================
# Build Options and Configuration
# ==============================================================================

# Build type configuration with validation
set(VALID_BUILD_TYPES Debug Release MinSizeRel RelWithDebInfo)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type: ${VALID_BUILD_TYPES}" FORCE)
endif()

# Validate build type
if(NOT CMAKE_BUILD_TYPE IN_LIST VALID_BUILD_TYPES)
    message(FATAL_ERROR "Invalid build type: ${CMAKE_BUILD_TYPE}. Valid options: ${VALID_BUILD_TYPES}")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Compiler standards and options
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build-specific compiler flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -Wall -Wextra -Wpedantic)
    add_compile_definitions(DEBUG=1)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -flto -ffunction-sections -fdata-sections)
    add_compile_definitions(NDEBUG)
    add_link_options(-Wl,--gc-sections -flto)
elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    add_compile_options(-Os -flto -ffunction-sections -fdata-sections)
    add_compile_definitions(NDEBUG)
    add_link_options(-Wl,--gc-sections -flto)
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_options(-O2 -g)
endif()

# ==============================================================================
# SDK Configuration
# ==============================================================================

# SDK version requirements
set(REQUIRED_SDK_VERSION 2.1.1)
set(REQUIRED_TOOLCHAIN_VERSION 14_2_Rel1)
set(REQUIRED_PICOTOOL_VERSION 2.1.1)

# Platform-specific home directory
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()

# Pico SDK paths
set(sdkVersion ${REQUIRED_SDK_VERSION})
set(toolchainVersion ${REQUIRED_TOOLCHAIN_VERSION})
set(picotoolVersion ${REQUIRED_PICOTOOL_VERSION})
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)

# Include Pico VS Code extension support if available
if(EXISTS ${picoVscode})
    include(${picoVscode})
    message(STATUS "Pico VS Code extension support loaded")
else()
    message(STATUS "Pico VS Code extension not found, using standard SDK")
endif()

# ==============================================================================
# Board Configuration
# ==============================================================================

# Board configuration table
set(BOARD_CONFIG_RP2040_pico "pico;RP2040;Raspberry Pi Pico")
set(BOARD_CONFIG_RP2040_feather "pico;RP2040;Adafruit Feather RP2040 USB Host")
set(BOARD_CONFIG_RP2350_pico2 "pico2;RP2350;Raspberry Pi Pico 2")
set(BOARD_CONFIG_RP2350_feather "pico2;RP2350;Adafruit Feather RP2350")
set(BOARD_CONFIG_RP2350_xiao "seeed_xiao_rp2350;RP2350;Seeed XIAO RP2350")

# Supported board list
set(SUPPORTED_BOARDS
    "pico"                              # Raspberry Pi Pico (RP2040)
    "adafruit_feather_rp2040_usb_host"  # Adafruit Feather RP2040 USB Host
    "pico2"                             # Raspberry Pi Pico 2 (RP2350)
    "adafruit_feather_rp2350"           # Adafruit Feather RP2350
    "seeed_xiao_rp2350"                 # Seeed XIAO RP2350
)

# Board selection
if(NOT DEFINED TARGET_BOARD)
    set(TARGET_BOARD "pico" CACHE STRING "Target board type. Options: ${SUPPORTED_BOARDS}")
endif()

# Validate and configure board
function(configure_board BOARD_NAME)
    if(BOARD_NAME STREQUAL "pico")
        set(BOARD_CONFIG ${BOARD_CONFIG_RP2040_pico} PARENT_SCOPE)
    elseif(BOARD_NAME STREQUAL "adafruit_feather_rp2040_usb_host")
        set(BOARD_CONFIG ${BOARD_CONFIG_RP2040_feather} PARENT_SCOPE)
    elseif(BOARD_NAME STREQUAL "pico2")
        set(BOARD_CONFIG ${BOARD_CONFIG_RP2350_pico2} PARENT_SCOPE)
    elseif(BOARD_NAME STREQUAL "adafruit_feather_rp2350")
        set(BOARD_CONFIG ${BOARD_CONFIG_RP2350_feather} PARENT_SCOPE)
    elseif(BOARD_NAME STREQUAL "seeed_xiao_rp2350")
        set(BOARD_CONFIG ${BOARD_CONFIG_RP2350_xiao} PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unsupported board: ${BOARD_NAME}\nSupported boards: ${SUPPORTED_BOARDS}")
    endif()
endfunction()

# Configure the selected board
configure_board(${TARGET_BOARD})
list(GET BOARD_CONFIG 0 PICO_BOARD)
list(GET BOARD_CONFIG 1 TARGET_CHIP)
list(GET BOARD_CONFIG 2 BOARD_DESCRIPTION)

set(PICO_BOARD ${PICO_BOARD} CACHE STRING "Board type" FORCE)
message(STATUS "Target board: ${BOARD_DESCRIPTION}")
message(STATUS "Target chip: ${TARGET_CHIP}")
message(STATUS "Pico board: ${PICO_BOARD}")

# ==============================================================================
# SDK Initialization
# ==============================================================================

# Import Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# Validate SDK availability
if(NOT PICO_SDK_PATH)
    message(FATAL_ERROR "Pico SDK path not found. Please set PICO_SDK_PATH environment variable.")
endif()

# Initialize project
project(${PROJECT_NAME} 
    VERSION ${PROJECT_VERSION}
    DESCRIPTION "Raspberry Pi Pico USB HID Project"
    LANGUAGES C CXX ASM
)

# Initialize Pico SDK
pico_sdk_init()

# Include helper functions after SDK init
include(PIOKMboxHelpers)

# Validate tools and SDK
check_required_tools()
validate_sdk_version()

# ==============================================================================
# Source Files Configuration
# ==============================================================================

# Core source files (common to all boards)
set(CORE_SOURCES
    PIOKMbox.c
    led_control.c
    usb_hid.c
    usb_hid_init.c
    usb_hid_device.c
    usb_hid_host.c
    usb_hid_reports.c
    usb_hid_descriptors.c
    usb_hid_strings.c
    usb_hid_callbacks.c
    watchdog.c
    init_state_machine.c
    state_management.c
    dma_handlers.c
    dma_manager.c
    usb_locks.c
)

# Platform-specific source files
set(RP2350_SOURCES
    rp2350_hw_accel.c
    rp2350_dma_handler.c
    tuh_task_wrapper.c
)

# Optional source files (check existence)
set(OPTIONAL_SOURCES
    usb_hid_stats.c
)

# Build final source list
set(PROJECT_SOURCES ${CORE_SOURCES})

# Add platform-specific sources
if(TARGET_CHIP STREQUAL "RP2350")
    list(APPEND PROJECT_SOURCES ${RP2350_SOURCES})
    message(STATUS "Added RP2350-specific source files")
endif()

# Add optional sources if they exist
foreach(SOURCE ${OPTIONAL_SOURCES})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE})
        list(APPEND PROJECT_SOURCES ${SOURCE})
        message(STATUS "Added optional source: ${SOURCE}")
    endif()
endforeach()

# Validate all source files exist
foreach(SOURCE ${PROJECT_SOURCES})
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE})
        message(FATAL_ERROR "Source file not found: ${SOURCE}")
    endif()
endforeach()

# ==============================================================================
# Target Configuration
# ==============================================================================

# Create executable target
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

# Set program metadata
pico_set_program_name(${PROJECT_NAME} "${PROJECT_NAME}")
pico_set_program_version(${PROJECT_NAME} "${PROJECT_VERSION}")
pico_set_program_description(${PROJECT_NAME} "USB HID Project for Raspberry Pi Pico")

# ==============================================================================
# I/O Configuration
# ==============================================================================

# I/O options with descriptions
option(ENABLE_UART_OUTPUT "Enable debug output via UART" ON)
option(ENABLE_USB_OUTPUT "Enable debug output via USB CDC" OFF)

# Validate I/O configuration
if(ENABLE_USB_OUTPUT AND ENABLE_UART_OUTPUT)
    message(WARNING "Both UART and USB output enabled. This may cause conflicts.")
endif()

# Apply I/O configuration
pico_enable_stdio_uart(${PROJECT_NAME} ${ENABLE_UART_OUTPUT})
pico_enable_stdio_usb(${PROJECT_NAME} ${ENABLE_USB_OUTPUT})

if(ENABLE_UART_OUTPUT)
    message(STATUS "UART output: ENABLED")
else()
    message(STATUS "UART output: DISABLED")
endif()

if(ENABLE_USB_OUTPUT)
    message(STATUS "USB output: ENABLED")
else()
    message(STATUS "USB output: DISABLED")
endif()

# ==============================================================================
# PIO Configuration
# ==============================================================================

# Generate PIO headers
set(PIO_FILES ws2812.pio)

foreach(PIO_FILE ${PIO_FILES})
    if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/${PIO_FILE})
        pico_generate_pio_header(${PROJECT_NAME} 
            ${CMAKE_CURRENT_LIST_DIR}/${PIO_FILE} 
            OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}
        )
        message(STATUS "Generated PIO header for: ${PIO_FILE}")
    else()
        message(WARNING "PIO file not found: ${PIO_FILE}")
    endif()
endforeach()

# ==============================================================================
# Include Directories
# ==============================================================================

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# ==============================================================================
# PIO USB Library Configuration
# ==============================================================================

# Check for PIO USB library
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/lib/pio_usb/CMakeLists.txt)
    add_subdirectory(lib/pio_usb)
    message(STATUS "PIO USB library: FOUND")
    
    # Add PIO USB HCD implementation
    if(PICO_TINYUSB_PATH)
        set(PIO_USB_SOURCES
            ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/dcd_pio_usb.c
            ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
        )
        
        # Validate PIO USB source files
        foreach(SOURCE ${PIO_USB_SOURCES})
            if(EXISTS ${SOURCE})
                target_sources(${PROJECT_NAME} PRIVATE ${SOURCE})
            else()
                message(WARNING "PIO USB source not found: ${SOURCE}")
            endif()
        endforeach()
    else()
        message(WARNING "PICO_TINYUSB_PATH not set, PIO USB HCD not added")
    endif()
else()
    message(FATAL_ERROR "PIO USB library not found at lib/pio_usb")
endif()

# ==============================================================================
# Compile Definitions
# ==============================================================================

# Common definitions for all configurations
target_compile_definitions(${PROJECT_NAME} PRIVATE
    # TinyUSB configuration
    CFG_TUSB_CONFIG_FILE="tusb_config.h"
    BOARD_TUH_RHPORT=1
    BOARD_TUD_RHPORT=0
    
    # PIO USB configuration
    PIO_USB_USE_TINYUSB
    CFG_TUH_RPI_PIO_USB=1
    
    # Build configuration
    BUILD_CONFIG=BUILD_CONFIG_DEVELOPMENT
    USE_DMA_MANAGER=1
    
    # Version information
    PROJECT_VERSION="${PROJECT_VERSION}"
    PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# Platform-specific definitions
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

# Board-specific definitions
if(TARGET_BOARD MATCHES "adafruit_feather")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        TARGET_RP2350=1
        PICO_RP2350=1
        RP2350=1
        BOARD_ADAFRUIT_FEATHER_RP2350=1
        # Adafruit Feather RP2350 specific pin definitions
        # Make sure your usb host pins match your board
        PIN_USB_HOST_DP=10
        PIN_USB_HOST_DM=11
        USE_HARDWARE_ACCELERATION
    )
elseif(TARGET_BOARD STREQUAL "seeed_xiao_rp2350")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        BOARD_SEEED_XIAO_RP2350=1
        TARGET_RP2350=1
        PICO_RP2350=1
        RP2350=1
        # XIAO RP2350 specific pin definitions
        PIN_USB_HOST_DP=9   # D+ pin for USB host
        PIN_USB_HOST_DM=10   # D- pin for USB host
        USE_HARDWARE_ACCELERATION=1

    )
endif()

# ==============================================================================
# Library Dependencies
# ==============================================================================

# Required libraries
set(REQUIRED_LIBRARIES
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_watchdog
    pico_unique_id
    pico_multicore
)

# USB libraries
set(USB_LIBRARIES
    tinyusb_device
    tinyusb_host
    tinyusb_board
    pico_pio_usb
)

# Optional libraries
set(OPTIONAL_LIBRARIES
    hardware_adc
    hardware_i2c
    hardware_spi
)

# Math library
set(SYSTEM_LIBRARIES m)

# Build final library list
set(PROJECT_LIBRARIES ${REQUIRED_LIBRARIES} ${USB_LIBRARIES} ${SYSTEM_LIBRARIES})

# Add optional libraries if available
foreach(LIB ${OPTIONAL_LIBRARIES})
    if(TARGET ${LIB})
        list(APPEND PROJECT_LIBRARIES ${LIB})
        message(STATUS "Added optional library: ${LIB}")
    endif()
endforeach()

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_LIBRARIES})

# Platform-specific linker options
if(TARGET_CHIP STREQUAL "RP2350")
    target_link_options(${PROJECT_NAME} PRIVATE
        -Wl,--wrap=tuh_task
    )
    message(STATUS "Added RP2350 linker wrapping for tuh_task")
endif()

# ==============================================================================
# Build Output Configuration
# ==============================================================================

# Generate additional output formats
pico_add_extra_outputs(${PROJECT_NAME})

# Custom target for size information
add_custom_target(size
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    DEPENDS ${PROJECT_NAME}
    COMMENT "Binary size information:"
)

# Custom target for flashing
add_custom_target(flash
    COMMAND picotool load -f $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.uf2
    DEPENDS ${PROJECT_NAME}
    COMMENT "Flashing ${PROJECT_NAME}.uf2 to device"
)

# Installation rules (optional)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.uf2
    DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/firmware
    OPTIONAL
)

# Generate firmware info header
generate_firmware_info(${PROJECT_NAME})

# Configure hardware features
configure_hardware_features(${PROJECT_NAME} ${TARGET_CHIP})

# Generate build documentation
set(BUILD_DATE ${CMAKE_CURRENT_DATE})
set(BUILD_TIME ${CMAKE_CURRENT_TIME})
generate_build_docs()

# ==============================================================================
# Build Summary
# ==============================================================================

# Use the pretty print function from helpers
print_config_summary()

# ==============================================================================
# Developer Options (Advanced)
# ==============================================================================

# Advanced options (hidden by default)
option(ENABLE_VERBOSE_BUILD "Enable verbose build output" OFF)
mark_as_advanced(ENABLE_VERBOSE_BUILD)

if(ENABLE_VERBOSE_BUILD)
    set(CMAKE_VERBOSE_MAKEFILE ON)
endif()

# Code coverage options (for Debug builds)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    option(ENABLE_COVERAGE "Enable code coverage" OFF)
    mark_as_advanced(ENABLE_COVERAGE)
    
    if(ENABLE_COVERAGE)
        add_compile_options(--coverage)
        add_link_options(--coverage)
    endif()
endif()

# ==============================================================================
# Testing Support
# ==============================================================================

option(BUILD_TESTING "Build test executables" OFF)
if(BUILD_TESTING)
    enable_testing()
    message(STATUS "Testing: ENABLED")
    
    # Add test subdirectory if it exists
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
        add_subdirectory(tests)
    endif()
endif()

# ==============================================================================
# Package Configuration
# ==============================================================================

# CPack configuration for creating packages
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "PIOKMbox Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_GENERATOR "ZIP;TGZ")
set(CPACK_SOURCE_GENERATOR "ZIP;TGZ")
set(CPACK_SOURCE_IGNORE_FILES
    "/build/;/.git/;/.vscode/;/\\\\.DS_Store;/.*\\\\.swp"
)
include(CPack)
