/*
 * Hurricane PIOKMbox Firmware
 */

#include "usb_hid_strings.h"
#include "usb_hid_types.h"
#include "usb_hid_descriptors.h"
#include "defines.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#if PIO_USB_AVAILABLE
#include "pio_usb.h"
#include "lib/pio_usb/src/pio_usb.h"
#include "lib/pio_usb/src/pio_usb_ll.h"
#endif

// External declarations for variables defined in other modules
extern device_connection_state_t connection_state;
extern char serial_string[SERIAL_STRING_BUFFER_SIZE];

// Read a string descriptor from a connected USB device
bool read_device_string_descriptor(uint8_t dev_addr, uint8_t string_index,
                                  uint16_t lang_id, char* buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        LOG_ERROR("Invalid buffer for string descriptor");
        return false;
    }
    
    // Clear the buffer first
    memset(buffer, 0, buffer_size);
    
    // Default to English US if no language ID specified
    if (lang_id == 0) {
        lang_id = 0x0409; // English US
    }
    
    // Setup packet for string descriptor request
    uint8_t setup_packet[8] = {
        0x80 | 0x00 | 0x00, // bmRequestType: Device-to-host | Standard | Device
        0x06,                   // bRequest: GET_DESCRIPTOR
        string_index,           // wValueL: String index
        0x03,                   // wValueH: String descriptor type
        (uint8_t)(lang_id & 0xFF),       // wIndexL: Language ID LSB
        (uint8_t)(lang_id >> 8),         // wIndexH: Language ID MSB
        buffer_size,            // wLengthL: Max length to retrieve
        0                       // wLengthH: (high byte of 16-bit length)
    };
    
    // Temporary buffer for the raw descriptor (UTF-16 format)
    uint8_t raw_descriptor[64];
    memset(raw_descriptor, 0, sizeof(raw_descriptor));
    
    // Send setup packet to request the string descriptor
    if (!pio_usb_host_send_setup(USB_HOST_PORT, dev_addr, setup_packet)) {
        LOG_ERROR("Failed to send setup packet for string descriptor %d", string_index);
        return false;
    }
    
    // Wait for the transfer to complete
    sleep_ms(10);
    
    // Read the descriptor data
    if (!pio_usb_host_endpoint_transfer(USB_HOST_PORT, dev_addr, 0x80, raw_descriptor, sizeof(raw_descriptor))) {
        LOG_ERROR("Failed to read string descriptor %d data", string_index);
        return false;
    }
    
    // Wait for the transfer to complete
    sleep_ms(10);
    
    // Check if we got valid data
    if (raw_descriptor[0] < 2 || raw_descriptor[1] != 0x03) { // 0x03 is string descriptor type
        LOG_ERROR("Invalid string descriptor %d received", string_index);
        return false;
    }
    
    // Convert UTF-16LE to ASCII
    // First byte is the length, second byte is the descriptor type
    // Actual string data starts at offset 2
    uint8_t str_len = (raw_descriptor[0] - 2) / 2; // Length in characters (16-bit per char)
    
    // Limit to buffer size minus null terminator
    if (str_len >= buffer_size) {
        str_len = buffer_size - 1;
    }
    
    // Convert UTF-16LE to ASCII (simple conversion, ignoring high byte)
    for (uint8_t i = 0; i < str_len; i++) {
        buffer[i] = raw_descriptor[2 + (i * 2)]; // Take only the low byte of each UTF-16 character
    }
    
    // Ensure null termination
    buffer[str_len] = '\0';
    
    LOG_VERBOSE("Read string descriptor %d: '%s'", string_index, buffer);
    return true;
}

// Read all three main string descriptors from a device
bool read_device_string_descriptors(uint8_t dev_addr,
                                   char* manufacturer, size_t man_size,
                                   char* product, size_t prod_size,
                                   char* serial, size_t serial_size)
{
    if (dev_addr == 0) {
        LOG_ERROR("Invalid device address for string descriptor read");
        return false;
    }
    
    // Get the device descriptor to find string indices
    tusb_desc_device_t dev_desc;
    uint8_t setup_packet[8] = {
        0x80 | 0x00 | 0x00, // bmRequestType: Device-to-host | Standard | Device
        0x06,                   // bRequest: GET_DESCRIPTOR
        0x00,                   // wValueL: Descriptor index (0)
        0x01,                   // wValueH: Device descriptor type
        0x00,                   // wIndexL: 0
        0x00,                   // wIndexH: 0
        sizeof(tusb_desc_device_t), // wLengthL: Device descriptor size
        0                       // wLengthH: (high byte of 16-bit length)
    };
    
    // Send setup packet to request the device descriptor
    if (!pio_usb_host_send_setup(USB_HOST_PORT, dev_addr, setup_packet)) {
        LOG_ERROR("Failed to send setup packet for device descriptor");
        return false;
    }
    
    // Wait for the transfer to complete
    sleep_ms(10);
    
    // Read the descriptor data
    if (!pio_usb_host_endpoint_transfer(USB_HOST_PORT, dev_addr, 0x80, (uint8_t*)&dev_desc, sizeof(dev_desc))) {
        LOG_ERROR("Failed to read device descriptor data");
        return false;
    }
    
    // Wait for the transfer to complete
    sleep_ms(10);
    
    bool success = true;
    
    // Read manufacturer string if available
    if (dev_desc.iManufacturer != 0 && manufacturer != NULL && man_size > 0) {
        if (!read_device_string_descriptor(dev_addr, dev_desc.iManufacturer, 0x0409, manufacturer, man_size)) {
            LOG_ERROR("Failed to read manufacturer string");
            success = false;
            // Use default manufacturer string
            strncpy(manufacturer, MANUFACTURER_STRING, man_size - 1);
            manufacturer[man_size - 1] = '\0';
        }
    } else if (manufacturer != NULL && man_size > 0) {
        // No manufacturer string, use default
        strncpy(manufacturer, MANUFACTURER_STRING, man_size - 1);
        manufacturer[man_size - 1] = '\0';
    }
    
    // Read product string if available
    if (dev_desc.iProduct != 0 && product != NULL && prod_size > 0) {
        if (!read_device_string_descriptor(dev_addr, dev_desc.iProduct, 0x0409, product, prod_size)) {
            LOG_ERROR("Failed to read product string");
            success = false;
            // Use default product string
            strncpy(product, PRODUCT_STRING, prod_size - 1);
            product[prod_size - 1] = '\0';
        }
    } else if (product != NULL && prod_size > 0) {
        // No product string, use default
        strncpy(product, PRODUCT_STRING, prod_size - 1);
        product[prod_size - 1] = '\0';
    }
    
    // Read serial string if available
    if (dev_desc.iSerialNumber != 0 && serial != NULL && serial_size > 0) {
        if (!read_device_string_descriptor(dev_addr, dev_desc.iSerialNumber, 0x0409, serial, serial_size)) {
            LOG_ERROR("Failed to read serial string");
            success = false;
            // Use default serial string
            strncpy(serial, serial_string, serial_size - 1);
            serial[serial_size - 1] = '\0';
        }
    } else if (serial != NULL && serial_size > 0) {
        // No serial string, use default
        strncpy(serial, serial_string, serial_size - 1);
        serial[serial_size - 1] = '\0';
    }
    
    return success;
}

// Update string descriptors with values from connected device
void update_string_descriptors(const char* manufacturer, const char* product, const char* serial)
{
    // Update the string descriptor array with the new values
    if (manufacturer != NULL && strlen(manufacturer) > 0) {
        string_desc_arr[STRING_DESC_MANUFACTURER_IDX] = manufacturer;
    }
    
    if (product != NULL && strlen(product) > 0) {
        string_desc_arr[STRING_DESC_PRODUCT_IDX] = product;
    }
    
    if (serial != NULL && strlen(serial) > 0) {
        string_desc_arr[STRING_DESC_SERIAL_IDX] = serial;
    }
    
    connection_state.strings_updated = true;
    LOG_VERBOSE("String descriptors updated: Mfr='%s', Prod='%s', SN='%s'",
               manufacturer, product, serial);
}

// Reset string descriptors to default values
void reset_string_descriptors(void)
{
    if (connection_state.strings_updated) {
        // Restore original values
        string_desc_arr[STRING_DESC_MANUFACTURER_IDX] = MANUFACTURER_STRING;
        string_desc_arr[STRING_DESC_PRODUCT_IDX] = PRODUCT_STRING;
        string_desc_arr[STRING_DESC_SERIAL_IDX] = serial_string;
        
        // Reset the connection state strings
        strncpy(connection_state.manufacturer, MANUFACTURER_STRING, USB_STRING_BUFFER_SIZE - 1);
        strncpy(connection_state.product, PRODUCT_STRING, USB_STRING_BUFFER_SIZE - 1);
        strncpy(connection_state.serial, serial_string, USB_STRING_BUFFER_SIZE - 1);
        
        connection_state.strings_updated = false;
        LOG_VERBOSE("String descriptors reset to default values");
    }
}

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void) langid;

    // Convert ASCII string into UTF-16
    static uint16_t _desc_str[MAX_STRING_DESCRIPTOR_CHARS + 1];
    uint8_t chr_count;

    if (index == STRING_DESC_LANGUAGE_IDX) {
        // Language descriptor (always first)
        memcpy(&_desc_str[STRING_DESC_FIRST_CHAR_OFFSET], string_desc_arr[STRING_DESC_LANGUAGE_IDX], 2);
        chr_count = STRING_DESC_CHAR_COUNT_INIT;
    } else {
        // String descriptor
        const char* str = string_desc_arr[index];
        
        // Handle serial number specially
        if (index == STRING_DESC_SERIAL_IDX) {
            // Use the serial string generated from the unique board ID
            str = serial_string;
        }
        
        // Cap at max char
        chr_count = strlen(str);
        if (chr_count > MAX_STRING_DESCRIPTOR_CHARS) {
            chr_count = MAX_STRING_DESCRIPTOR_CHARS;
        }

        // Convert ASCII to UTF-16
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[i + STRING_DESC_FIRST_CHAR_OFFSET] = str[i];
        }
    }

    // First byte is length (including header), second byte is string type
    _desc_str[0] = (STRING_DESC_HEADER_SIZE + (chr_count * STRING_DESC_LENGTH_MULTIPLIER));
    _desc_str[0] |= (TUSB_DESC_STRING << STRING_DESC_TYPE_SHIFT);

    return _desc_str;
}