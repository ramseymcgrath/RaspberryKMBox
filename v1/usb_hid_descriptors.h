/*
 * Hurricane PIOKMbox Firmware
 */

#ifndef USB_HID_DESCRIPTORS_H
#define USB_HID_DESCRIPTORS_H

#include <stdint.h>
#include "tusb.h"

//--------------------------------------------------------------------+
// USB DESCRIPTORS
//--------------------------------------------------------------------+

// HID report descriptor for device mode
extern uint8_t const desc_hid_report[];

// Device descriptor (non-const to allow VID/PID updates)
extern tusb_desc_device_t desc_device;

// Configuration descriptor
extern uint8_t const desc_configuration[];

// String descriptors
extern char const* string_desc_arr[];

// Descriptor management functions
void update_device_descriptors(uint16_t vid, uint16_t pid);
void reset_device_descriptors(void);

#endif // USB_HID_DESCRIPTORS_H