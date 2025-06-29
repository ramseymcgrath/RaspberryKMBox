/*
 * Hurricane PIOKMbox Firmware
 */

#include "usb_hid_host.h"
#include "defines.h"
#include "led_control.h"
#include "usb_hid_descriptors.h"
#include "usb_hid_strings.h"
#include <stdio.h>
#include <string.h>

// External declarations for variables defined in other modules
extern device_connection_state_t connection_state;

bool is_mouse_connected(void)
{
    return connection_state.mouse_connected;
}

bool is_keyboard_connected(void)
{
    return connection_state.keyboard_connected;
}

void get_connected_mouse_vid_pid(uint16_t* vid, uint16_t* pid)
{
    if (vid != NULL && pid != NULL) {
        if (connection_state.mouse_connected) {
            *vid = connection_state.mouse_vid;
            *pid = connection_state.mouse_pid;
        } else {
            // Return default values if no mouse is connected
            *vid = USB_VENDOR_ID;
            *pid = USB_PRODUCT_ID;
        }
    }
}

void handle_device_disconnection(uint8_t dev_addr)
{
    // Only reset connection flags for the specific device that was disconnected
    if (dev_addr == connection_state.mouse_dev_addr) {
        connection_state.mouse_connected = false;
        connection_state.mouse_dev_addr = 0;
        
        // Clear mouse VID/PID
        connection_state.mouse_vid = 0;
        connection_state.mouse_pid = 0;
        
        // Reset device descriptors if no keyboard is connected
        // or use keyboard VID/PID if a keyboard is connected
        if (connection_state.keyboard_connected) {
            update_device_descriptors(connection_state.keyboard_vid, connection_state.keyboard_pid);
            LOG_VERBOSE("USB Host: Mouse disconnected - switching to keyboard VID/PID");
        } else {
            reset_device_descriptors();
            LOG_VERBOSE("USB Host: Mouse disconnected - resetting to default VID/PID");
        }
        
        // Mouse disconnected - re-enabling button-based movement (reduced logging)
    }
    
    if (dev_addr == connection_state.keyboard_dev_addr) {
        connection_state.keyboard_connected = false;
        connection_state.keyboard_dev_addr = 0;
        
        // Clear keyboard VID/PID
        connection_state.keyboard_vid = 0;
        connection_state.keyboard_pid = 0;
        
        // Only reset device descriptors if no mouse is connected
        // (mouse takes precedence for VID/PID passthrough)
        if (!connection_state.mouse_connected) {
            reset_device_descriptors();
            LOG_VERBOSE("USB Host: Keyboard disconnected - resetting to default VID/PID");
        } else {
            LOG_VERBOSE("USB Host: Keyboard disconnected - keeping mouse VID/PID");
        }
        
        // Keyboard disconnected (reduced logging)
    }
}

void handle_hid_device_connection(uint8_t dev_addr, uint8_t itf_protocol)
{
    // Validate input parameters
    if (dev_addr == 0) {
        LOG_ERROR("Invalid device address: %d", dev_addr);
        return;
    }
    
    LOG_VERBOSE("USB Host: Handling HID device connection - addr=%d, protocol=%d", dev_addr, itf_protocol);
    
    // Get VID/PID for the connected device
    uint16_t vid = 0, pid = 0;
    bool has_vid_pid = tuh_vid_pid_get(dev_addr, &vid, &pid);
    
    // Track connected device types and store device addresses and VID/PID
    switch (itf_protocol) {
        case HID_ITF_PROTOCOL_MOUSE:
            connection_state.mouse_connected = true;
            connection_state.mouse_dev_addr = dev_addr;
            if (has_vid_pid) {
                connection_state.mouse_vid = vid;
                connection_state.mouse_pid = pid;
                // Update device descriptors with mouse VID/PID
                update_device_descriptors(vid, pid);
                
                printf("USB Host: Mouse connected at address %d (VID:0x%04X PID:0x%04X) - disabling button-based movement\n",
                       dev_addr, vid, pid);
            } else {
                printf("USB Host: Mouse connected at address %d - disabling button-based movement\n", dev_addr);
            }
            neopixel_trigger_mouse_activity();  // Flash magenta for mouse connection
            break;
            
        case HID_ITF_PROTOCOL_KEYBOARD:
            connection_state.keyboard_connected = true;
            connection_state.keyboard_dev_addr = dev_addr;
            if (has_vid_pid) {
                connection_state.keyboard_vid = vid;
                connection_state.keyboard_pid = pid;
                // Only update device descriptors if no mouse is connected
                // (mouse takes precedence for VID/PID passthrough)
                if (!connection_state.mouse_connected) {
                    update_device_descriptors(vid, pid);
                }
                printf("USB Host: Keyboard connected at address %d (VID:0x%04X PID:0x%04X)\n",
                       dev_addr, vid, pid);
            } else {
                printf("USB Host: Keyboard connected at address %d\n", dev_addr);
            }
            neopixel_trigger_keyboard_activity();  // Flash yellow for keyboard connection
            break;
            
        default:
            printf("USB Host: Unknown HID protocol: %d\n", itf_protocol);
            break;
    }
    
    printf("USB Host: Connection state - Mouse: %s, Keyboard: %s\n",
           connection_state.mouse_connected ? "YES" : "NO",
           connection_state.keyboard_connected ? "YES" : "NO");
}