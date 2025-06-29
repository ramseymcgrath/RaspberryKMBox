/*
 * Hurricane PIOKMbox Firmware
 */

#ifndef USB_HID_DEVICE_H
#define USB_HID_DEVICE_H

#include <stdbool.h>
#include "usb_hid_types.h"

// Device mode functions
void hid_device_task(void);
void send_hid_report(uint8_t report_id);

// State management
bool get_caps_lock_state(void);

#endif // USB_HID_DEVICE_H