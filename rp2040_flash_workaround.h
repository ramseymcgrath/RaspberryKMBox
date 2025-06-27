#ifndef RP2040_FLASH_WORKAROUND_H
#define RP2040_FLASH_WORKAROUND_H

// Workaround for Pico SDK 2.1.1 bug where RP2350 flash code is compiled for RP2040
// This file provides dummy definitions to allow compilation to succeed

#ifdef PICO_RP2040

#include <stdint.h>

// Dummy QMI hardware structure for RP2040 (doesn't actually exist)
typedef struct {
    struct {
        uint32_t timing;
        uint32_t wfmt;
        uint32_t wcmd;
    } m[2];
} qmi_hw_t;

// Dummy QMI hardware pointer (will never be used at runtime)
#define qmi_hw ((qmi_hw_t*)0)

// Dummy flash devinfo functions
#define FLASH_DEVINFO_SIZE_NONE 0
static inline uint32_t flash_devinfo_get_cs_size(uint32_t cs) {
    (void)cs;
    return FLASH_DEVINFO_SIZE_NONE;
}

// Dummy QMI reset values
#define QMI_M1_WFMT_RESET 0
#define QMI_M1_WCMD_RESET 0

#endif // PICO_RP2040

#endif // RP2040_FLASH_WORKAROUND_H