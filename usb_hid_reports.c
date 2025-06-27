/*
 * Hurricane PIOKMbox Firmware
 */

#include "usb_hid_reports.h"
#include "defines.h"
#include "led_control.h"
#include <stdio.h>

#ifdef RP2350
#include "rp2350_hw_accel.h"
#include "rp2350_dma_handler.h"
#endif

// No external stats declarations needed

// Forward declarations for static functions
static bool process_keyboard_report_internal(const hid_keyboard_report_t* report);
static bool process_mouse_report_internal(const hid_mouse_report_t* report);
static void dequeue_and_process_mouse_report(void);

#ifdef RP2350
// RP2350 hardware-accelerated implementations
extern bool hw_accel_process_keyboard_report(const hid_keyboard_report_t* report);
extern bool hw_accel_process_mouse_report(const hid_mouse_report_t* report);
extern bool hw_accel_is_enabled(void);
#endif

// Word-aligned circular buffers for DMA transfers
__attribute__((aligned(4))) hid_keyboard_report_t kbd_buffer[KBD_BUFFER_SIZE];
__attribute__((aligned(4))) hid_mouse_report_t mouse_buffer[MOUSE_BUFFER_SIZE];

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
/**
 * @brief Initialize a circular buffer structure
 *
 * @param buffer Pointer to the circular buffer structure
 * @param data Pointer to the data buffer
 * @param size Size of the buffer (must be power of 2)
 */
static void init_circular_buffer(dma_circular_buffer_t* buffer, void* data, size_t size) {
    if (buffer == NULL || data == NULL) {
        return;
    }
    
    buffer->read_idx = 0;
    buffer->write_idx = 0;
    buffer->size = size;
    buffer->mask = size - 1;
    buffer->buffer = data;
}

/**
 * @brief Configure a DMA channel for HID report processing
 *
 * @param channel DMA channel number
 * @param report_size Size of the report in bytes
 * @return true if configuration was successful, false otherwise
 */
static bool configure_dma_channel(int channel, size_t report_size) {
    dma_channel_config config = dma_channel_get_default_config(channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    channel_config_set_dreq(&config, DREQ_FORCE);
    
    dma_channel_configure(
        channel,
        &config,
        NULL,                    // Dest address set during transfer
        NULL,                    // Source address set during transfer
        report_size / 4,         // Transfer size in words
        false                    // Don't start immediately
    );
    
    // Enable DMA interrupts for this channel
    dma_channel_set_irq0_enabled(channel, true);
    
    return true;
}

/**
 * @brief Initialize DMA for HID report processing
 */
void init_hid_dma(void) {
    // Initialize circular buffer structures
    init_circular_buffer(&kbd_circular_buffer, kbd_buffer, KBD_BUFFER_SIZE);
    init_circular_buffer(&mouse_circular_buffer, mouse_buffer, MOUSE_BUFFER_SIZE);

    // Request DMA channels from the manager
    if (!dma_manager_request_channel(DMA_CHANNEL_KEYBOARD, "HID Keyboard")) {
        LOG_ERROR("Failed to request keyboard DMA channel");
        return;
    }
    kbd_dma_channel = DMA_CHANNEL_KEYBOARD;
    
    if (!dma_manager_request_channel(DMA_CHANNEL_MOUSE, "HID Mouse")) {
        LOG_ERROR("Failed to request mouse DMA channel");
        dma_manager_release_channel(DMA_CHANNEL_KEYBOARD);
        return;
    }
    mouse_dma_channel = DMA_CHANNEL_MOUSE;

    // Get spinlocks for thread safety
    kbd_spinlock = spin_lock_init(spin_lock_claim_unused(true));
    mouse_spinlock = spin_lock_init(spin_lock_claim_unused(true));

    // Configure DMA channels
    configure_dma_channel(kbd_dma_channel, sizeof(hid_keyboard_report_t));
    configure_dma_channel(mouse_dma_channel, sizeof(hid_mouse_report_t));
    
    // Set up DMA interrupt handlers
#ifdef RP2350
    // Use RP2350-specific DMA handler
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_priority(DMA_IRQ_0, DMA_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);
    
    irq_set_exclusive_handler(DMA_IRQ_1, dma_handler);
    irq_set_priority(DMA_IRQ_1, DMA_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);
#else
    // Use generic DMA handlers for non-RP2350 platforms
    extern void dma_kbd_irq_handler(void);
    extern void dma_mouse_irq_handler(void);
    
    irq_set_exclusive_handler(DMA_IRQ_0, dma_kbd_irq_handler);
    irq_set_priority(DMA_IRQ_0, DMA_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);
    
    irq_set_exclusive_handler(DMA_IRQ_1, dma_mouse_irq_handler);
    irq_set_priority(DMA_IRQ_1, DMA_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);
#endif
    
    LOG_INIT("DMA HID report processing initialized");
}

// Process keyboard report - queue to circular buffer
/**
 * @brief Process a keyboard HID report
 *
 * This function processes a keyboard HID report, using hardware acceleration
 * if available, or falling back to software processing.
 *
 * @param report Pointer to the keyboard HID report
 */
void process_kbd_report(const hid_keyboard_report_t* report)
{
    if (report == NULL) {
        return; // Fast fail without printf for performance
    }
    
    // Visual feedback (throttled for performance)
    static uint32_t activity_counter = 0;
    if (++activity_counter % KEYBOARD_ACTIVITY_THROTTLE == 0) {
        neopixel_trigger_keyboard_activity();
    }
    
    // Process the report using the most efficient method available
#ifdef RP2350
    // Try hardware acceleration first
    if (hw_accel_is_enabled() && hw_accel_process_keyboard_report(report)) {
        return; // Hardware acceleration successful
    }
#endif

    // Fall back to software processing
    process_keyboard_report_internal(report);
}

// Process mouse report - queue to circular buffer
/**
 * @brief Process a mouse HID report
 *
 * This function processes a mouse HID report, using hardware acceleration
 * if available, or falling back to software processing.
 *
 * @param report Pointer to the mouse HID report
 */
void process_mouse_report(const hid_mouse_report_t* report)
{
    if (report == NULL) {
        return; // Fast fail without printf for performance
    }
    
    // Visual feedback (throttled for performance)
    static uint32_t activity_counter = 0;
    if (++activity_counter % MOUSE_ACTIVITY_THROTTLE == 0) {
        neopixel_trigger_mouse_activity();
    }
    
    // Process the report using the most efficient method available
#ifdef RP2350
    // Try hardware acceleration first
    if (hw_accel_is_enabled() && hw_accel_process_mouse_report(report)) {
        // Hardware acceleration successful
    } else {
        // Software fallback
        process_mouse_report_internal(report);
    }
#else
    // Standard processing
    process_mouse_report_internal(report);
#endif
    
    // Process any queued reports
    if (!is_mouse_buffer_empty()) {
        dequeue_and_process_mouse_report();
    }
}

/**
 * @brief Check if the mouse circular buffer is empty
 *
 * @return true if the buffer is empty, false otherwise
 */
bool is_mouse_buffer_empty(void) {
    return mouse_circular_buffer.read_idx == mouse_circular_buffer.write_idx;
}

/**
 * @brief Check if the keyboard circular buffer is empty
 *
 * @return true if the buffer is empty, false otherwise
 */
bool is_kbd_buffer_empty(void) {
    return kbd_circular_buffer.read_idx == kbd_circular_buffer.write_idx;
}

/**
 * @brief Process all queued reports in the circular buffers
 *
 * This function processes all pending reports in both the keyboard and mouse
 * circular buffers. It should be called periodically to ensure reports are
 * processed in a timely manner.
 */
/**
 * @brief Process all queued reports in the circular buffers
 *
 * This function processes all pending reports in both the keyboard and mouse
 * circular buffers. It should be called periodically to ensure reports are
 * processed in a timely manner.
 */
void process_queued_reports(void) {
    // Process keyboard reports
    while (!is_kbd_buffer_empty()) {
        // Get the next report from the circular buffer
        uint32_t read_idx = kbd_circular_buffer.read_idx;
        hid_keyboard_report_t* report = &kbd_buffer[read_idx];
        
        // Process the report
        process_keyboard_report_internal(report);
        
        // Update read index
        kbd_circular_buffer.read_idx = (read_idx + 1) & kbd_circular_buffer.mask;
    }
    
    // Process mouse reports
    while (!is_mouse_buffer_empty()) {
        dequeue_and_process_mouse_report();
    }
}

/**
 * @brief Dequeue a mouse report from the circular buffer and process it
 *
 * This function removes the oldest mouse report from the circular buffer
 * and processes it.
 */
static void dequeue_and_process_mouse_report(void) {
    if (is_mouse_buffer_empty()) {
        return; // Buffer is empty, nothing to process
    }
    
    // Get the next report from the circular buffer
    uint32_t read_idx = mouse_circular_buffer.read_idx;
    hid_mouse_report_t* report = &mouse_buffer[read_idx];
    
    // Process the report
    process_mouse_report_internal(report);
    
    // Update read index
    mouse_circular_buffer.read_idx = (read_idx + 1) & mouse_circular_buffer.mask;
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

/**
 * @brief Process a keyboard report using software (non-accelerated) method
 *
 * @param report Pointer to the keyboard HID report
 * @return true if processing was successful, false otherwise
 */
static bool process_keyboard_report_internal(const hid_keyboard_report_t* report)
{
    if (report == NULL) {
        return false;
    }
    
    // Fast path: skip ready check for maximum performance
    // TinyUSB will handle the queuing internally
    return tud_hid_report(REPORT_ID_KEYBOARD, report, sizeof(hid_keyboard_report_t));
}

/**
 * @brief Process a mouse report using software (non-accelerated) method
 *
 * @param report Pointer to the mouse HID report
 * @return true if processing was successful, false otherwise
 */
static bool process_mouse_report_internal(const hid_mouse_report_t* report)
{
    if (report == NULL) {
        return false;
    }
    
    // Validate buttons - keep only first 3 bits (L/R/M buttons)
    uint8_t valid_buttons = report->buttons & 0x07;
    
    // Fast path: skip ready check for maximum performance
    return tud_hid_mouse_report(REPORT_ID_MOUSE, valid_buttons,
                               report->x, report->y, report->wheel, 0);
}