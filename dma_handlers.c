/*
 * Generic DMA Handlers for non-RP2350 platforms
 *
 * This file implements the DMA interrupt handlers for keyboard and mouse
 * data transfers on platforms that don't have the RP2350 hardware acceleration.
 */

#include "defines.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "usb_hid_types.h"
#include "dma_manager.h"
#include "tusb.h"
#include "class/hid/hid.h"

// External buffer references from usb_hid_reports.c
extern hid_keyboard_report_t kbd_buffer[];
extern hid_mouse_report_t mouse_buffer[];

/**
 * @brief Process a keyboard report from DMA buffer
 *
 * @param buffer Pointer to the keyboard data buffer
 * @return true if processing was successful, false otherwise
 */
static bool process_keyboard_dma_report(uint8_t* buffer) {
    if (buffer == NULL) {
        return false;
    }
    
    hid_keyboard_report_t* report = (hid_keyboard_report_t*)buffer;
    
    // Forward the report to the USB device stack
    return tud_hid_report(REPORT_ID_KEYBOARD, report, sizeof(hid_keyboard_report_t));
}

/**
 * @brief Process a mouse report from DMA buffer
 *
 * @param buffer Pointer to the mouse data buffer
 * @return true if processing was successful, false otherwise
 */
static bool process_mouse_dma_report(uint8_t* buffer) {
    if (buffer == NULL) {
        return false;
    }
    
    hid_mouse_report_t* report = (hid_mouse_report_t*)buffer;
    
    // Validate buttons (keep only first 3 bits for L/R/M buttons)
    uint8_t valid_buttons = report->buttons & 0x07;
    
    // Forward the report to the USB device stack
    return tud_hid_mouse_report(REPORT_ID_MOUSE, valid_buttons,
                              report->x, report->y, report->wheel, 0);
}

/**
 * @brief DMA interrupt handler for keyboard HID data
 *
 * This function handles DMA completion interrupts for keyboard data transfers.
 * It is called automatically by the hardware when a keyboard DMA transfer completes.
 */
void dma_kbd_irq_handler(void) {
    // Check if the keyboard DMA channel triggered the interrupt
    if (dma_channel_get_irq0_status(DMA_CHANNEL_KEYBOARD)) {
        // Acknowledge the interrupt
        dma_channel_acknowledge_irq0(DMA_CHANNEL_KEYBOARD);
        
        // Process the keyboard data that was transferred via DMA
        process_keyboard_dma_report((uint8_t*)kbd_buffer);
    }
}

/**
 * @brief DMA interrupt handler for mouse HID data
 *
 * This function handles DMA completion interrupts for mouse data transfers.
 * It is called automatically by the hardware when a mouse DMA transfer completes.
 */
void dma_mouse_irq_handler(void) {
    // Check if the mouse DMA channel triggered the interrupt
    if (dma_channel_get_irq0_status(DMA_CHANNEL_MOUSE)) {
        // Acknowledge the interrupt
        dma_channel_acknowledge_irq0(DMA_CHANNEL_MOUSE);
        
        // Process the mouse data that was transferred via DMA
        process_mouse_dma_report((uint8_t*)mouse_buffer);
    }
}