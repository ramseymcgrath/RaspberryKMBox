/*
 * Hurricane PIOKMbox Firmware
 */

#ifndef USB_HID_HOST_H
#define USB_HID_HOST_H

#include <stdbool.h>
#include <stdint.h>
#include "usb_hid_types.h"

// Connection state functions
bool is_mouse_connected(void);
bool is_keyboard_connected(void);
void get_connected_mouse_vid_pid(uint16_t* vid, uint16_t* pid);

// Device connection/disconnection handlers
void handle_device_disconnection(uint8_t dev_addr);
void handle_hid_device_connection(uint8_t dev_addr, uint8_t itf_protocol);

#endif // USB_HID_HOST_H