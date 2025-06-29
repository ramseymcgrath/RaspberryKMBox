// timing_config.h - Centralized timing constants

#ifndef TIMING_CONFIG_H
#define TIMING_CONFIG_H

// Include the consolidated defines
#include "defines.h"

//--------------------------------------------------------------------+
// Timing Validation Macros
//--------------------------------------------------------------------+

// Compile-time validation of timing relationships
#if BUTTON_HOLD_TRIGGER_MS <= USB_RESET_COOLDOWN_MS
#warning "Button hold time should be less than reset cooldown"
#endif

#if CORE1_EXTRA_INIT_DELAY_MS >= WATCHDOG_HEARTBEAT_INTERVAL_MS
#warning "Core1 init delay may cause watchdog timeout"
#endif

// RP2040/RP2350 timing validations
#if PICO_XOSC_STARTUP_DELAY_MULTIPLIER < 32
#warning "PICO_XOSC_STARTUP_DELAY_MULTIPLIER may be too low for reliable operation"
#endif

#if defined(TARGET_RP2350)
// RP2350-specific validations
#if PICO_FLASH_SPI_CLKDIV < 1
#warning "PICO_FLASH_SPI_CLKDIV is set too low for RP2350, flash access may be unreliable"
#endif
#else
// RP2040-specific validations
#if PICO_FLASH_SPI_CLKDIV < 2
#warning "PICO_FLASH_SPI_CLKDIV is set very low for RP2040, may cause flash access issues"
#endif
#endif

#endif // TIMING_CONFIG_H
