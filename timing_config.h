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

#endif // TIMING_CONFIG_H
