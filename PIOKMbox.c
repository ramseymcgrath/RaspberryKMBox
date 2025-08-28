/*
 * Hurricane PIOKMBox Firmware
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/unique_id.h"

#include "tusb.h"
#include "defines.h"
#include "config.h"
#include "timing_config.h"
#include "led_control.h"
#include "usb_hid.h"
#include "watchdog.h"
#include "init_state_machine.h"
#include "state_management.h"
#include "kmbox_serial_handler.h"

#if PIO_USB_AVAILABLE
#include "pio_usb.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "tusb.h"
#endif

//--------------------------------------------------------------------+
// Type Definitions and Structures
//--------------------------------------------------------------------+

typedef struct {
    uint32_t watchdog_status_timer;
    uint32_t last_button_press_time;
    bool button_pressed_last;
    bool usb_reset_cooldown;
    uint32_t usb_reset_cooldown_start;
} main_loop_state_t;

typedef struct {
    bool button_pressed;
    uint32_t current_time;
    uint32_t hold_duration;
} button_state_t;

//--------------------------------------------------------------------+
// Constants and Configuration
//--------------------------------------------------------------------+
static const uint32_t WATCHDOG_STATUS_INTERVAL_MS = WATCHDOG_STATUS_REPORT_INTERVAL_MS;

//--------------------------------------------------------------------+
// Function Prototypes
//--------------------------------------------------------------------+

#if PIO_USB_AVAILABLE
static void core1_main(void);
static void core1_task_loop(void);
#endif

static bool initialize_system(void);
static bool initialize_usb_device(void);
static void main_application_loop(void);

// Button handling functions
static button_state_t get_button_state(uint32_t current_time);
static void handle_button_press_start(system_state_t* state, uint32_t current_time);
static void handle_button_held(system_state_t* state, uint32_t current_time);
static void handle_button_release(const system_state_t* state, uint32_t hold_duration);
static void process_button_input(system_state_t* state, uint32_t current_time);

// Reporting functions
static void report_watchdog_status(uint32_t current_time, uint32_t* watchdog_status_timer);

// Utility functions
static inline bool is_time_elapsed(uint32_t current_time, uint32_t last_time, uint32_t interval);

//--------------------------------------------------------------------+
// Core1 Main (USB Host Task)
//--------------------------------------------------------------------+

#if PIO_USB_AVAILABLE
// Separate initialization concerns into focused functions

typedef enum {
    INIT_SUCCESS,
    INIT_FAILURE,
    INIT_RETRY_NEEDED
} init_result_t;

typedef struct {
    int attempt;
    int max_attempts;
    uint32_t base_delay_ms;
    uint32_t last_heartbeat_time;
} init_context_t;

typedef struct {
    uint32_t last_heartbeat_ms;
    uint32_t heartbeat_counter;
} core1_state_t;


static void core1_main(void) {
    // Small delay to let core0 stabilize
    sleep_ms(10);
    
    // CRITICAL: Configure PIO USB BEFORE tuh_init() - this is the key!
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = PIN_USB_HOST_DP;
    pio_cfg.pinout = PIO_USB_PINOUT_DPDM;
    
    // Configure host stack with PIO USB configuration
    tuh_configure(USB_HOST_PORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    
    // Initialize host stack on core1
    tuh_init(USB_HOST_PORT);
    
    // Mark host as initialized
    usb_host_mark_initialized();
    
    // Start the main host task loop
    core1_task_loop();
}

static void core1_task_loop(void) {
    core1_state_t state = {0};  // Local state instead of static

    while (true) {
        tuh_task();
        
        // Check heartbeat timing less frequently
        if (++state.heartbeat_counter >= CORE1_HEARTBEAT_CHECK_LOOPS) {
            const uint32_t current_time = to_ms_since_boot(get_absolute_time());
            if (system_state_should_run_task(NULL, current_time,
                                            state.last_heartbeat_ms,
                                            WATCHDOG_HEARTBEAT_INTERVAL_MS)) {
                watchdog_core1_heartbeat();
                state.last_heartbeat_ms = current_time;
            }
            state.heartbeat_counter = 0;
        }
    }
}

#endif // PIO_USB_AVAILABLE
//--------------------------------------------------------------------+
// System Initialization Functions
//--------------------------------------------------------------------+

static bool initialize_system(void) {
    // Initialize stdio first for early debug output
    stdio_init_all();
    
    // Add extended startup delay for cold boot stability
    sleep_ms(200);
    
    printf("PICO PIO KMBox - Starting initialization...\n");
    printf("Neopixel pins initialized (power OFF for cold boot stability)\n");
    printf("Setting system clock to %d kHz...\n", CPU_FREQ);
    if (!set_sys_clock_khz(CPU_FREQ, true)) {
        printf("CRITICAL: Failed to set system clock to %d kHz\n", CPU_FREQ);
        return false;
    }
    
    // Re-initialize stdio after clock change with proper delay
    sleep_ms(100);  // Allow clock to stabilize
    stdio_init_all();
    sleep_ms(100);  // Allow UART to stabilize
    printf("System clock set successfully to %d kHz\n", CPU_FREQ);
    
    // Configure UART0 for debug output with non-blocking operation
    uart_set_fifo_enabled(uart0, true);  // Enable FIFO for better performance
    
    // Initialize KMBox serial handler on UART1
    kmbox_serial_init();
    
    // Initialize LED control module (neopixel power OFF for now)
    neopixel_init();

    // Initialize USB HID module (USB host power OFF for now)
    usb_hid_init();

    // Initialize watchdog system (but don't start it yet)
    watchdog_init();

    printf("System initialization complete\n");
    return true;
}

static bool initialize_usb_device(void) {
    printf("USB Device: Initializing on controller %d (native USB)...\n", USB_DEVICE_PORT);
    
    const bool device_init_success = tud_init(USB_DEVICE_PORT);
    printf("USB Device init: %s\n", device_init_success ? "SUCCESS" : "FAILED");
    
    if (device_init_success) {
        usb_device_mark_initialized();
        printf("USB Device: Initialization complete\n");
    }
    
    return device_init_success;
}



//--------------------------------------------------------------------+
// Button Handling Functions
//--------------------------------------------------------------------+

static button_state_t get_button_state(uint32_t current_time) {
    button_state_t state = {
        .button_pressed = !gpio_get(PIN_BUTTON), // Button is active low
        .current_time = current_time,
        .hold_duration = 0
    };
    return state;
}

static void handle_button_press_start(system_state_t* state, uint32_t current_time) {
    state->last_button_press_time = current_time;
}

static void handle_button_held(system_state_t* state, uint32_t current_time) {
    if (is_time_elapsed(current_time, state->last_button_press_time, BUTTON_HOLD_TRIGGER_MS)) {
        printf("Button held - triggering USB reset\n");
        usb_stacks_reset();
        state->usb_reset_cooldown = true;
        state->usb_reset_cooldown_start = current_time;
    }
}

static void handle_button_release(const system_state_t* state, uint32_t hold_duration) {
    (void)state; // Suppress unused parameter warning
    if (hold_duration < BUTTON_HOLD_TRIGGER_MS) {
    }
}

static void process_button_input(system_state_t* state, uint32_t current_time) {
    const button_state_t button = get_button_state(current_time);

    // Handle cooldown after USB reset
    if (state->usb_reset_cooldown) {
        if (is_time_elapsed(current_time, state->usb_reset_cooldown_start, USB_RESET_COOLDOWN_MS)) {
            state->usb_reset_cooldown = false;
        }
        state->button_pressed_last = button.button_pressed;
        return; // Skip button processing during cooldown
    }

    if (button.button_pressed && !state->button_pressed_last) {
        // Button just pressed
        handle_button_press_start(state, current_time);
    } else if (button.button_pressed && state->button_pressed_last) {
        // Button being held
        handle_button_held(state, current_time);
    } else if (!button.button_pressed && state->button_pressed_last) {
        // Button just released
        const uint32_t hold_duration = current_time - state->last_button_press_time;
        handle_button_release(state, hold_duration);
    }

    state->button_pressed_last = button.button_pressed;
}

//--------------------------------------------------------------------+
// Reporting Functions
//--------------------------------------------------------------------+

static void report_watchdog_status(uint32_t current_time, uint32_t* watchdog_status_timer) {
    if (!is_time_elapsed(current_time, *watchdog_status_timer, WATCHDOG_STATUS_INTERVAL_MS)) {
        return;
    }

    *watchdog_status_timer = current_time;
    
    const watchdog_status_t watchdog_status = watchdog_get_status();
    
    printf("=== Watchdog Status ===\n");
    printf("System healthy: %s\n", watchdog_status.system_healthy ? "YES" : "NO");
    printf("Core 0: %s (heartbeats: %lu)\n",
           watchdog_status.core0_responsive ? "RESPONSIVE" : "UNRESPONSIVE",
           watchdog_status.core0_heartbeat_count);
    printf("Core 1: %s (heartbeats: %lu)\n",
           watchdog_status.core1_responsive ? "RESPONSIVE" : "UNRESPONSIVE",
           watchdog_status.core1_heartbeat_count);
    printf("Hardware updates: %lu\n", watchdog_status.hardware_updates);
    printf("Timeout warnings: %lu\n", watchdog_status.timeout_warnings);
    printf("=======================\n");
}

//--------------------------------------------------------------------+
// Utility Functions
//--------------------------------------------------------------------+

static inline bool is_time_elapsed(uint32_t current_time, uint32_t last_time, uint32_t interval) {
    return (current_time - last_time) >= interval;
}

//--------------------------------------------------------------------+
// Main Application Loop
//--------------------------------------------------------------------+


static void main_application_loop(void) {
    system_state_t* state = get_system_state();
    system_state_init(state);
    
    // Cache frequently used intervals
    const uint32_t watchdog_interval = WATCHDOG_TASK_INTERVAL_MS;
    const uint32_t visual_interval = VISUAL_TASK_INTERVAL_MS;
    const uint32_t error_interval = ERROR_CHECK_INTERVAL_MS;
    const uint32_t button_interval = BUTTON_DEBOUNCE_MS;

    while (true) {
        // TinyUSB device task - highest priority
        tud_task();
        hid_device_task();
        
        // KMBox serial task - high priority for responsiveness
        kmbox_serial_task();
        
        // Get time once per loop iteration
        const uint32_t current_time = to_ms_since_boot(get_absolute_time());
        
        // Combine time checks to reduce function call overhead
        const uint32_t time_since_watchdog = current_time - state->last_watchdog_time;
        const uint32_t time_since_visual = current_time - state->last_visual_time;
        const uint32_t time_since_button = current_time - state->last_button_time;
        
        // Watchdog tasks - controlled frequency
        if (time_since_watchdog >= watchdog_interval) {
            watchdog_task();
            watchdog_core0_heartbeat();
            state->last_watchdog_time = current_time;
        }
        
        // LED and visual tasks
        if (time_since_visual >= visual_interval) {
            led_blinking_task();
            neopixel_status_task();
            state->last_visual_time = current_time;
        }
        
        // Error check (simplified - no actual task)
        if (current_time - state->last_error_check_time >= error_interval) {
            state->last_error_check_time = current_time;
        }
        
        // Button input processing
        if (time_since_button >= button_interval) {
            process_button_input(state, current_time);
            state->last_button_time = current_time;
        }
        
        // Periodic reporting - watchdog status only
        if ((current_time - state->watchdog_status_timer) >= WATCHDOG_STATUS_REPORT_INTERVAL_MS) {
            report_watchdog_status(current_time, &state->watchdog_status_timer);
        }
    }
}

//--------------------------------------------------------------------+
// Main Function
//--------------------------------------------------------------------+

int main(void) {
    
    // Set system clock to 120MHz (required for PIO USB)
    set_sys_clock_khz(CPU_FREQ, true);
    
    // Small delay for clock stabilization
    sleep_ms(10);
    
    // Configure SMPS mode pin (if provided by board) to reduce 3V3 ripple at light loads
#ifdef PICO_SMPS_MODE_PIN
    gpio_init(PICO_SMPS_MODE_PIN);
    gpio_set_dir(PICO_SMPS_MODE_PIN, GPIO_OUT);
    gpio_put(PICO_SMPS_MODE_PIN, 1);
#endif

    // Initialize USB 5V power control GPIO if available
#ifdef PIN_USB_5V
    gpio_init(PIN_USB_5V);
    gpio_set_dir(PIN_USB_5V, GPIO_OUT);
    gpio_put(PIN_USB_5V, 0);  // Keep USB power OFF initially
#endif
    
#ifdef PIN_LED
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 1);  // Turn on LED
#endif
    
    printf("=== PIOKMBox Starting ===\n");
    
    // Initialize system components
    printf("BOOTTRACE: calling initialize_system()\n");
    if (!initialize_system()) {
        printf("CRITICAL: System initialization failed\n");
        return -1;
    }
    printf("BOOTTRACE: initialize_system() returned\n");
    
    // Enable USB host power (no-op if PIN_USB_5V is not defined)
    printf("BOOTTRACE: enabling USB host power\n");
    usb_host_enable_power();
    sleep_ms(100);  // Brief power stabilization
    printf("BOOTTRACE: USB host power enabled (or always-on)\n");
    
    // Launch core1 first (like the working example)
    printf("BOOTTRACE: launching core1 for USB host...\n");
    multicore_reset_core1();
    multicore_launch_core1(core1_main);
    printf("BOOTTRACE: core1 launched\n");
    
    // Initialize device stack on core0 (like the working example)
    printf("BOOTTRACE: initializing USB device stack on core0...\n");
    if (!initialize_usb_device()) {
        printf("CRITICAL: USB Device initialization failed\n");
        return -1;
    }
    printf("BOOTTRACE: USB device initialized\n");
    
    // Initialize remaining systems
    printf("BOOTTRACE: initializing watchdog...\n");
    watchdog_init();
    printf("BOOTTRACE: watchdog_init() returned\n");
    // NOTE: For debugging startup hangs we skip the extended, blocking
    // watchdog_start() sequence which performs long sleeps/prints and
    // enables the hardware watchdog. If you need the full watchdog
    // behavior re-enable the call below.
    // watchdog_start();
    printf("BOOTTRACE: watchdog_start() skipped (debug)\n");
    printf("BOOTTRACE: enabling neopixel power\n");
    neopixel_enable_power();
    printf("BOOTTRACE: neopixel power enabled\n");
    
    printf("=== PIOKMBox Ready ===\n");
    printf("BOOTTRACE: about to enter main_application_loop()\n");
    
    // Enter main application loop
    main_application_loop();
    
    return 0;
}
