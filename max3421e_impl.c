/*
 * MAX3421E Implementation for PIOKMbox
 * 
 * This file provides the board-specific implementation for MAX3421E
 * register read/write functions required by TinyUSB.
 */

#include <stdint.h>
#include <stdbool.h>
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "tusb.h"

#if CFG_TUH_ENABLED && CFG_TUH_MAX3421E

// MAX3421E SPI configuration - use SDK defaults where available
#ifndef MAX3421E_SPI
#ifdef PICO_DEFAULT_SPI
// PICO_DEFAULT_SPI is just the instance number (0 or 1)
#if PICO_DEFAULT_SPI == 0
#define MAX3421E_SPI        spi0
#else
#define MAX3421E_SPI        spi1
#endif
#else
#define MAX3421E_SPI        spi0
#endif
#endif

#ifndef MAX3421E_SCK_PIN
#ifdef PICO_DEFAULT_SPI_SCK_PIN
#define MAX3421E_SCK_PIN    PICO_DEFAULT_SPI_SCK_PIN
#else
#define MAX3421E_SCK_PIN    22   // Fallback for RP2350 Feather
#endif
#endif

#ifndef MAX3421E_MOSI_PIN
#ifdef PICO_DEFAULT_SPI_TX_PIN
#define MAX3421E_MOSI_PIN   PICO_DEFAULT_SPI_TX_PIN
#else
#define MAX3421E_MOSI_PIN   23   // Fallback for RP2350 Feather
#endif
#endif

#ifndef MAX3421E_MISO_PIN
#ifdef PICO_DEFAULT_SPI_RX_PIN
#define MAX3421E_MISO_PIN   PICO_DEFAULT_SPI_RX_PIN
#else
#define MAX3421E_MISO_PIN   20   // Fallback for RP2350 Feather
#endif
#endif

#ifndef MAX3421E_CS_PIN
#define MAX3421E_CS_PIN     9    // Chip select pin (board-specific)
#endif

// MAX3421E register access bits
#define MAX3421E_REG_WRITE  0x02
#define MAX3421E_REG_READ   0x00

// Initialize SPI for MAX3421E
static bool max3421e_spi_initialized = false;

static void max3421e_spi_init(void) {
    if (max3421e_spi_initialized) return;
    
    // Configure CS pin first and ensure it's high
    gpio_init(MAX3421E_CS_PIN);
    gpio_set_dir(MAX3421E_CS_PIN, GPIO_OUT);
    gpio_put(MAX3421E_CS_PIN, 1); // CS high (inactive)
    
    // Small delay to ensure CS is stable
    sleep_us(10);
    
    // Initialize SPI at a lower speed first for stability
    // Start at 4MHz, then we can increase later if needed
    spi_init(MAX3421E_SPI, 4000000);
    
    // Configure SPI pins
    gpio_set_function(MAX3421E_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(MAX3421E_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(MAX3421E_MISO_PIN, GPIO_FUNC_SPI);
    
    // Configure SPI format: Mode 0 (CPOL=0, CPHA=0), 8 bits
    spi_set_format(MAX3421E_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    
    // Add a small delay after SPI init
    sleep_ms(1);
    
    // Now increase SPI speed to operational speed (26MHz max for MAX3421E)
    // But use a more conservative 20MHz for better stability
    spi_set_baudrate(MAX3421E_SPI, 20000000);
    
    max3421e_spi_initialized = true;
}

// Read a register from MAX3421E
uint8_t tuh_max3421_reg_read(uint8_t rhport, uint8_t reg, bool in_isr) {
    (void)rhport;
    (void)in_isr;
    
    // Ensure SPI is initialized
    max3421e_spi_init();
    
    uint8_t tx_buf[2] = { reg | MAX3421E_REG_READ, 0 };
    uint8_t rx_buf[2] = { 0, 0 };
    
    // Small delay before transaction
    sleep_us(1);
    
    // Assert CS
    gpio_put(MAX3421E_CS_PIN, 0);
    
    // Small delay after CS assert (MAX3421E needs min 20ns)
    sleep_us(1);
    
    // Perform SPI transaction
    spi_write_read_blocking(MAX3421E_SPI, tx_buf, rx_buf, 2);
    
    // Small delay before CS deassert
    sleep_us(1);
    
    // Deassert CS
    gpio_put(MAX3421E_CS_PIN, 1);
    
    // Small delay after CS deassert
    sleep_us(1);
    
    return rx_buf[1];
}

// Write a register to MAX3421E
bool tuh_max3421_reg_write(uint8_t rhport, uint8_t reg, uint8_t data, bool in_isr) {
    (void)rhport;
    (void)in_isr;
    
    // Ensure SPI is initialized
    max3421e_spi_init();
    
    uint8_t tx_buf[2] = { reg | MAX3421E_REG_WRITE, data };
    
    // Small delay before transaction
    sleep_us(1);
    
    // Assert CS
    gpio_put(MAX3421E_CS_PIN, 0);
    
    // Small delay after CS assert (MAX3421E needs min 20ns)
    sleep_us(1);
    
    // Perform SPI transaction
    spi_write_blocking(MAX3421E_SPI, tx_buf, 2);
    
    // Small delay before CS deassert
    sleep_us(1);
    
    // Deassert CS
    gpio_put(MAX3421E_CS_PIN, 1);
    
    // Small delay after CS deassert
    sleep_us(1);
    
    return true;
}

#endif // CFG_TUH_ENABLED && CFG_TUH_MAX3421E