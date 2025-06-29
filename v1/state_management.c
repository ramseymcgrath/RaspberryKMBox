/*
 * Hurricane PIOKMBox Firmware
*/

#include "state_management.h"
#include "defines.h"
#include <string.h>
// state_management.c
static system_state_t g_system_state = {0};

void system_state_init(system_state_t* state) {
    memset(state, 0, sizeof(system_state_t));
    // Set any non-zero initial values here
}

system_state_t* get_system_state(void) {
    return &g_system_state;
}

bool system_state_should_run_task(const system_state_t* state, uint32_t current_time,
                                  uint32_t last_run_time, uint32_t interval_ms) {
    (void)state; // Suppress unused parameter warning
    return (current_time - last_run_time) >= interval_ms;
}
