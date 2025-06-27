# PIOKMbox CMake Helper Functions
# This file contains helper functions and macros for the PIOKMbox project

# ==============================================================================
# Validation Functions
# ==============================================================================

# Function to validate required tools
function(check_required_tools)
    find_program(ARM_GCC arm-none-eabi-gcc)
    find_program(ARM_GXX arm-none-eabi-g++)
    find_program(ARM_SIZE arm-none-eabi-size)
    
    if(NOT ARM_GCC)
        message(FATAL_ERROR "arm-none-eabi-gcc not found. Please install ARM toolchain.")
    endif()
    
    if(NOT ARM_GXX)
        message(FATAL_ERROR "arm-none-eabi-g++ not found. Please install ARM toolchain.")
    endif()
    
    message(STATUS "ARM toolchain: FOUND")
endfunction()

# Function to validate SDK version
function(validate_sdk_version)
    if(PICO_SDK_VERSION_STRING)
        if(PICO_SDK_VERSION_STRING VERSION_LESS REQUIRED_SDK_VERSION)
            message(WARNING "Pico SDK version ${PICO_SDK_VERSION_STRING} is older than required ${REQUIRED_SDK_VERSION}")
        else()
            message(STATUS "Pico SDK version: ${PICO_SDK_VERSION_STRING}")
        endif()
    endif()
endfunction()

# ==============================================================================
# Source File Management
# ==============================================================================

# Function to add source files with validation
function(add_validated_sources TARGET SOURCE_LIST)
    foreach(SOURCE ${SOURCE_LIST})
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE})
            target_sources(${TARGET} PRIVATE ${SOURCE})
        else()
            message(FATAL_ERROR "Required source file not found: ${SOURCE}")
        endif()
    endforeach()
endfunction()

# Function to add optional sources
function(add_optional_sources TARGET SOURCE_LIST)
    foreach(SOURCE ${SOURCE_LIST})
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE})
            target_sources(${TARGET} PRIVATE ${SOURCE})
            message(STATUS "Added optional source: ${SOURCE}")
        endif()
    endforeach()
endfunction()

# ==============================================================================
# Build Configuration
# ==============================================================================

# Macro to set up debug build flags
macro(setup_debug_flags)
    add_compile_options(
        -g3                 # Maximum debug info
        -O0                 # No optimization
        -Wall               # All warnings
        -Wextra             # Extra warnings
        -Wpedantic          # Pedantic warnings
        -Wconversion        # Type conversion warnings
        -Wshadow            # Variable shadowing warnings
        -Wformat=2          # Format string warnings
        -Wunused            # Unused variable warnings
        -Werror=return-type # Error on missing return
    )
    add_compile_definitions(
        DEBUG=1
        PICO_STDIO_ENABLE_CRLF_SUPPORT=1
        PICO_STDIO_DEFAULT_CRLF=1
    )
endmacro()

# Macro to set up release build flags
macro(setup_release_flags)
    add_compile_options(
        -O3                     # Maximum optimization
        -flto                   # Link-time optimization
        -ffunction-sections     # Separate functions
        -fdata-sections         # Separate data
        -fno-exceptions         # No C++ exceptions
        -fno-rtti              # No RTTI
        -fno-threadsafe-statics # No thread-safe statics
    )
    add_link_options(
        -Wl,--gc-sections      # Remove unused sections
        -flto                  # Link-time optimization
    )
    add_compile_definitions(NDEBUG)
endmacro()

# ==============================================================================
# Feature Detection
# ==============================================================================

# Function to detect and configure hardware features
function(configure_hardware_features TARGET CHIP)
    if(CHIP STREQUAL "RP2350")
        # RP2350 specific features
        target_compile_definitions(${TARGET} PRIVATE
            PICO_RP2350_A2_SUPPORTED=1
            PICO_FLASH_SIZE_BYTES=4194304  # 4MB default
        )
        
        # Check for hardware acceleration support
        include(CheckCSourceCompiles)
        check_c_source_compiles("
            #include <hardware/structs/qmi.h>
            int main() { return 0; }
        " HAS_RP2350_QMI)
        
        if(HAS_RP2350_QMI)
            target_compile_definitions(${TARGET} PRIVATE HAS_HARDWARE_QMI=1)
        endif()
    endif()
endfunction()

# ==============================================================================
# Testing Support
# ==============================================================================

# Function to add test targets
function(add_test_target TEST_NAME TEST_SOURCES)
    if(BUILD_TESTING)
        add_executable(${TEST_NAME} ${TEST_SOURCES})
        target_link_libraries(${TEST_NAME} PRIVATE
            pico_stdlib
            hardware_pio
            hardware_dma
        )
        pico_add_extra_outputs(${TEST_NAME})
        
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
        message(STATUS "Added test: ${TEST_NAME}")
    endif()
endfunction()

# ==============================================================================
# Documentation
# ==============================================================================

# Function to generate build documentation
function(generate_build_docs)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_info.md.in
        ${CMAKE_CURRENT_BINARY_DIR}/BUILD_INFO.md
        @ONLY
    )
endfunction()

# ==============================================================================
# Utility Functions
# ==============================================================================

# Function to print configuration summary
function(print_config_summary)
    message(STATUS "")
    message(STATUS "╔════════════════════════════════════════╗")
    message(STATUS "║     PIOKMbox Build Configuration       ║")
    message(STATUS "╠════════════════════════════════════════╣")
    message(STATUS "║ Project Version : ${PROJECT_VERSION}")
    message(STATUS "║ Target Board    : ${TARGET_BOARD}")
    message(STATUS "║ Target Chip     : ${TARGET_CHIP}")
    message(STATUS "║ Build Type      : ${CMAKE_BUILD_TYPE}")
    message(STATUS "║ Compiler        : ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
    message(STATUS "║ SDK Path        : ${PICO_SDK_PATH}")
    message(STATUS "╚════════════════════════════════════════╝")
    message(STATUS "")
endfunction()

# Function to create firmware info header
function(generate_firmware_info TARGET)
    string(TIMESTAMP BUILD_DATE "%Y-%m-%d")
    string(TIMESTAMP BUILD_TIME "%H:%M:%S")
    
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/firmware_info.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/firmware_info.h
        @ONLY
    )
    
    target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()