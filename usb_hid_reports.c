/*
 * Hurricane PIOKMbox Firmware
 */

#include "usb_hid_reports.h"
#include "defines.h"
#include "led_control.h"
#include <stdio.h>
#include <string.h>
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "tusb.h"
#include "device/usbd.h"
#include "class/hid/hid_device.h"

// External declarations for variables defined in other modules
extern performance_stats_t stats;

// Forward declarations for static functions
static void process_keyboard_report_internal(const hid_keyboard_report_t* report);
static void process_mouse_report_internal(const hid_mouse_report_t* report);
static void dma_kbd_irq_handler(void);
static void dma_mouse_irq_handler(void);
static bool enqueue_kbd_report(const hid_keyboard_report_t* report);
static bool enqueue_mouse_report(const hid_mouse_report_t* report);
static bool dequeue_and_process_kbd_report(void);
static bool dequeue_and_process_mouse_report(void);

#if USE_HARDWARE_ACCELERATION
// RP2350 hardware-accelerated implementations
static void process_keyboard_report_internal_m33(const hid_keyboard_report_t* report);
static void process_mouse_report_internal_m33(const hid_mouse_report_t* report);
#endif

// Word-aligned circular buffers for DMA transfers
static __attribute__((aligned(4))) hid_keyboard_report_t kbd_buffer[KBD_BUFFER_SIZE];
static __attribute__((aligned(4))) hid_mouse_report_t mouse_buffer[MOUSE_BUFFER_SIZE];

// Dedicated destination buffers for DMA transfers - made accessible for forwarding
__attribute__((aligned(4))) hid_keyboard_report_t kbd_dest_buffer;
__attribute__((aligned(4))) hid_mouse_report_t mouse_dest_buffer;

// Circular buffer control structures
static dma_circular_buffer_t kbd_circular_buffer;
static dma_circular_buffer_t mouse_circular_buffer;

// DMA channel handles
static int kbd_dma_channel;
static int mouse_dma_channel;

// Spinlocks for thread-safe buffer access
static spin_lock_t *kbd_spinlock;
static spin_lock_t *mouse_spinlock;

// Include DMA manager
#include "dma_manager.h"

// Initialize DMA channels and circular buffers
void init_hid_dma(void) {
    // Initialize circular buffer structures
    kbd_circular_buffer.read_idx = 0;
    kbd_circular_buffer.write_idx = 0;
    kbd_circular_buffer.size = KBD_BUFFER_SIZE;
    kbd_circular_buffer.mask = KBD_BUFFER_SIZE - 1;
    kbd_circular_buffer.buffer = kbd_buffer;

    mouse_circular_buffer.read_idx = 0;
    mouse_circular_buffer.write_idx = 0;
    mouse_circular_buffer.size = MOUSE_BUFFER_SIZE;
    mouse_circular_buffer.mask = MOUSE_BUFFER_SIZE - 1;
    mouse_circular_buffer.buffer = mouse_buffer;

    // Request specific DMA channels from the DMA manager
    if (!dma_manager_request_channel(DMA_CHANNEL_KEYBOARD, "HID Keyboard")) {
        // Failed to get the keyboard DMA channel
        LOG_ERROR("Failed to request keyboard DMA channel");
        return;
    }
    kbd_dma_channel = DMA_CHANNEL_KEYBOARD;
    
    if (!dma_manager_request_channel(DMA_CHANNEL_MOUSE, "HID Mouse")) {
        // Failed to get the mouse DMA channel
        LOG_ERROR("Failed to request mouse DMA channel");
        // Release the keyboard channel we already claimed
        dma_manager_release_channel(DMA_CHANNEL_KEYBOARD);
        return;
    }
    mouse_dma_channel = DMA_CHANNEL_MOUSE;

    // Get spinlocks for thread safety
    kbd_spinlock = spin_lock_init(spin_lock_claim_unused(true));
    mouse_spinlock = spin_lock_init(spin_lock_claim_unused(true));

    // Configure keyboard DMA channel
    dma_channel_config kbd_config = dma_channel_get_default_config(kbd_dma_channel);
    channel_config_set_transfer_data_size(&kbd_config, DMA_SIZE_32);
    channel_config_set_read_increment(&kbd_config, true);
    channel_config_set_write_increment(&kbd_config, false);
    channel_config_set_dreq(&kbd_config, DREQ_FORCE);
    
    dma_channel_configure(
        kbd_dma_channel,
        &kbd_config,
        NULL,                              // Dest address set during transfer
        NULL,                              // Source address set during transfer
        sizeof(hid_keyboard_report_t) / 4, // Transfer size in words
        false                              // Don't start immediately
    );

    // Configure mouse DMA channel
    dma_channel_config mouse_config = dma_channel_get_default_config(mouse_dma_channel);
    channel_config_set_transfer_data_size(&mouse_config, DMA_SIZE_32);
    channel_config_set_read_increment(&mouse_config, true);
    channel_config_set_write_increment(&mouse_config, false);
    channel_config_set_dreq(&mouse_config, DREQ_FORCE);
    
    dma_channel_configure(
        mouse_dma_channel,
        &mouse_config,
        NULL,                             // Dest address set during transfer
        NULL,                             // Source address set during transfer
        sizeof(hid_mouse_report_t) / 4,   // Transfer size in words
        false                             // Don't start immediately
    );

    // Set up DMA interrupts
    dma_channel_set_irq0_enabled(kbd_dma_channel, true);
    dma_channel_set_irq0_enabled(mouse_dma_channel, true);
    
    irq_set_exclusive_handler(DMA_IRQ_0, dma_kbd_irq_handler);
    irq_set_priority(DMA_IRQ_0, DMA_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);
    
    irq_set_exclusive_handler(DMA_IRQ_1, dma_mouse_irq_handler);
    irq_set_priority(DMA_IRQ_1, DMA_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);
    
    LOG_INIT("DMA HID report processing initialized");
}

// Process keyboard report - queue to circular buffer
void process_kbd_report(const hid_keyboard_report_t* report)
{
    if (report == NULL) {
        return; // Fast fail without printf for performance
    }
    
    // Reduced activity flash frequency for better performance
    static uint32_t activity_counter = 0;
    if (++activity_counter % (KEYBOARD_ACTIVITY_THROTTLE * 10) == 0) {
        // Multiply throttle by 10 to reduce frequency of flashes
        neopixel_trigger_keyboard_activity();
    }
    
    // Queue the report to the circular buffer without stats tracking
    enqueue_kbd_report(report);
}

// Process mouse report - queue to circular buffer
void process_mouse_report(const hid_mouse_report_t* report)
{
    if (report == NULL) {
        return; // Fast fail without printf for performance
    }
    
    // Reduced activity flash frequency for better performance
    static uint32_t activity_counter = 0;
    if (++activity_counter % (MOUSE_ACTIVITY_THROTTLE * 10) == 0) {
        // Multiply throttle by 10 to reduce frequency of flashes
        neopixel_trigger_mouse_activity();
    }
    
    // Queue the report to the circular buffer without stats tracking
    enqueue_mouse_report(report);
}

// Process queued reports from main loop
void process_queued_reports(void) {
    // Process keyboard reports if available
    if (!is_kbd_buffer_empty()) {
        dequeue_and_process_kbd_report();
    }
    
    // Process mouse reports if available
    if (!is_mouse_buffer_empty()) {
        dequeue_and_process_mouse_report();
    }
}

// Check if keyboard buffer is empty
bool is_kbd_buffer_empty(void) {
    uint32_t save = spin_lock_blocking(kbd_spinlock);
    bool empty = (kbd_circular_buffer.read_idx == kbd_circular_buffer.write_idx);
    spin_unlock(kbd_spinlock, save);
    return empty;
}

// Check if mouse buffer is empty
bool is_mouse_buffer_empty(void) {
    uint32_t save = spin_lock_blocking(mouse_spinlock);
    bool empty = (mouse_circular_buffer.read_idx == mouse_circular_buffer.write_idx);
    spin_unlock(mouse_spinlock, save);
    return empty;
}

// Enqueue keyboard report to circular buffer
static bool enqueue_kbd_report(const hid_keyboard_report_t* report) {
    uint32_t save = spin_lock_blocking(kbd_spinlock);
    
    // Calculate next write index
    uint32_t next_write = (kbd_circular_buffer.write_idx + 1) & kbd_circular_buffer.mask;
    
    // Check if buffer is full
    if (next_write == kbd_circular_buffer.read_idx) {
        spin_unlock(kbd_spinlock, save);
        return false;
    }
    
    // Copy report to buffer
    memcpy(&((hid_keyboard_report_t*)kbd_circular_buffer.buffer)[kbd_circular_buffer.write_idx],
           report,
           sizeof(hid_keyboard_report_t));
    
    // Update write index
    kbd_circular_buffer.write_idx = next_write;
    
    spin_unlock(kbd_spinlock, save);
    
    // If DMA is not active, start it
    if (!dma_channel_is_busy(kbd_dma_channel)) {
        dequeue_and_process_kbd_report();
    }
    
    return true;
}

// Enqueue mouse report to circular buffer
static bool enqueue_mouse_report(const hid_mouse_report_t* report) {
    uint32_t save = spin_lock_blocking(mouse_spinlock);
    
    // Calculate next write index
    uint32_t next_write = (mouse_circular_buffer.write_idx + 1) & mouse_circular_buffer.mask;
    
    // Check if buffer is full
    if (next_write == mouse_circular_buffer.read_idx) {
        spin_unlock(mouse_spinlock, save);
        return false;
    }
    
    // Copy report to buffer with button validation
    hid_mouse_report_t validated_report = *report;
    validated_report.buttons &= 0x07; // Keep only first 3 bits (L/R/M buttons)
    
    memcpy(&((hid_mouse_report_t*)mouse_circular_buffer.buffer)[mouse_circular_buffer.write_idx],
           &validated_report,
           sizeof(hid_mouse_report_t));
    
    // Update write index
    mouse_circular_buffer.write_idx = next_write;
    
    spin_unlock(mouse_spinlock, save);
    
    // If DMA is not active, start it
    if (!dma_channel_is_busy(mouse_dma_channel)) {
        dequeue_and_process_mouse_report();
    }
    
    return true;
}

// Dequeue and process keyboard report using DMA
static bool dequeue_and_process_kbd_report(void) {
    uint32_t save = spin_lock_blocking(kbd_spinlock);
    
    // Check if buffer is empty
    if (kbd_circular_buffer.read_idx == kbd_circular_buffer.write_idx) {
        spin_unlock(kbd_spinlock, save);
        return false;
    }
    
    // Get report from buffer
    hid_keyboard_report_t* report = &((hid_keyboard_report_t*)kbd_circular_buffer.buffer)[kbd_circular_buffer.read_idx];
    
    // Set up DMA transfer
    dma_channel_set_read_addr(kbd_dma_channel, report, false);
    
    // Start DMA transfer
    dma_channel_set_trans_count(kbd_dma_channel, sizeof(hid_keyboard_report_t) / 4, false);
    dma_channel_set_write_addr(kbd_dma_channel, &kbd_dest_buffer, true); // Start transfer
    
    // Update read index
    kbd_circular_buffer.read_idx = (kbd_circular_buffer.read_idx + 1) & kbd_circular_buffer.mask;
    
    spin_unlock(kbd_spinlock, save);
    return true;
}

// Dequeue and process mouse report using DMA
static bool dequeue_and_process_mouse_report(void) {
    uint32_t save = spin_lock_blocking(mouse_spinlock);
    
    // Check if buffer is empty
    if (mouse_circular_buffer.read_idx == mouse_circular_buffer.write_idx) {
        spin_unlock(mouse_spinlock, save);
        return false;
    }
    
    // Get report from buffer
    hid_mouse_report_t* report = &((hid_mouse_report_t*)mouse_circular_buffer.buffer)[mouse_circular_buffer.read_idx];
    
    // Set up DMA transfer
    dma_channel_set_read_addr(mouse_dma_channel, report, false);
    
    // Start DMA transfer
    dma_channel_set_trans_count(mouse_dma_channel, sizeof(hid_mouse_report_t) / 4, false);
    dma_channel_set_write_addr(mouse_dma_channel, &mouse_dest_buffer, true); // Start transfer
    
    // Update read index
    mouse_circular_buffer.read_idx = (mouse_circular_buffer.read_idx + 1) & mouse_circular_buffer.mask;
    
    spin_unlock(mouse_spinlock, save);
    return true;
}

// DMA interrupt handler for keyboard reports
static void dma_kbd_irq_handler(void) {
    // Clear the interrupt
    dma_hw->ints0 = 1u << kbd_dma_channel;
    
    // Set a flag to indicate a new keyboard report is available for forwarding
    extern device_connection_state_t connection_state;
    // Direct access without lock since we have separate stacks
    connection_state.keyboard_report_ready = true;
    
    // Use the dedicated destination buffer that the DMA wrote to
#if USE_HARDWARE_ACCELERATION
    process_keyboard_report_internal_m33(&kbd_dest_buffer);
#else
    process_keyboard_report_internal(&kbd_dest_buffer);
#endif
    
    // Process next report if available
    if (!is_kbd_buffer_empty()) {
        dequeue_and_process_kbd_report();
    }
}

// DMA interrupt handler for mouse reports
static void dma_mouse_irq_handler(void) {
    // Clear the interrupt
    dma_hw->ints0 = 1u << mouse_dma_channel;
    
    // Set a flag to indicate a new mouse report is available for forwarding
    extern device_connection_state_t connection_state;
    // Direct access without lock since we have separate stacks
    connection_state.mouse_report_ready = true;
    
    // Use the dedicated destination buffer that the DMA wrote to
#if USE_HARDWARE_ACCELERATION
    process_mouse_report_internal_m33(&mouse_dest_buffer);
#else
    process_mouse_report_internal(&mouse_dest_buffer);
#endif
    
    // Process next report if available
    if (!is_mouse_buffer_empty()) {
        dequeue_and_process_mouse_report();
    }
}

bool find_key_in_report(const hid_keyboard_report_t* report, uint8_t keycode)
{
    if (report == NULL) {
        return false;
    }
    
    for (uint8_t i = 0; i < HID_KEYBOARD_KEYCODE_COUNT; i++) {
        if (report->keycode[i] == keycode) {
            return true;
        }
    }
    
    return false;
}

__attribute__((unused)) static void process_keyboard_report_internal(const hid_keyboard_report_t* report)
{
    if (report == NULL) {
        return;
    }
    
    // Fast path: skip ready check for maximum performance
    // TinyUSB will handle the queuing internally
    tud_hid_report(REPORT_ID_KEYBOARD, report, sizeof(hid_keyboard_report_t));
}

#if USE_HARDWARE_ACCELERATION
// RP2350 hardware-accelerated implementation for keyboard report processing
static void process_keyboard_report_internal_m33(const hid_keyboard_report_t* report)
{
    if (report == NULL) {
        return;
    }
    
    // Use optimized processing for RP2350
    // Instead of inline assembly, use direct function calls for better compatibility
    
    // Validate the report data
    uint8_t modifier = report->modifier & 0xFF;  // Ensure modifier is valid (8 bits only)
    
    // Create a validated report
    hid_keyboard_report_t validated_report = *report;
    validated_report.modifier = modifier;
    
    // Call the TinyUSB function directly
    tud_hid_report(REPORT_ID_KEYBOARD, &validated_report, sizeof(hid_keyboard_report_t));
}

// RP2350 hardware-accelerated implementation for mouse report processing
static void process_mouse_report_internal_m33(const hid_mouse_report_t* report)
{
    if (report == NULL) {
        return;
    }
    
    // Use optimized processing for RP2350
    // Instead of inline assembly, use direct function calls for better compatibility
    
    // Validate the buttons (keep only first 3 bits for L/R/M buttons)
    uint8_t valid_buttons = report->buttons & 0x07;
    
    // Call the TinyUSB function directly
    tud_hid_mouse_report(
        REPORT_ID_MOUSE,
        valid_buttons,
        report->x,
        report->y,
        report->wheel,
        0  // pan parameter set to 0
    );
}
#endif // USE_HARDWARE_ACCELERATION

__attribute__((unused)) static void process_mouse_report_internal(const hid_mouse_report_t* report)
{
    if (report == NULL) {
        return;
    }
    
    // Skip coordinate clamping for performance - trust the input device
    // Most modern mice send valid coordinates anyway
    
    // Fast button validation using bitwise AND
    uint8_t valid_buttons = report->buttons & 0x07; // Keep only first 3 bits (L/R/M buttons)
    
    // Fast path: skip ready check for maximum performance
    tud_hid_mouse_report(REPORT_ID_MOUSE, valid_buttons, report->x, report->y, report->wheel, 0);
}