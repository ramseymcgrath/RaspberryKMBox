// init_state_machine.h - Initialization State Machine

#ifndef INIT_STATE_MACHINE_H
#define INIT_STATE_MACHINE_H

#include <stdint.h>
#include <stdbool.h>

//--------------------------------------------------------------------+
// Initialization States
//--------------------------------------------------------------------+

typedef enum {
    INIT_STATE_POWER_STABILIZATION,
    INIT_STATE_SYSTEM_SETUP,
    INIT_STATE_USB_DEVICE_INIT,
    INIT_STATE_CORE1_STARTUP,
    INIT_STATE_WAITING_CORE1,
    INIT_STATE_WATCHDOG_START,
    INIT_STATE_POWER_ENABLE,
    INIT_STATE_FINAL_CHECKS,
    INIT_STATE_COMPLETE,
    INIT_STATE_ERROR,
    INIT_STATE_RETRY
} init_state_t;

typedef enum {
    INIT_EVENT_TIMER_EXPIRED,
    INIT_EVENT_SUCCESS,
    INIT_EVENT_FAILURE,
    INIT_EVENT_RETRY_LIMIT_REACHED,
    INIT_EVENT_CORE1_READY,
    INIT_EVENT_RESET_REQUEST
} init_event_t;

//--------------------------------------------------------------------+
// State Machine Context
//--------------------------------------------------------------------+

typedef struct {
    init_state_t current_state;
    init_state_t previous_state;
    uint32_t state_entry_time;
    uint32_t state_timeout_ms;
    int retry_count;
    int max_retries;
    bool error_occurred;
    char error_message[64];
} init_state_machine_t;

//--------------------------------------------------------------------+
// State Machine Functions
//--------------------------------------------------------------------+

void init_state_machine_init(init_state_machine_t* sm);
bool init_state_machine_process(init_state_machine_t* sm, init_event_t event);
const char* init_state_to_string(init_state_t state);
bool init_state_machine_is_complete(const init_state_machine_t* sm);
bool init_state_machine_has_error(const init_state_machine_t* sm);

#endif // INIT_STATE_MACHINE_H

