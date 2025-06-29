/*
 * Hurricane PIOKMbox Firmware
 */

#ifndef USB_HID_STRINGS_H
#define USB_HID_STRINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// String descriptor functions
bool read_device_string_descriptor(uint8_t dev_addr, uint8_t string_index,
                                  uint16_t lang_id, char* buffer, size_t buffer_size);
bool read_device_string_descriptors(uint8_t dev_addr,
                                   char* manufacturer, size_t man_size,
                                   char* product, size_t prod_size,
                                   char* serial, size_t serial_size);
void update_string_descriptors(const char* manufacturer, const char* product, const char* serial);
void reset_string_descriptors(void);

// String descriptor callback
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid);

#endif // USB_HID_STRINGS_H