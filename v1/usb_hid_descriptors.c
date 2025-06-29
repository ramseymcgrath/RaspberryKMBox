/*
 * Hurricane PIOKMbox Firmware
 */

#include "usb_hid_descriptors.h"
#include "usb_hid_types.h"
#include "defines.h"
#include <stdio.h>
#include <string.h>

// Modifiable copy of the device descriptor (in RAM)
static tusb_desc_device_t ram_device_descriptor;

// Flag to track if we've initialized our RAM copy
static bool ram_descriptor_initialized = false;

// HID report descriptor for device mode
// Single Report (no ID) descriptor
uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(2)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(3))
};

// USB Device Descriptor
tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD_VERSION,
    .bDeviceClass       = USB_DEVICE_CLASS_NONE,
    .bDeviceSubClass    = USB_DEVICE_SUBCLASS_NONE,
    .bDeviceProtocol    = USB_DEVICE_PROTOCOL_NONE,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VENDOR_ID,
    .idProduct          = USB_PRODUCT_ID,
    .bcdDevice          = USB_DEVICE_VERSION,
    .iManufacturer      = STRING_DESC_MANUFACTURER_IDX,
    .iProduct           = STRING_DESC_PRODUCT_IDX,
    .iSerialNumber      = STRING_DESC_SERIAL_IDX,
    .bNumConfigurations = USB_NUM_CONFIGURATIONS
};

// USB Configuration Descriptor
uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(USB_CONFIG_INDEX, 1, USB_INTERFACE_STRING_NONE, CONFIG_TOTAL_LEN,
                         TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, USB_CONFIG_POWER_MA),

    // Interface number, string index, protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, USB_INTERFACE_STRING_NONE, 0, sizeof(desc_hid_report),
                      EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, HID_POLLING_INTERVAL_MS)
};

// String Descriptors
// Array of pointer to string descriptors
char const* string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 },  // 0: Supported language is English (0x0409)
    MANUFACTURER_STRING,            // 1: Manufacturer
    PRODUCT_STRING,                 // 2: Product
    NULL,                           // 3: Serial number (set dynamically)
};

void update_device_descriptors(uint16_t vid, uint16_t pid)
{
    // Make a copy of the device descriptor if we haven't already
    if (!ram_descriptor_initialized) {
        memcpy(&ram_device_descriptor, &desc_device, sizeof(tusb_desc_device_t));
        ram_descriptor_initialized = true;
    }
    
    // Update the VID/PID in our RAM copy
    ram_device_descriptor.idVendor = vid;
    ram_device_descriptor.idProduct = pid;
    
    // Point the descriptor pointer to our RAM copy
    desc_device = ram_device_descriptor;
    
    LOG_VERBOSE("Device descriptors updated: VID:0x%04X PID:0x%04X", vid, pid);
}

void reset_device_descriptors(void)
{
    // Reset to default VID/PID
    if (ram_descriptor_initialized) {
        ram_device_descriptor.idVendor = USB_VENDOR_ID;
        ram_device_descriptor.idProduct = USB_PRODUCT_ID;
        
        // Point the descriptor pointer to our RAM copy
        desc_device = ram_device_descriptor;
        
        LOG_VERBOSE("Device descriptors reset to default: VID:0x%04X PID:0x%04X",
                   USB_VENDOR_ID, USB_PRODUCT_ID);
    }
}

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const * tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return desc_hid_report;
}

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index; // for multiple configurations
    return desc_configuration;
}