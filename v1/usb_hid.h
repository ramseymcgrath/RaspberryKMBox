/*
 * Hurricane PIOKMbox Firmware
 */

#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>
#include <stdbool.h>
#include "tusb.h"
#include "class/hid/hid.h"
#include "class/hid/hid_device.h"
#include "class/hid/hid_host.h"

// Include all component headers
#include "usb_hid_types.h"
#include "usb_hid_init.h"
#include "usb_hid_device.h"
#include "usb_hid_host.h"
#include "usb_hid_reports.h"
#include "usb_hid_descriptors.h"
#include "usb_hid_strings.h"
#include "usb_hid_callbacks.h"
#include "usb_hid_stats.h"

#endif // USB_HID_H