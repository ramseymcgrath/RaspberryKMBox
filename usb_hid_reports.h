/*
 * Hurricane PIOKMbox Firmware
 *
 * USB HID Report Processing Header
 * This header defines the interface for processing USB HID reports
 * with DMA acceleration support.
 */

#ifndef USB_HID_REPORTS_H
#define USB_HID_REPORTS_H

#include <stdbool.h>
#include "defines.h"
#include "usb_hid_types.h"
#include "class/hid/hid.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

/**
 * @brief Circular buffer structure for DMA transfers
 *
 * This structure is word-aligned for optimal DMA performance.
 * The buffer size must be a power of 2 for efficient masking operations.
 */
typedef struct __attribute__((aligned(4))) {
    volatile uint32_t read_idx;     // Current read position
    volatile uint32_t write_idx;    // Current write position
    uint32_t size;                  // Total buffer size
    uint32_t mask;                  // Mask for wrapping (size - 1)
    void* buffer;                   // Pointer to the actual buffer
} dma_circular_buffer_t;

//--------------------------------------------------------------------+
// Function Declarations
//--------------------------------------------------------------------+

/**
 * @brief Process a keyboard HID report
 * @param report Pointer to the keyboard report structure
 */
void process_kbd_report(const hid_keyboard_report_t* report);

/**
 * @brief Process a mouse HID report
 * @param report Pointer to the mouse report structure
 */
void process_mouse_report(const hid_mouse_report_t* report);

/**
 * @brief Initialize DMA for HID report processing
 */
void init_hid_dma(void);

/**
 * @brief Process all queued reports in the circular buffers
 */
void process_queued_reports(void);

/**
 * @brief Check if the keyboard buffer is empty
 * @return true if empty, false otherwise
 */
bool is_kbd_buffer_empty(void);

/**
 * @brief Check if the mouse buffer is empty
 * @return true if empty, false otherwise
 */
bool is_mouse_buffer_empty(void);

/**
 * @brief Find a specific keycode in a keyboard report
 * @param report Pointer to the keyboard report
 * @param keycode The keycode to search for
 * @return true if found, false otherwise
 */
bool find_key_in_report(const hid_keyboard_report_t* report, uint8_t keycode);

#endif // USB_HID_REPORTS_H