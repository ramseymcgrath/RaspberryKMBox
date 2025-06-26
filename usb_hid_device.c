/*
 * Hurricane PIOKMbox Firmware
 */

#include "usb_hid_device.h"
#include "defines.h"
#include "led_control.h"
#include "pico/stdlib.h"
#include <stdio.h>

// External declarations for variables defined in other modules
extern device_connection_state_t connection_state;

// Device mode state
static bool caps_lock_state = false;

void hid_device_task(void)
{
    static uint32_t start_ms = 0;
    uint32_t current_ms = to_ms_since_boot(get_absolute_time());
    
    if (current_ms - start_ms < HID_DEVICE_TASK_INTERVAL_MS) {
        return; // Not enough time elapsed
    }
    start_ms = current_ms;

    // Remote wakeup handling
    if (tud_suspended() && !gpio_get(PIN_BUTTON)) {
        // Wake up host if we are in suspend mode and button is pressed
        tud_remote_wakeup();
        return;
    }

    // Only send reports when USB device is properly mounted and ready
    if (!tud_mounted() || !tud_ready()) {
        return; // Don't send reports if device is not properly mounted
    }
    
    // Only send reports when devices are not connected (avoid conflicts)
    bool mouse_connected, keyboard_connected;
    
    // Direct access without lock since we have separate stacks
    mouse_connected = connection_state.mouse_connected;
    keyboard_connected = connection_state.keyboard_connected;
    
    // Always maintain the connection with consumer control reports
    send_hid_report(REPORT_ID_CONSUMER_CONTROL);
    
    // Forward reports from connected devices
    if (mouse_connected) {
        send_hid_report(REPORT_ID_MOUSE);
    }
    
    if (keyboard_connected) {
        send_hid_report(REPORT_ID_KEYBOARD);
    }
}

void send_hid_report(uint8_t report_id)
{
    // CRITICAL: Multiple checks to prevent endpoint allocation conflicts
    if (!tud_mounted() || !tud_ready()) {
        return; // Prevent endpoint conflicts by not sending reports when device isn't ready
    }
    
    // CRITICAL: Additional check to ensure device stack is stable
    static uint32_t last_mount_check = 0;
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    if (current_time - last_mount_check > 1000) { // Check every second
        if (!tud_mounted()) {
            return; // Device was unmounted, don't send reports
        }
        last_mount_check = current_time;
    }
    
    switch (report_id) {
        case REPORT_ID_KEYBOARD: {
            bool keyboard_connected;
            
            // Direct access without lock since we have separate stacks
            keyboard_connected = connection_state.keyboard_connected;
            
            // CRITICAL: Check device readiness before each report
            if (tud_hid_ready()) {
                if (keyboard_connected) {
                    // Check if we have a new keyboard report to forward
                    bool report_ready = false;
                    // Direct access without lock since we have separate stacks
                    report_ready = connection_state.keyboard_report_ready;
                    if (report_ready) {
                        connection_state.keyboard_report_ready = false;
                    }
                    
                    if (report_ready) {
                        // Forward the last received keyboard report
                        extern hid_keyboard_report_t kbd_dest_buffer;
                        tud_hid_report(REPORT_ID_KEYBOARD, &kbd_dest_buffer, sizeof(hid_keyboard_report_t));
                    }
                } else {
                    // Use static array to avoid stack allocation overhead
                    static const uint8_t empty_keycode[HID_KEYBOARD_KEYCODE_COUNT] = { 0 };
                    tud_hid_keyboard_report(REPORT_ID_KEYBOARD, MOUSE_NO_MOVEMENT, empty_keycode);
                }
            }
            break;
        }

        case REPORT_ID_MOUSE: {
            bool mouse_connected;
            
            // Direct access without lock since we have separate stacks
            mouse_connected = connection_state.mouse_connected;
            
            // CRITICAL: Check device readiness before each report
            if (tud_hid_ready()) {
                if (mouse_connected) {
                    // Check if we have a new mouse report to forward
                    bool report_ready = false;
                    // Direct access without lock since we have separate stacks
                    report_ready = connection_state.mouse_report_ready;
                    if (report_ready) {
                        connection_state.mouse_report_ready = false;
                    }
                    
                    if (report_ready) {
                        // Forward the last received mouse report
                        extern hid_mouse_report_t mouse_dest_buffer;
                        tud_hid_report(REPORT_ID_MOUSE, &mouse_dest_buffer, sizeof(hid_mouse_report_t));
                    }
                } else {
                    // Only send button-based mouse movement if no mouse is connected
                    static bool prev_button_state = true; // true = not pressed (active low)
                    bool current_button_state = gpio_get(PIN_BUTTON);
                    
                    if (!current_button_state) { // button pressed (active low)
                        // Mouse move up (negative Y direction)
                        tud_hid_mouse_report(REPORT_ID_MOUSE, MOUSE_BUTTON_NONE,
                                           MOUSE_NO_MOVEMENT, MOUSE_BUTTON_MOVEMENT_DELTA,
                                           MOUSE_NO_MOVEMENT, MOUSE_NO_MOVEMENT);
                    } else if (prev_button_state != current_button_state) {
                        // Send stop movement when button is released
                        tud_hid_mouse_report(REPORT_ID_MOUSE, MOUSE_BUTTON_NONE,
                                           MOUSE_NO_MOVEMENT, MOUSE_NO_MOVEMENT,
                                           MOUSE_NO_MOVEMENT, MOUSE_NO_MOVEMENT);
                    }
                    
                    prev_button_state = current_button_state;
                }
            }
            break;
        }

        case REPORT_ID_CONSUMER_CONTROL: {
            // CRITICAL: Check device readiness before each report
            if (tud_hid_ready()) {
                static const uint16_t empty_key = 0;
                tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &empty_key, HID_CONSUMER_CONTROL_SIZE);
            }
            break;
        }

        default:
            break;
    }
}

bool get_caps_lock_state(void)
{
    return caps_lock_state;
}