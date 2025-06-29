#ifndef CONFIG_H
#define CONFIG_H

// Include the consolidated defines
#include "defines.h"

//--------------------------------------------------------------------+
// Build Configuration Overrides
//--------------------------------------------------------------------+

// Board selection is handled via CMake with -DTARGET_BOARD=adafruit_feather_rp2350
// See CMakeLists.txt for supported boards

#if BUILD_CONFIG == BUILD_CONFIG_PRODUCTION
    // Production: Conservative, reliable settings
    #undef USB_INIT_MAX_RETRIES
    #define USB_INIT_MAX_RETRIES                12
    
    #undef ERROR_RETRY_DELAY_MS
    #define ERROR_RETRY_DELAY_MS                1000
    
    #undef COLD_BOOT_STABILIZATION_MS
    #define COLD_BOOT_STABILIZATION_MS          3000
    
#elif BUILD_CONFIG == BUILD_CONFIG_TESTING
    // Testing: Fast iteration, extensive logging
    #undef USB_INIT_MAX_RETRIES
    #define USB_INIT_MAX_RETRIES                3
    
    #undef ERROR_RETRY_DELAY_MS
    #define ERROR_RETRY_DELAY_MS                100
    
    #undef COLD_BOOT_STABILIZATION_MS
    #define COLD_BOOT_STABILIZATION_MS          500
    
#elif BUILD_CONFIG == BUILD_CONFIG_DEBUG
    // Debug: Maximum retries, detailed logging
    #undef USB_INIT_MAX_RETRIES
    #define USB_INIT_MAX_RETRIES                20
    
    #undef ERROR_RETRY_DELAY_MS
    #define ERROR_RETRY_DELAY_MS                500
    
#endif

//--------------------------------------------------------------------+
// RP2040/RP2350 Build Configuration Overrides
//--------------------------------------------------------------------+

#if BUILD_CONFIG == BUILD_CONFIG_PRODUCTION
    // Production: Conservative timing settings
    #undef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
    #define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64
    
    #undef PICO_FLASH_SPI_CLKDIV
    #if defined(TARGET_RP2350)
        #define PICO_FLASH_SPI_CLKDIV 2  // RP2350 production setting
    #else
        #define PICO_FLASH_SPI_CLKDIV 4  // RP2040 production setting
    #endif
    
#elif BUILD_CONFIG == BUILD_CONFIG_TESTING
    // Testing: Faster flash access for development
    #undef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
    #define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 32
    
    #undef PICO_FLASH_SPI_CLKDIV
    #if defined(TARGET_RP2350)
        #define PICO_FLASH_SPI_CLKDIV 1  // RP2350 testing setting (faster)
    #else
        #define PICO_FLASH_SPI_CLKDIV 2  // RP2040 testing setting (faster)
    #endif
    
#elif BUILD_CONFIG == BUILD_CONFIG_DEBUG
    // Debug: Most conservative settings
    #undef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
    #define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 128
    
    #undef PICO_FLASH_SPI_CLKDIV
    #if defined(TARGET_RP2350)
        #define PICO_FLASH_SPI_CLKDIV 4  // RP2350 debug setting (more conservative)
    #else
        #define PICO_FLASH_SPI_CLKDIV 8  // RP2040 debug setting (more conservative)
    #endif
    
#endif

#endif // CONFIG_H
