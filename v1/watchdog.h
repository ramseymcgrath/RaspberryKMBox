/*
 * Watchdog System for PIOKMbox
 * 
 * This module implements a dual-core watchdog system that monitors both CPU cores
 * and resets the device if either core becomes unresponsive.
 * 
 * Features:
 * - Hardware watchdog timer for ultimate safety
 * - Inter-core heartbeat monitoring
 * - Configurable timeout periods
 * - Graceful shutdown handling
 * - Debug logging for troubleshooting
 */

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>
#include "defines.h"
//--------------------------------------------------------------------+
// WATCHDOG CONFIGURATION
//--------------------------------------------------------------------+

// All watchdog constants are now in defines.h

//--------------------------------------------------------------------+
// WATCHDOG STATUS STRUCTURE
//--------------------------------------------------------------------+

typedef struct {
    uint32_t core0_heartbeat_count;     // Core 0 heartbeat counter
    uint32_t core1_heartbeat_count;     // Core 1 heartbeat counter
    uint32_t core0_last_heartbeat_ms;   // Last heartbeat time from core 0
    uint32_t core1_last_heartbeat_ms;   // Last heartbeat time from core 1
    uint32_t hardware_updates;          // Hardware watchdog update counter
    uint32_t timeout_warnings;          // Number of timeout warnings issued
    bool core0_responsive;              // Core 0 responsiveness status
    bool core1_responsive;              // Core 1 responsiveness status
    bool system_healthy;                // Overall system health status
} watchdog_status_t;

//--------------------------------------------------------------------+
// WATCHDOG API
//--------------------------------------------------------------------+

/**
 * Initialize the watchdog system
 * Must be called before any other watchdog functions
 * Should be called from core 0 during system initialization
 */
void watchdog_init(void);

/**
 * Start the watchdog monitoring
 * Enables hardware watchdog and begins inter-core monitoring
 * Should be called after both cores are fully initialized
 */
void watchdog_start(void);

/**
 * Stop the watchdog monitoring
 * Disables hardware watchdog - use with caution!
 * Primarily for graceful shutdown scenarios
 */
void watchdog_stop(void);

/**
 * Core heartbeat functions
 * Each core should call its respective heartbeat function regularly
 * Recommended to call at least every WATCHDOG_HEARTBEAT_INTERVAL_MS
 */
void watchdog_core0_heartbeat(void);
void watchdog_core1_heartbeat(void);

/**
 * Watchdog task function
 * Should be called regularly from the main loop on core 0
 * Handles hardware watchdog updates and inter-core monitoring
 */
void watchdog_task(void);

/**
 * Get current watchdog status
 * Returns a copy of the current watchdog status structure
 * Useful for debugging and system monitoring
 */
watchdog_status_t watchdog_get_status(void);

/**
 * Check if system is healthy
 * Returns true if both cores are responsive and system is healthy
 * Quick health check function for other modules
 */
bool watchdog_is_system_healthy(void);

/**
 * Force a system reset
 * Immediately triggers a hardware reset
 * Use only in emergency situations
 */
void watchdog_force_reset(void);

/**
 * Enable/disable debug output
 * Controls whether watchdog debug messages are printed
 */
void watchdog_set_debug(bool enable);

#endif // WATCHDOG_H