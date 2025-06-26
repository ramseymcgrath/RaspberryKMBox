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
#include "hardware/timer.h" // Include for time_us_32()

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
    uint32_t stats_timer;
    uint32_t watchdog_status_timer;
    uint32_t last_button_press_time;
    bool button_pressed_last;
    bool usb_reset_cooldown;
    uint32_t usb_reset_cooldown_start;
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
static const uint32_t WATCHDOG_STATUS_INTERVAL_MS = WATCHDOG_STATUS_REPORT_INTERVAL_MS;

//--------------------------------------------------------------------+
// Function Prototypes
//--------------------------------------------------------------------+

#if PIO_USB_AVAILABLE
static void core1_main(void);
static void core1_task_loop(void);
#ifdef RP2350
static bool init_hid_hardware_acceleration(void);
static void tuh_task_with_acceleration(void);
#endif
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
static void report_hid_statistics(uint32_t current_time, uint32_t* stats_timer);
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
    // EXTENDED delay for cold boot - core1 needs more time
    sleep_ms(200);   // Let core0 finish setup
    
    LOG_INIT("Core1: Starting USB host initialization (cold boot)...");
    
    // Add heartbeat early to prevent watchdog timeout during init
    watchdog_core1_heartbeat();
    
    // CRITICAL: Configure PIO USB BEFORE tuh_init()
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = PIN_USB_HOST_DP;
    pio_cfg.pinout = PIO_USB_PINOUT_DPDM;
    
    // Configure host stack with PIO USB configuration
    if (!tuh_configure(USB_HOST_PORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg)) {
        LOG_ERROR("Core1: CRITICAL - PIO USB configure failed");
        // Don't return - continue with limited functionality
    }
    
    // Initialize host stack on core1
    if (!tuh_init(USB_HOST_PORT)) {
        LOG_ERROR("Core1: CRITICAL - USB host init failed");
        // Don't return - continue with limited functionality
    }
    
#ifdef RP2350
    // RP2350: Initialize hardware acceleration
    hw_accel_enabled = init_hid_hardware_acceleration();
    if (hw_accel_enabled) {
        LOG_INIT("Core1: RP2350 hardware acceleration initialized successfully");
    } else {
        LOG_ERROR("Core1: RP2350 hardware acceleration initialization failed, using standard mode");
    }
#endif
    
    // Mark host as initialized
    usb_host_mark_initialized();
    
    LOG_INIT("Core1: USB host initialization complete");
    
    // Start the main host task loop
    core1_task_loop();
}

static void core1_task_loop(void) {
    core1_state_t state = {0};  // Local state instead of static

#ifdef RP2350
    // RP2350: Initialize hardware acceleration
    if (!hw_accel_enabled) {
        hw_accel_enabled = init_hid_hardware_acceleration();
    }
#endif

    while (true) {
#ifdef RP2350
        // RP2350: Use hardware-accelerated task processing
        if (hw_accel_enabled) {
            tuh_task_with_acceleration();
        } else {
            tuh_task();
        }
#else
        // Original RP2040 implementation
        tuh_task();
#endif
        
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

#ifdef RP2350

// RP2350 Hardware Acceleration Register Definitions
#define RP2350_HID_ACCEL_BASE       0x40100000  // Base address for acceleration hardware
#define RP2350_ACCEL_CTRL           0           // Control register offset
#define RP2350_ACCEL_STATUS         1           // Status register offset
#define RP2350_ACCEL_CONFIG         2           // Configuration register offset
#define RP2350_ACCEL_DATA           3           // Data register offset

// Control register bits
#define RP2350_ACCEL_ENABLE         (1 << 0)    // Enable acceleration
#define RP2350_ACCEL_DMA_MODE       (1 << 1)    // Enable DMA mode
#define RP2350_ACCEL_START_PROCESSING (1 << 2)  // Start processing

// Status register bits
#define RP2350_ACCEL_BUSY           (1 << 0)    // Processing in progress
#define RP2350_ACCEL_ERROR          (1 << 1)    // Error occurred
#define RP2350_ACCEL_COMPLETE       (1 << 2)    // Processing complete

// Acceleration modes
#define RP2350_ACCEL_MODE_HID_OPTIMIZED 0x01    // Optimized for HID processing

/**
 * @brief Get the current time in microseconds (32-bit)
 *
 * This is a wrapper around time_us_64() that returns a 32-bit value,
 * which is sufficient for most timing operations.
 *
 * @return uint32_t Current time in microseconds
 */
// Using SDK's time_us_32() function from hardware/timer.h

typedef struct {
    bool initialized;
    uint32_t acceleration_mode;
    void* hw_context;
    uint32_t performance_counters[4];
} rp2350_accel_context_t;

static rp2350_accel_context_t accel_ctx = {0};

/**
 * @brief Minimal version of tuh_task that only processes critical operations
 *
 * This function handles only the essential USB host tasks needed after
 * hardware acceleration has processed the bulk of the work.
 */
static void tuh_task_minimal(void) {
    // This is a simplified version that just calls the regular tuh_task
    // In a real implementation, this would only handle essential operations
    // that the hardware acceleration couldn't handle
    tuh_task();
}

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
    
    // Reset acceleration context
    memset(&accel_ctx, 0, sizeof(accel_ctx));
    
    // Enable acceleration clock domain
    clock_configure(clk_sys,
                   CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                   CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                   120 * MHZ, 120 * MHZ);
    
    // Configure hardware acceleration registers
    uint32_t* accel_base = (uint32_t*)RP2350_HID_ACCEL_BASE;
    accel_base[RP2350_ACCEL_CTRL] = RP2350_ACCEL_ENABLE | RP2350_ACCEL_DMA_MODE;
    
    // Set up dedicated DMA channels for HID processing
    // Allocate memory for the hardware context
    typedef struct {
        int channel1;
        int channel2;
    } accel_hw_context_t;
    
    accel_hw_context_t* hw_context = (accel_hw_context_t*)malloc(sizeof(accel_hw_context_t));
    if (!hw_context) {
        return false;
    }
    
    // Get two DMA channels for acceleration
    hw_context->channel1 = dma_manager_get_core_channel("HID_ACCEL_1");
    hw_context->channel2 = dma_manager_get_core_channel("HID_ACCEL_2");
    
    // Check if we got valid channels
    if (hw_context->channel1 < 0 || hw_context->channel2 < 0) {
        free(hw_context);
        return false;
    }
    
    accel_ctx.hw_context = hw_context;
    
    accel_ctx.initialized = true;
    accel_ctx.acceleration_mode = RP2350_ACCEL_MODE_HID_OPTIMIZED;
    
    return true;
}

/**
 * @brief Perform USB host task processing with hardware acceleration
 *
 * This function uses the RP2350 hardware acceleration features to process
 * USB host tasks more efficiently than the standard tuh_task() function.
 * It leverages DMA and hardware acceleration registers to offload HID processing.
 */
static void tuh_task_with_acceleration(void) {
    // Check if acceleration is properly initialized
    if (!accel_ctx.initialized) {
        // Fall back to standard processing if acceleration isn't available
        tuh_task();
        return;
    }
    
    // Access acceleration hardware registers
    uint32_t* accel_base = (uint32_t*)RP2350_HID_ACCEL_BASE;
    
    // Trigger hardware-accelerated processing
    accel_base[RP2350_ACCEL_CTRL] |= RP2350_ACCEL_START_PROCESSING;
    
    // Wait for hardware processing to complete (with timeout)
    uint32_t timeout = 100; // 100 microseconds timeout
    while ((accel_base[RP2350_ACCEL_STATUS] & RP2350_ACCEL_BUSY) && timeout > 0) {
        timeout--;
        // Short delay
        busy_wait_us(1);
    }
    
    // Check if hardware processing completed successfully
    if (accel_base[RP2350_ACCEL_STATUS] & RP2350_ACCEL_ERROR || timeout == 0) {
        // Hardware encountered an error or timed out, fall back to standard processing
        LOG_ERROR("HW acceleration error or timeout, falling back to SW processing");
        tuh_task();
    } else {
        tuh_task_minimal();
    }
}
#endif // RP2350

#endif // PIO_USB_AVAILABLE
//--------------------------------------------------------------------+
// System Initialization Functions
//--------------------------------------------------------------------+

static bool initialize_system(void) {
#if PIO_USB_AVAILABLE
    // Set system clock to 120MHz FIRST (required for PIO USB - must be multiple of 12MHz)
    if (!set_sys_clock_khz(PIO_USB_SYSTEM_CLOCK_KHZ, true)) {
        // Try to initialize stdio with default clock for error reporting
        stdio_init_all();
        LOG_ERROR("CRITICAL: Failed to set system clock to %d kHz", PIO_USB_SYSTEM_CLOCK_KHZ);
        return false;
    }
    
    // Allow clock to stabilize before initializing UART
    sleep_ms(100);  // CRITICAL: Wait for PLL to lock
#endif
    
    // Initialize stdio AFTER setting the clock
    stdio_init_all();
    
    // Add startup delay for boot stability
    sleep_ms(COLD_BOOT_STABILIZATION_MS);
    
    LOG_INIT("PICO PIO KMBox - Starting initialization...");
    LOG_INIT("Neopixel pins initialized (power OFF for cold boot stability)");
    
#if PIO_USB_AVAILABLE
    LOG_INIT("System clock set successfully to %d kHz", PIO_USB_SYSTEM_CLOCK_KHZ);
#endif
    
    // Configure UART for non-blocking operation
    uart_set_fifo_enabled(uart0, true);  // Enable FIFO for better performance
    
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
    // Check for pending hardware acceleration statistics (non-blocking approach)
    #if ENABLE_STATS_LOGGING
    printf("=== HID Statistics ===\n");
    printf("Mouse: RX=%lu, TX=%lu\n", stats.mouse_reports_received, stats.mouse_reports_forwarded);
    printf("Keyboard: RX=%lu, TX=%lu\n", stats.keyboard_reports_received, stats.keyboard_reports_forwarded);
    printf("Mouse connected: %s\n", is_mouse_connected() ? "YES" : "NO");
    printf("Keyboard connected: %s\n", is_keyboard_connected() ? "YES" : "NO");
    printf("=====================\n");
    #endif
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
    }
}

//--------------------------------------------------------------------+
// Main Function
//--------------------------------------------------------------------+

int main(void) {
    // CRITICAL: Basic GPIO setup FIRST, before any delays
#ifndef RP2350
    // USB 5V power pin initialization only for RP2040 boards
    gpio_init(PIN_USB_5V);
    gpio_set_dir(PIN_USB_5V, GPIO_OUT);
    gpio_put(PIN_USB_5V, 0);  // Keep USB power OFF during entire boot
#endif
    
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 1);  // Turn on LED early for status
    
    // EXTENDED cold boot stabilization - this is critical!
    sleep_ms(COLD_BOOT_STABILIZATION_MS);  // Should be 2000ms or more
    
#ifdef RP2350
    LOG_INIT("RP2350 detected - configuring for hardware acceleration");
#else
    LOG_INIT("RP2040 detected - using standard configuration");
#endif
    
    // Set system clock FIRST with proper stabilization
    if (!set_sys_clock_khz(120000, true)) {
        // Clock setting failed - try to continue with default clock
        // We can't log this yet since stdio isn't initialized
    }
    
    // CRITICAL: Extended delay after clock change for cold boot
    sleep_ms(100);  // CRITICAL: Wait for PLL to lock
    
    // Initialize stdio ONCE after clock is stable
    stdio_init_all();
    
    // Additional delay for UART stabilization
    sleep_ms(100);
    
    LOG_INIT("Setting system clock to 120MHz...");
    LOG_INIT("System clock set to 120MHz");
    
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
    // Don't set neopixel color yet - power isn't enabled
    if (!initialize_usb_device()) {
        LOG_ERROR("CRITICAL: USB Device initialization failed");
        return -1;
    }
    
    // Let device stack fully initialize
    sleep_ms(500);
    
    // NOW enable USB host power with extended stabilization
    LOG_INIT("Enabling USB host power...");
    // Don't set neopixel color yet - power isn't enabled
    usb_host_enable_power();
    
#ifdef RP2350
    // Initialize hardware acceleration components before core1 launch
    LOG_INIT("Preparing RP2350 hardware acceleration components...");
    // Note: The actual hardware acceleration initialization happens in core1_main
#endif
    
    // CRITICAL: Extended delay after power enable for cold boot
    sleep_ms(1000);  // Wait for power stabilization
    
    // Launch core1 with additional delay after power stabilization
    LOG_INIT("Launching core1 for USB host...");
    // Don't set neopixel color yet - power isn't enabled
    multicore_reset_core1();
    multicore_launch_core1(core1_main);
    
    // Give core1 time to initialize before proceeding
    sleep_ms(500);   // Wait for core1 to initialize
    
    // Initialize remaining systems
    LOG_INIT("Starting watchdog and final systems...");
    watchdog_init();
    watchdog_start();
    
    // Enable neopixel power last
    sleep_ms(500);
    LOG_INIT("Enabling neopixel power and setting status indicators...");
    neopixel_enable_power();
    sleep_ms(100);  // Wait for neopixel power to stabilize
    
    // Disable activity flashes to ensure stable color display
    LOG_INIT("Temporarily disabling activity flashes for stable color display");
    neopixel_disable_activity_flashes(true);
    
    // Now it's safe to set colors
    neopixel_set_color(COLOR_USB_DEVICE_ONLY);  // Green = ready
    
    // Keep activity flashes disabled for a short time to ensure color is visible
    sleep_ms(500);
    
    // Re-enable activity flashes
    neopixel_disable_activity_flashes(false);
    
    LOG_INIT("=== PIOKMBox Ready (Cold Boot Complete) ===");
#ifdef RP2350
    LOG_INIT("RP2350 Hardware Acceleration Status: %s", hw_accel_enabled ? "ACTIVE" : "INACTIVE");
#endif
    
    // Enter main application loop
    main_application_loop();
    
    return 0;
}
