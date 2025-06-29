/*
 * Hurricane PIOKMBox Firmware
*/

// init_state_machine.c

#include "init_state_machine.h"
#include "defines.h"
#include "config.h"
#include "timing_config.h"
#include "pico/time.h"
#include <string.h>
#include <stdio.h>

// State transition table
typedef struct {
    init_state_t from_state;
    init_event_t event;
    init_state_t to_state;
    uint32_t timeout_ms;
} state_transition_t;

static const state_transition_t transitions[] = {
    // Power stabilization - use existing magic number
    {INIT_STATE_POWER_STABILIZATION, INIT_EVENT_TIMER_EXPIRED, INIT_STATE_SYSTEM_SETUP, COLD_BOOT_STABILIZATION_MS},
    
    // System setup
    {INIT_STATE_SYSTEM_SETUP, INIT_EVENT_SUCCESS, INIT_STATE_USB_DEVICE_INIT, USB_DEVICE_STABILIZATION_MS},
    {INIT_STATE_SYSTEM_SETUP, INIT_EVENT_FAILURE, INIT_STATE_ERROR, 0},
    
    // USB device initialization - use longer timeout for device ready
    {INIT_STATE_USB_DEVICE_INIT, INIT_EVENT_SUCCESS, INIT_STATE_CORE1_STARTUP, USB_STACK_READY_DELAY_MS},
    {INIT_STATE_USB_DEVICE_INIT, INIT_EVENT_FAILURE, INIT_STATE_RETRY, USB_STACK_READY_DELAY_MS},
    
    // Core1 startup
    {INIT_STATE_CORE1_STARTUP, INIT_EVENT_SUCCESS, INIT_STATE_WAITING_CORE1, USB_INIT_PROGRESSIVE_DELAY_MS},
    {INIT_STATE_CORE1_STARTUP, INIT_EVENT_FAILURE, INIT_STATE_RETRY, USB_INIT_PROGRESSIVE_DELAY_MS},
    
    // Waiting for core1 - use core1 timeout from magic numbers
    {INIT_STATE_WAITING_CORE1, INIT_EVENT_CORE1_READY, INIT_STATE_WATCHDOG_START, WATCHDOG_INIT_DELAY_MS},
    {INIT_STATE_WAITING_CORE1, INIT_EVENT_TIMER_EXPIRED, INIT_STATE_RETRY, USB_INIT_PROGRESSIVE_DELAY_MS},
    
    // Watchdog start - use power stabilization delay
    {INIT_STATE_WATCHDOG_START, INIT_EVENT_SUCCESS, INIT_STATE_POWER_ENABLE, FINAL_STABILIZATION_DELAY_MS},
    {INIT_STATE_WATCHDOG_START, INIT_EVENT_FAILURE, INIT_STATE_RETRY, USB_INIT_PROGRESSIVE_DELAY_MS},
    
    // Power enable - use power enable delay from magic numbers
    {INIT_STATE_POWER_ENABLE, INIT_EVENT_SUCCESS, INIT_STATE_FINAL_CHECKS, POWER_ENABLE_DELAY_MS},
    {INIT_STATE_POWER_ENABLE, INIT_EVENT_FAILURE, INIT_STATE_RETRY, USB_INIT_PROGRESSIVE_DELAY_MS},
    
    // Final checks
    {INIT_STATE_FINAL_CHECKS, INIT_EVENT_SUCCESS, INIT_STATE_COMPLETE, 0},
    {INIT_STATE_FINAL_CHECKS, INIT_EVENT_FAILURE, INIT_STATE_RETRY, USB_INIT_PROGRESSIVE_DELAY_MS},
    
    // Retry logic - back to system setup with retry delay
    {INIT_STATE_RETRY, INIT_EVENT_TIMER_EXPIRED, INIT_STATE_SYSTEM_SETUP, 0},
    {INIT_STATE_RETRY, INIT_EVENT_RETRY_LIMIT_REACHED, INIT_STATE_ERROR, 0},
};

void init_state_machine_init(init_state_machine_t* sm) {
    memset(sm, 0, sizeof(init_state_machine_t));
    sm->current_state = INIT_STATE_POWER_STABILIZATION;
    sm->state_entry_time = to_ms_since_boot(get_absolute_time());
    sm->state_timeout_ms = COLD_BOOT_STABILIZATION_MS;  // From magic_numbers.h
    sm->max_retries = USB_INIT_MAX_RETRIES;               // From magic_numbers.h
}

bool init_state_machine_process(init_state_machine_t* sm, init_event_t event) {
    const uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // Check for timeout only if we haven't already been given a timer expired event
    if (event != INIT_EVENT_TIMER_EXPIRED &&
        sm->state_timeout_ms > 0 &&
        (current_time - sm->state_entry_time) >= sm->state_timeout_ms) {
        event = INIT_EVENT_TIMER_EXPIRED;
    }
    
    // Find matching transition
    for (size_t i = 0; i < sizeof(transitions) / sizeof(transitions[0]); i++) {
        if (transitions[i].from_state == sm->current_state && 
            transitions[i].event == event) {
            
            // Handle retry counting
            if (sm->current_state == INIT_STATE_RETRY) {
                sm->retry_count++;
                if (sm->retry_count >= sm->max_retries) {
                    event = INIT_EVENT_RETRY_LIMIT_REACHED;
                    continue; // Re-check transitions with new event
                }
            }
            
            // State transition
            sm->previous_state = sm->current_state;
            sm->current_state = transitions[i].to_state;
            sm->state_entry_time = current_time;
            sm->state_timeout_ms = transitions[i].timeout_ms;
            
            LOG_INIT("State: %s -> %s (event: %d)", 
                    init_state_to_string(sm->previous_state),
                    init_state_to_string(sm->current_state),
                    event);
            
            return true;
        }
    }
    
    // No valid transition found
    LOG_ERROR("Invalid transition from state %s with event %d", 
             init_state_to_string(sm->current_state), event);
    return false;
}

const char* init_state_to_string(init_state_t state) {
    switch (state) {
        case INIT_STATE_POWER_STABILIZATION: return "POWER_STABILIZATION";
        case INIT_STATE_SYSTEM_SETUP: return "SYSTEM_SETUP";
        case INIT_STATE_USB_DEVICE_INIT: return "USB_DEVICE_INIT";
        case INIT_STATE_CORE1_STARTUP: return "CORE1_STARTUP";
        case INIT_STATE_WAITING_CORE1: return "WAITING_CORE1";
        case INIT_STATE_WATCHDOG_START: return "WATCHDOG_START";
        case INIT_STATE_POWER_ENABLE: return "POWER_ENABLE";
        case INIT_STATE_FINAL_CHECKS: return "FINAL_CHECKS";
        case INIT_STATE_COMPLETE: return "COMPLETE";
        case INIT_STATE_ERROR: return "ERROR";
        case INIT_STATE_RETRY: return "RETRY";
        default: return "UNKNOWN";
    }
}

bool init_state_machine_is_complete(const init_state_machine_t* sm) {
    return sm->current_state == INIT_STATE_COMPLETE;
}

bool init_state_machine_has_error(const init_state_machine_t* sm) {
    return sm->current_state == INIT_STATE_ERROR;
}
