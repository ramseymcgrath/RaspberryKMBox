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
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#ifdef RP2350
#include "rp2350_hw_accel.h"
#include "rp2350_dma_handler.h"
#endif

#include "tusb.h"
#include "defines.h"
#include "config.h"
#include "timing_config.h"
#include "led_control.h"
#include "usb_hid.h"
#include "watchdog.h"
#include "init_state_machine.h"
#include "state_management.h"
#include "dma_manager.h"
#include "usb_hid_stats.h"

#if PIO_USB_AVAILABLE
#include "pio_usb.h"
#endif


//--------------------------------------------------------------------+
// Type Definitions and Structures
//--------------------------------------------------------------------+

typedef struct {
    uint32_t stats_timer;
    uint32_t watchdog_status_timer;
    uint32_t last_button_press_time;
    bool button_pressed_last;
    bool usb_reset_cooldown;
    uint32_t usb_reset_cooldown_start;
    uint32_t last_heartbeat_time;
    bool heartbeat_state;
} main_loop_state_t;

#ifdef RP2350
// Global variable to track if hardware acceleration is enabled
static bool hw_accel_enabled = false;
#endif

typedef struct {
    bool button_pressed;
    uint32_t current_time;
    uint32_t hold_duration;
} button_state_t;

//--------------------------------------------------------------------+
// Constants and Configuration
//--------------------------------------------------------------------+
static const uint32_t STATS_INTERVAL_MS = STATS_REPORT_INTERVAL_MS;
static const uint32_t WATCHDOG_STATUS_INTERVAL_MS = WATCHDOG_STATUS_REPORT_INTERVAL_MS;

//--------------------------------------------------------------------+
// Function Prototypes
//--------------------------------------------------------------------+

// Core functions
static void core1_main(void);
static void core1_task_loop(void);
static bool initialize_system(void);
static bool initialize_usb_device(void);
static void main_application_loop(void);

// USB controller initialization functions
#if CFG_TUH_ENABLED && CFG_TUH_MAX3421E
static bool initialize_max3421e_controller(void);
#endif
#if CFG_TUH_ENABLED && CFG_TUH_RPI_PIO_USB
static bool initialize_pio_usb_controller(void);
#endif

// Hardware acceleration functions
#ifdef RP2350
static bool init_hid_hardware_acceleration(void);
static bool initialize_rp2350_acceleration(void);
#endif

// Button handling functions
static button_state_t get_button_state(uint32_t current_time);
static void handle_button_press_start(system_state_t* state, uint32_t current_time);
static void handle_button_held(system_state_t* state, uint32_t current_time);
static void handle_button_release(const system_state_t* state, uint32_t hold_duration);
static void process_button_input(system_state_t* state, uint32_t current_time);

// Reporting functions
static void report_hid_statistics(uint32_t current_time, uint32_t* stats_timer);
static void report_watchdog_status(uint32_t current_time, uint32_t* watchdog_status_timer);

// Utility functions
static inline bool is_time_elapsed(uint32_t current_time, uint32_t last_time, uint32_t interval);

#if CFG_TUH_MAX3421E
// API to read/write MAX3421's register. Implemented by TinyUSB
extern uint8_t tuh_max3421_reg_read(uint8_t rhport, uint8_t reg, bool in_isr);
extern bool tuh_max3421_reg_write(uint8_t rhport, uint8_t reg, uint8_t data, bool in_isr);
#endif


//--------------------------------------------------------------------+
// Core1 Main (USB Host Task)
//--------------------------------------------------------------------+

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


/**
 * @brief Core1 main function - handles USB host initialization and task loop
 */
static void core1_main(void) {
    // EXTENDED delay for cold boot - core1 needs more time
    LOG_INIT("Core1: Starting with extended stabilization delay...");
    
#if CFG_TUH_MAX3421E
    // MAX3421E needs extra stabilization time
    LOG_INIT("Core1: MAX3421E detected, adding 3 second stabilization delay...");
    for (int i = 0; i < 30; i++) {
        sleep_ms(100);
        // Send periodic heartbeat during delay
        if (i % 10 == 0) {
            watchdog_core1_heartbeat();
        }
    }
#else
    sleep_ms(500);  // Standard delay for PIO USB
#endif
    
    LOG_INIT("Core1: Starting USB host initialization (cold boot)...");
    
    // Add heartbeat early to prevent watchdog timeout during init
    watchdog_core1_heartbeat();
    
    // Initialize the appropriate USB host controller
#if CFG_TUH_ENABLED && CFG_TUH_MAX3421E
    if (!initialize_max3421e_controller()) {
        LOG_ERROR("Core1: CRITICAL - MAX3421E initialization failed");
        LOG_ERROR("Core1: Will continue attempting to initialize in background");
        // Continue with limited functionality - we'll retry in the task loop
    }
#elif CFG_TUH_ENABLED && CFG_TUH_RPI_PIO_USB
    if (!initialize_pio_usb_controller()) {
        LOG_ERROR("Core1: CRITICAL - PIO USB initialization failed");
        // Continue with limited functionality
    }
#endif
    
#ifdef RP2350
    // Initialize RP2350 hardware acceleration
    if (!initialize_rp2350_acceleration()) {
        LOG_ERROR("Core1: RP2350 hardware acceleration initialization failed, using standard mode");
    }
#endif
    
    // Mark host as initialized
    usb_host_mark_initialized();
    
    LOG_INIT("Core1: USB host initialization complete");
    
    // Start the main host task loop
    core1_task_loop();
}

/**
 * @brief Initialize MAX3421E USB host controller
 *
 * @return true if initialization was successful, false otherwise
 */
#if CFG_TUH_ENABLED && CFG_TUH_MAX3421E

// MAX3421E register definitions for debugging
enum {
    REVISION_ADDR = 18u << 3,     /* 0x90 - Revision register */
    IOPINS1_ADDR = 20u << 3,      /* 0xA0 - GPIO control register */
    IOPINS2_ADDR = 21u << 3,      /* 0xA8 - GPIO direction register */
    HIRQ_ADDR = 25u << 3,         /* 0xC8 - Host interrupt request register */
    HIEN_ADDR = 26u << 3,         /* 0xD0 - Host interrupt enable register */
    MODE_ADDR = 27u << 3,         /* 0xD8 - Mode register */
    HCTL_ADDR = 29u << 3,         /* 0xE8 - Host control register */
    HRSL_ADDR = 31u << 3,         /* 0xF8 - Host result register */
};

static void print_max3421e_registers(void) {
    LOG_INIT("=== MAX3421E Register Dump ===");
    
    uint8_t revision = tuh_max3421_reg_read(BOARD_TUH_RHPORT, REVISION_ADDR, false);
    LOG_INIT("REVISION: 0x%02X (expected 0x12 or 0x13)", revision);
    
    uint8_t iopins1 = tuh_max3421_reg_read(BOARD_TUH_RHPORT, IOPINS1_ADDR, false);
    LOG_INIT("IOPINS1 (GPIO): 0x%02X", iopins1);
    
    uint8_t iopins2 = tuh_max3421_reg_read(BOARD_TUH_RHPORT, IOPINS2_ADDR, false);
    LOG_INIT("IOPINS2 (GPIO DIR): 0x%02X", iopins2);
    
    uint8_t hirq = tuh_max3421_reg_read(BOARD_TUH_RHPORT, HIRQ_ADDR, false);
    LOG_INIT("HIRQ: 0x%02X", hirq);
    
    uint8_t hien = tuh_max3421_reg_read(BOARD_TUH_RHPORT, HIEN_ADDR, false);
    LOG_INIT("HIEN: 0x%02X", hien);
    
    uint8_t mode = tuh_max3421_reg_read(BOARD_TUH_RHPORT, MODE_ADDR, false);
    LOG_INIT("MODE: 0x%02X", mode);
    
    uint8_t hctl = tuh_max3421_reg_read(BOARD_TUH_RHPORT, HCTL_ADDR, false);
    LOG_INIT("HCTL: 0x%02X", hctl);
    
    uint8_t hrsl = tuh_max3421_reg_read(BOARD_TUH_RHPORT, HRSL_ADDR, false);
    LOG_INIT("HRSL: 0x%02X", hrsl);
    
    LOG_INIT("==============================");
}

static bool initialize_max3421e_controller(void) {
    const int MAX_INIT_ATTEMPTS = 3;
    bool init_success = false;
    
    LOG_INIT("Core1: Initializing MAX3421E USB host controller...");
    
    for (int attempt = 1; attempt <= MAX_INIT_ATTEMPTS; attempt++) {
        LOG_INIT("Core1: MAX3421E initialization attempt %d/%d", attempt, MAX_INIT_ATTEMPTS);
        
        // Add delay before each attempt
        if (attempt > 1) {
            LOG_INIT("Core1: Waiting 2 seconds before retry...");
            sleep_ms(2000);
        }
        
        // Initialize TinyUSB host stack
        if (!tuh_init(BOARD_TUH_RHPORT)) {
            LOG_ERROR("Core1: USB host init failed on attempt %d", attempt);
            
            // Print registers for debugging
            LOG_ERROR("Core1: Dumping MAX3421E registers after failure:");
            print_max3421e_registers();
            
            continue; // Try again
        }
        
        LOG_INIT("Core1: USB host stack initialized successfully");
        
        // Add stabilization delay
        sleep_ms(500);
        
        // Check if MAX3421E is responding correctly
        uint8_t revision = tuh_max3421_reg_read(BOARD_TUH_RHPORT, REVISION_ADDR, false);
        if (revision != 0x12 && revision != 0x13) {
            LOG_ERROR("Core1: Invalid MAX3421E revision: 0x%02X (expected 0x12 or 0x13)", revision);
            print_max3421e_registers();
            continue;
        }
        
        LOG_INIT("Core1: MAX3421E revision verified: 0x%02X", revision);
        
        // Now enable VBUS via MAX3421E's GPIO0 (FeatherWing style)
        // The MAX3421E uses its internal GPIO0 pin for VBUS control
        
        // Set GPIO0 as output (bit 0 = 1 for output)
        tuh_max3421_reg_write(BOARD_TUH_RHPORT, IOPINS2_ADDR, 0x01, false);
        sleep_ms(10); // Small delay after register write
        
        // Set GPIO0 high to enable VBUS (bit 0 = 1)
        tuh_max3421_reg_write(BOARD_TUH_RHPORT, IOPINS1_ADDR, 0x01, false);
        sleep_ms(100); // Allow VBUS to stabilize
        
        // Verify VBUS is enabled
        uint8_t gpio_state = tuh_max3421_reg_read(BOARD_TUH_RHPORT, IOPINS1_ADDR, false);
        if ((gpio_state & 0x01) == 0) {
            LOG_ERROR("Core1: Failed to enable VBUS, GPIO0 state: 0x%02X", gpio_state);
            print_max3421e_registers();
            continue;
        }
        
        LOG_INIT("Core1: MAX3421E VBUS enabled successfully via GPIO0");
        
        // Final register dump for successful init
        LOG_INIT("Core1: MAX3421E initialized successfully, final register state:");
        print_max3421e_registers();
        
        init_success = true;
        break;
    }
    
    if (!init_success) {
        LOG_ERROR("Core1: CRITICAL - MAX3421E initialization failed after %d attempts", MAX_INIT_ATTEMPTS);
        LOG_ERROR("Core1: System will continue with limited functionality");
    }
    
    return init_success;
}
#endif

/**
 * @brief Initialize PIO USB host controller
 *
 * @return true if initialization was successful, false otherwise
 */
#if CFG_TUH_ENABLED && CFG_TUH_RPI_PIO_USB
static bool initialize_pio_usb_controller(void) {
    LOG_INIT("Core1: Initializing PIO USB host controller...");
    
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = PIN_USB_HOST_DP;
    pio_cfg.pinout = PIO_USB_PINOUT_DPDM;
    
    // Configure host stack with PIO USB configuration
    if (!tuh_configure(USB_HOST_PORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg)) {
        LOG_ERROR("Core1: CRITICAL - PIO USB configure failed");
        return false;
    }
    
    // Initialize host stack on core1
    if (!tuh_init(USB_HOST_PORT)) {
        LOG_ERROR("Core1: CRITICAL - USB host init failed");
        return false;
    }
    
    LOG_INIT("Core1: PIO USB host controller initialized successfully");
    return true;
}
#endif

/**
 * @brief Initialize RP2350 hardware acceleration
 *
 * @return true if initialization was successful, false otherwise
 */
#ifdef RP2350
static bool initialize_rp2350_acceleration(void) {
    // Initialize hardware acceleration
    hw_accel_enabled = init_hid_hardware_acceleration();
    if (!hw_accel_enabled) {
        return false;
    }
    
    printf("Core1: RP2350 hardware acceleration initialized successfully\n");
    
    // Initialize enhanced tuh_task implementation
    extern bool rp2350_tuh_task_init(void);
    extern bool rp2350_patch_tuh_task(void);
    
    // First initialize the enhanced implementation
    if (!rp2350_tuh_task_init()) {
        printf("Core1: Enhanced tuh_task implementation initialization failed\n");
        return false;
    }
    
    printf("Core1: Enhanced tuh_task implementation initialized successfully\n");
    
    // Then patch the tuh_task function to use our enhanced implementation
    if (!rp2350_patch_tuh_task()) {
        printf("Core1: Failed to patch tuh_task, using direct calls\n");
        return false;
    }
    
    printf("Core1: tuh_task patched successfully\n");
    return true;
}
#endif

/**
 * @brief Core1 task loop - handles USB host tasks and watchdog heartbeat
 */
static void core1_task_loop(void) {
    core1_state_t state = {0};  // Local state instead of static
    
#if CFG_TUH_MAX3421E
    // Track if we need to retry MAX3421E initialization
    static bool max3421e_initialized = false;
    static uint32_t last_reinit_attempt = 0;
    static int reinit_attempts = 0;
    const uint32_t REINIT_INTERVAL_MS = 5000; // Try every 5 seconds
    const int MAX_REINIT_ATTEMPTS = 10;
    
    // Check initial state
    if (!usb_host_is_initialized()) {
        LOG_ERROR("Core1: USB host not initialized, will retry in task loop");
    } else {
        max3421e_initialized = true;
    }
#endif

#ifdef RP2350
    // Retry hardware acceleration initialization if needed
    if (!hw_accel_enabled) {
        initialize_rp2350_acceleration();
    }
#endif

    while (true) {
#if CFG_TUH_MAX3421E
        // Check if we need to retry MAX3421E initialization
        if (!max3421e_initialized && reinit_attempts < MAX_REINIT_ATTEMPTS) {
            uint32_t current_time = to_ms_since_boot(get_absolute_time());
            if (current_time - last_reinit_attempt >= REINIT_INTERVAL_MS) {
                reinit_attempts++;
                LOG_INIT("Core1: Retrying MAX3421E initialization (attempt %d/%d)...",
                         reinit_attempts, MAX_REINIT_ATTEMPTS);
                
                if (initialize_max3421e_controller()) {
                    max3421e_initialized = true;
                    LOG_INIT("Core1: MAX3421E initialization successful on retry!");
                    usb_host_mark_initialized();
                } else {
                    LOG_ERROR("Core1: MAX3421E initialization retry %d failed", reinit_attempts);
                }
                
                last_reinit_attempt = current_time;
            }
        }
#endif

        // Run the appropriate USB host task implementation
#ifdef RP2350
        extern void rp2350_enhanced_tuh_task(void);
        
        if (hw_accel_enabled) {
            // Use hardware-accelerated implementation
            rp2350_enhanced_tuh_task();
        } else {
            // Fall back to standard implementation
            tuh_task();
        }
#else
        // Standard RP2040 implementation
        tuh_task();
#endif
        
        // Handle watchdog heartbeat at controlled intervals
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

#ifdef RP2350
/**
 * @brief Initialize RP2350 hardware acceleration for USB HID processing
 *
 * This function initializes the hardware acceleration features of the RP2350
 * for improved USB HID processing performance.
 *
 * @return true if hardware acceleration was successfully initialized, false otherwise
 */
static bool init_hid_hardware_acceleration(void) {
    LOG_INIT("Initializing RP2350 hardware acceleration for USB HID...");
    
    // Use the implementation from rp2350_hw_accel.c
    extern bool hw_accel_init(void);
    bool success = hw_accel_init();
    
    if (success) {
        printf("RP2350 hardware acceleration initialized successfully\n");
    } else {
        printf("RP2350 hardware acceleration initialization failed, falling back to standard mode\n");
    }
    
    return success;
}
#endif // RP2350

//--------------------------------------------------------------------+
// System Initialization Functions
//--------------------------------------------------------------------+

static bool initialize_system(void) {
    // Initialize stdio first for early debug output
    stdio_init_all();
    
    // Add extended startup delay for cold boot stability
    // Use the proper constant from defines.h instead of hardcoded value
    sleep_ms(COLD_BOOT_STABILIZATION_MS);
    
    LOG_INIT("PICO PIO KMBox - Starting initialization...");
    LOG_INIT("Platform: %s",
#ifdef RP2350
        "RP2350"
#else
        "RP2040"
#endif
    );
    LOG_INIT("USB Host Mode: %s",
#if CFG_TUH_MAX3421E
        "MAX3421E"
#else
        "PIO USB"
#endif
    );
    
#if PIO_USB_AVAILABLE
    // Set system clock to 120MHz (required for PIO USB - must be multiple of 12MHz)
    LOG_INIT("Setting system clock to %d kHz...", PIO_USB_SYSTEM_CLOCK_KHZ);
    if (!set_sys_clock_khz(PIO_USB_SYSTEM_CLOCK_KHZ, true)) {
        LOG_ERROR("CRITICAL: Failed to set system clock to %d kHz", PIO_USB_SYSTEM_CLOCK_KHZ);
        return false;
    }
    
    // Re-initialize stdio after clock change with proper delay
    sleep_ms(100);  // Allow clock to stabilize
    stdio_init_all();
    sleep_ms(100);  // Allow UART to stabilize
    LOG_INIT("System clock set successfully to %d kHz", PIO_USB_SYSTEM_CLOCK_KHZ);
#endif
    
    // Configure UART for non-blocking operation
    uart_set_fifo_enabled(uart0, true);  // Enable FIFO for better performance
    
    // Initialize onboard LED for heartbeat
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);  // Start with LED off

    // Initialize LED control module (neopixel power OFF for now)
    neopixel_init();

    // Initialize USB HID module (USB host power OFF for now)
    usb_hid_init();
    
    // Initialize DMA manager
    dma_manager_init();
    
    // Initialize DMA for HID report processing
    usb_hid_dma_init();

    // Initialize watchdog system (but don't start it yet)
    watchdog_init();
    
    // Validate DMA channel assignments
    dma_manager_validate_channels();

    LOG_INIT("System initialization complete");
    return true;
}

static bool initialize_usb_device(void) {
    // Simple device initialization - let the working example pattern guide us
    LOG_INIT("USB Device: Initializing on controller %d (native USB)...", USB_DEVICE_PORT);
    
    const bool device_init_success = tud_init(USB_DEVICE_PORT);
    LOG_INIT("USB Device init: %s", device_init_success ? "SUCCESS" : "FAILED");
    
    if (device_init_success) {
        usb_device_mark_initialized();
        LOG_INIT("USB Device: Initialization complete");
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
    // Button pressed (reduced logging)
}

static void handle_button_held(system_state_t* state, uint32_t current_time) {
    if (is_time_elapsed(current_time, state->last_button_press_time, BUTTON_HOLD_TRIGGER_MS)) {
        LOG_INIT("Button held - triggering USB reset");
        usb_stacks_reset();
        state->usb_reset_cooldown = true;
        state->usb_reset_cooldown_start = current_time;
    }
}

static void handle_button_release(const system_state_t* state, uint32_t hold_duration) {
    (void)state; // Suppress unused parameter warning
    if (hold_duration < BUTTON_HOLD_TRIGGER_MS) {
        // Button released (reduced logging)
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

static void report_hid_statistics(uint32_t current_time, uint32_t* stats_timer) {
    if (!is_time_elapsed(current_time, *stats_timer, STATS_INTERVAL_MS)) {
        return;
    }

    *stats_timer = current_time;
    
    // Stats reporting removed
    printf("=== HID Status ===\n");
    printf("Mouse connected: %s\n", is_mouse_connected() ? "YES" : "NO");
    printf("Keyboard connected: %s\n", is_keyboard_connected() ? "YES" : "NO");
    printf("=================\n");
}

static void report_watchdog_status(uint32_t current_time, uint32_t* watchdog_status_timer) {
    if (!is_time_elapsed(current_time, *watchdog_status_timer, WATCHDOG_STATUS_INTERVAL_MS)) {
        return;
    }

    *watchdog_status_timer = current_time;
    
    #if ENABLE_WATCHDOG_REPORTING
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
    #endif
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
    
    // Initialize main loop state for heartbeat
    main_loop_state_t loop_state = {0};
    loop_state.last_heartbeat_time = to_ms_since_boot(get_absolute_time());
    loop_state.heartbeat_state = false;

    while (true) {
        // TinyUSB device task - highest priority
        tud_task();
        hid_device_task();
        
        // Process DMA-queued HID reports - high priority
        process_queued_reports();
        
        const uint32_t current_time = to_ms_since_boot(get_absolute_time());
        
        // Watchdog tasks - controlled frequency
        if (system_state_should_run_task(state, current_time,
                                         state->last_watchdog_time,
                                         WATCHDOG_TASK_INTERVAL_MS)) {
            watchdog_task();
            watchdog_core0_heartbeat();
            state->last_watchdog_time = current_time;
        }
        
        // LED and visual tasks
        if (system_state_should_run_task(state, current_time,
                                        state->last_visual_time,
                                        VISUAL_TASK_INTERVAL_MS)) {
            led_blinking_task();
            neopixel_status_task();
            state->last_visual_time = current_time;
        }
        
        // USB stack error monitoring - DISABLED to prevent endpoint conflicts
        // The error checking was causing automatic resets that conflict with dual-mode operation
        if (system_state_should_run_task(state, current_time,
                                        state->last_error_check_time,
                                        ERROR_CHECK_INTERVAL_MS)) {
            // usb_stack_error_check(); // Disabled to prevent endpoint conflicts
            state->last_error_check_time = current_time;
        }
        
        // Button input processing
        if (system_state_should_run_task(state, current_time,
                                        state->last_button_time,
                                        BUTTON_DEBOUNCE_MS)) {
            process_button_input(state, current_time);
            state->last_button_time = current_time;
        }
        
        // Periodic reporting
        report_hid_statistics(current_time, &state->stats_timer);
        report_watchdog_status(current_time, &state->watchdog_status_timer);
        
        // Onboard LED heartbeat
        if (is_time_elapsed(current_time, loop_state.last_heartbeat_time, LED_HEARTBEAT_INTERVAL_MS)) {
            loop_state.heartbeat_state = !loop_state.heartbeat_state;
            gpio_put(PICO_DEFAULT_LED_PIN, loop_state.heartbeat_state);
            loop_state.last_heartbeat_time = current_time;
        }
    }
}

//--------------------------------------------------------------------+
// Main Function
//--------------------------------------------------------------------+

int main(void) {
    // CRITICAL: Initialize LED FIRST for early heartbeat indication
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);  // Turn on LED immediately for boot indication

    // Start heartbeat LED pattern early (simple toggle every 250ms)
    absolute_time_t last_led_toggle = get_absolute_time();
    bool led_state = true;
    
    // CRITICAL: Basic GPIO setup FIRST, before any delays
#if !defined(RP2350) && CFG_TUH_RPI_PIO_USB
    // USB 5V power pin initialization only for RP2040 boards with PIO USB
    gpio_init(PIN_USB_5V);
    gpio_set_dir(PIN_USB_5V, GPIO_OUT);
    gpio_put(PIN_USB_5V, 0);  // Keep USB power OFF during entire boot
#elif CFG_TUH_MAX3421E
    // For MAX3421E, ensure we don't enable any power yet
    // VBUS will be controlled via MAX3421E GPIO0 later
#endif
    
    // Initialize NeoPixel power pin early but keep it OFF
    gpio_init(NEOPIXEL_POWER);
    gpio_set_dir(NEOPIXEL_POWER, GPIO_OUT);
    gpio_put(NEOPIXEL_POWER, 0);  // Keep NeoPixel power OFF initially
    
    // EXTENDED cold boot stabilization with heartbeat
    // This is critical for MAX3421E stability
    uint32_t stabilization_time = COLD_BOOT_STABILIZATION_MS + 1000; // Add extra 1s for MAX3421E
    uint32_t elapsed = 0;
    while (elapsed < stabilization_time) {
        // Toggle LED every 250ms for heartbeat
        if (absolute_time_diff_us(last_led_toggle, get_absolute_time()) > 250000) {
            led_state = !led_state;
            gpio_put(PICO_DEFAULT_LED_PIN, led_state);
            last_led_toggle = get_absolute_time();
        }
        sleep_ms(50);
        elapsed += 50;
    }
    
    // Initialize stdio early for debug output
    stdio_init_all();
    sleep_ms(100); // Let UART stabilize
    
    printf("\n\n=== PIOKMBox Cold Boot Sequence ===\n");
    printf("LED heartbeat started\n");
    
#ifdef RP2350
    printf("RP2350 detected - configuring for hardware acceleration\n");
#else
    printf("RP2040 detected - using standard configuration\n");
#endif

#if CFG_TUH_MAX3421E
    printf("MAX3421E USB host controller selected\n");
    printf("Extended stabilization enabled for MAX3421E\n");
#endif
    
    // Set system clock with proper stabilization
    printf("Setting system clock to 120MHz...\n");
    if (!set_sys_clock_khz(120000, true)) {
        // Clock setting failed - try to continue with default clock
        printf("WARNING: Failed to set 120MHz clock, continuing with default\n");
    }
    
    // CRITICAL: Extended delay after clock change for cold boot
    sleep_ms(200);  // Let clock fully stabilize
    
    // Re-initialize stdio after clock change
    stdio_init_all();
    
    // Additional delay for UART stabilization after clock change
    sleep_ms(100);
    
    // Now we can use LOG macros safely
    
    LOG_INIT("=== PIOKMBox Starting (Cold Boot Enhanced) ===");
#ifdef RP2350
    LOG_INIT("RP2350 Hardware Acceleration Enabled");
#endif
#ifndef RP2350
    LOG_INIT("USB power held LOW during boot for stability");
#else
    LOG_INIT("RP2350 detected - using plain USB (no 5V pin control needed)");
#endif
    
    // Initialize system components with more conservative timing
    if (!initialize_system()) {
        LOG_ERROR("CRITICAL: System initialization failed");
        return -1;
    }
    
    // Enable NeoPixel power early for visual feedback
    LOG_INIT("Enabling NeoPixel power for boot status indication...");
    gpio_put(NEOPIXEL_POWER, 1);
    sleep_ms(100);  // Allow power to stabilize
    neopixel_enable_power();  // Initialize the PIO for NeoPixel
    
    // Additional hardware-specific delay for problematic hardware revisions
    #if REQUIRES_EXTENDED_BOOT_DELAY
    LOG_INIT("Hardware revision %d requires extended boot delay...", HARDWARE_REVISION);
    sleep_ms(USB_POWER_STABILIZATION_MS);
    #else
    // Even "good" hardware needs some delay for cold boot
    sleep_ms(1000);
    #endif
    
    // Initialize USB device stack BEFORE enabling host power
    LOG_INIT("Initializing USB device stack on core0...");
    if (!initialize_usb_device()) {
        LOG_ERROR("CRITICAL: USB Device initialization failed");
        return -1;
    }
    
    // Let device stack fully initialize
    sleep_ms(500);
    
    // Extended stabilization before enabling USB host power
    LOG_INIT("Extended stabilization before USB host power enable...");
    
#if CFG_TUH_ENABLED && CFG_TUH_MAX3421E
    // For MAX3421E, add extra delay and don't enable power yet
    LOG_INIT("MAX3421E: Adding extra stabilization time (3 seconds)...");
    for (int i = 0; i < 30; i++) {
        // Continue heartbeat during delay
        if (i % 5 == 0) {
            led_state = !led_state;
            gpio_put(PICO_DEFAULT_LED_PIN, led_state);
        }
        sleep_ms(100);
    }
    LOG_INIT("MAX3421E: VBUS will be enabled during controller initialization");
#elif !defined(RP2350) && CFG_TUH_RPI_PIO_USB
    // For RP2040 PIO USB, we need to enable power via GPIO
    LOG_INIT("RP2040 PIO USB: Enabling 5V power on GPIO %d", PIN_USB_5V);
    usb_host_enable_power();
#else
    // RP2350 with PIO USB doesn't need 5V control
    LOG_INIT("RP2350 PIO USB: No 5V power control needed");
#endif
    
#ifdef RP2350
    // Initialize hardware acceleration components before core1 launch
    LOG_INIT("Preparing RP2350 hardware acceleration components...");
    // Note: The actual hardware acceleration initialization happens in core1_main
#endif
    
    // CRITICAL: Extended delay after power enable for cold boot
    sleep_ms(1000);  // Much longer for cold boot reliability
    
    // Launch core1 with additional delay after power stabilization
    LOG_INIT("Launching core1 for USB host...");
    multicore_reset_core1();
    multicore_launch_core1(core1_main);
    
    // Give core1 time to initialize before proceeding
    sleep_ms(500);
    
    // Initialize remaining systems
    LOG_INIT("Starting watchdog and final systems...");
    watchdog_init();
    watchdog_start();
    
    // NeoPixel is already enabled, just update status
    LOG_INIT("Updating NeoPixel status...");
    
    LOG_INIT("=== PIOKMBox Ready (Cold Boot Complete) ===");
#ifdef RP2350
    LOG_INIT("RP2350 Hardware Acceleration Status: %s", hw_accel_enabled ? "ACTIVE" : "INACTIVE");
#endif
    
    // Enter main application loop
    main_application_loop();
    
    return 0;
}
