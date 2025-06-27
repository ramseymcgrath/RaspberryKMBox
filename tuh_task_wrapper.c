/*
 * TinyUSB Host Task Wrapper with RP2350 Hardware Acceleration
 *
 * This file provides a wrapper implementation of tuh_task() that integrates
 * RP2350 hardware acceleration transparently within the standard function.
 */

#include <stdbool.h>
#include <stdint.h>
#include "tusb.h"
#include "defines.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#ifdef RP2350
#include "rp2350_hw_accel.h"
#endif

// External reference to the original TinyUSB tuh_task implementation
extern void __real_tuh_task(void);

// Global flag to track hardware acceleration status
#ifdef RP2350
extern bool hw_accel_is_enabled(void);
#endif

/**
 * @brief Enhanced tuh_task() with integrated RP2350 hardware acceleration
 *
 * This function wraps the standard tuh_task() and adds hardware acceleration
 * support when running on RP2350. The acceleration is transparent to the caller.
 */
void __wrap_tuh_task(void) {
#ifdef RP2350
    // Check if hardware acceleration is enabled
    if (hw_accel_is_enabled()) {
        // Process any pending hardware-accelerated operations first
        hw_accel_tuh_task();
    }
#endif
    
    // Always call the original tuh_task() for standard processing
    // This ensures compatibility and handles any operations not covered by acceleration
    __real_tuh_task();
}