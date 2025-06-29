/*
 * Hurricane PIOKMbox Firmware
 */

#ifndef USB_HID_TYPES_H
#define USB_HID_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "defines.h"
#include "tusb.h"
#include "class/hid/hid.h"
#include "class/hid/hid_device.h"
#include "class/hid/hid_host.h"

//--------------------------------------------------------------------+
// HID REPORT DEFINITIONS
//--------------------------------------------------------------------+

// HID Report IDs for device mode
enum {
  REPORT_ID_KEYBOARD = 1,
  REPORT_ID_MOUSE,
  REPORT_ID_CONSUMER_CONTROL,
  REPORT_ID_COUNT
};

// Error tracking structure with better organization
typedef struct {
    uint32_t device_errors;
    uint32_t host_errors;
    uint32_t consecutive_device_errors;
    uint32_t consecutive_host_errors;
    uint32_t last_error_check_time;
    bool device_error_state;
    bool host_error_state;
} usb_error_tracker_t;

// Device connection state with better encapsulation
typedef struct {
    bool mouse_connected;
    bool keyboard_connected;
    uint8_t mouse_dev_addr;
    uint8_t keyboard_dev_addr;
    uint16_t mouse_vid;
    uint16_t mouse_pid;
    uint16_t keyboard_vid;
    uint16_t keyboard_pid;
    bool descriptors_updated;
    
    // String descriptor storage
    char manufacturer[USB_STRING_BUFFER_SIZE];
    char product[USB_STRING_BUFFER_SIZE];
    char serial[USB_STRING_BUFFER_SIZE];
    bool strings_updated;
} device_connection_state_t;

// HID statistics structure
typedef struct {
    uint32_t mouse_reports_received;
    uint32_t mouse_reports_forwarded;
    uint32_t keyboard_reports_received;
    uint32_t keyboard_reports_forwarded;
    uint32_t forwarding_errors;
    
#if defined(RP2350)
    // RP2350 hardware acceleration statistics
    uint32_t hw_accel_reports_processed;
    uint32_t sw_fallback_reports_processed;
    uint32_t hw_accel_errors;
    uint64_t hw_processing_time_us;  // Cumulative processing time in microseconds
    uint64_t sw_processing_time_us;  // Cumulative processing time in microseconds
    uint32_t hw_processing_count;    // Number of measurements
    uint32_t sw_processing_count;    // Number of measurements
    float hw_accel_success_rate;     // Success rate (calculated on retrieval)
    float hw_avg_processing_time_us; // Average hardware processing time (calculated on retrieval)
    float sw_avg_processing_time_us; // Average software processing time (calculated on retrieval)
#endif
} hid_stats_t;

// Internal performance stats (alias to hid_stats_t for backward compatibility)
typedef hid_stats_t performance_stats_t;

#endif // USB_HID_TYPES_H