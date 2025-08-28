/*
 * KMBox Interface - UART and PIO-UART Interface
 * 
 * Provides a clean, unified interface for KMBox communication
 * supporting both hardware UART and PIO-UART transports.
 */

#ifndef KMBOX_INTERFACE_H
#define KMBOX_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "defines.h"

// Transport types
typedef enum {
    KMBOX_TRANSPORT_NONE = 0,
    KMBOX_TRANSPORT_UART,
#if KMBOX_PIO_AVAILABLE
    KMBOX_TRANSPORT_PIO_UART
#endif
} kmbox_transport_type_t;

typedef struct {
    uint32_t baudrate;
    unsigned int tx_pin;
    unsigned int rx_pin;
    bool use_dma;
} kmbox_uart_config_t;

#if KMBOX_PIO_AVAILABLE
typedef struct {
    uint32_t baudrate;
    unsigned int tx_pin;
    unsigned int rx_pin;
    bool use_dma;
    bool use_interrupts;
} kmbox_pio_uart_config_t;
#endif

// Main interface configuration
typedef struct {
    kmbox_transport_type_t transport_type;
    union {
        kmbox_uart_config_t uart;
#if KMBOX_PIO_AVAILABLE
        kmbox_pio_uart_config_t pio_uart;
#endif
    } config;
    
    // Callback for received commands
    void (*on_command_received)(const uint8_t* data, size_t len);
} kmbox_interface_config_t;

// Statistics
typedef struct {
    uint32_t bytes_received;
    uint32_t bytes_sent;
    uint32_t packets_received;
    uint32_t packets_sent;
    uint32_t errors;
    uint32_t commands_processed;
} kmbox_interface_stats_t;

// Initialize the interface with configuration
bool kmbox_interface_init(const kmbox_interface_config_t* config);

// Process interface tasks (call periodically)
void kmbox_interface_process(void);

// Send data through the interface
bool kmbox_interface_send(const uint8_t* data, size_t len);

// Check if interface is ready to send
bool kmbox_interface_is_ready(void);

// Get interface statistics
void kmbox_interface_get_stats(kmbox_interface_stats_t* stats);

// Deinitialize the interface
void kmbox_interface_deinit(void);

// Get current transport type
kmbox_transport_type_t kmbox_interface_get_transport_type(void);

// Default configurations
extern const kmbox_uart_config_t KMBOX_UART_DEFAULT_CONFIG;
#if KMBOX_PIO_AVAILABLE
extern const kmbox_pio_uart_config_t KMBOX_PIO_UART_DEFAULT_CONFIG;
#endif

#endif // KMBOX_INTERFACE_H