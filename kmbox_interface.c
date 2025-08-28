/*
 * KMBox Interface Implementation
 * 
 * Consolidated UART and PIO-UART interface implementation
 */

#include "kmbox_interface.h"
#include "defines.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include <string.h>

#if KMBOX_PIO_AVAILABLE
#include "hardware/pio.h"
#include "pio_uart.pio.h"
#endif

// Default configurations
const kmbox_uart_config_t KMBOX_UART_DEFAULT_CONFIG = {
    .baudrate = 250000,
    .tx_pin = 4,
    .rx_pin = 5,
    .use_dma = true
};

#if KMBOX_PIO_AVAILABLE
const kmbox_pio_uart_config_t KMBOX_PIO_UART_DEFAULT_CONFIG = {
    .baudrate = 250000,
    .tx_pin = 4,
    .rx_pin = 5,
    .use_dma = true,
    .use_interrupts = true
};
#endif

// Buffer sizes (must be power of 2)
#define RX_BUFFER_SIZE 512
#define TX_BUFFER_SIZE 256
#define RX_BUFFER_MASK (RX_BUFFER_SIZE - 1)
#define TX_BUFFER_MASK (TX_BUFFER_SIZE - 1)

// Static assertions for buffer sizes
_Static_assert((RX_BUFFER_SIZE & (RX_BUFFER_SIZE - 1)) == 0,
               "RX_BUFFER_SIZE must be a power of two");
_Static_assert((TX_BUFFER_SIZE & (TX_BUFFER_SIZE - 1)) == 0,
               "TX_BUFFER_SIZE must be a power of two");

// Interface state
typedef struct {
    // Configuration
    kmbox_interface_config_t config;
    
    // Transport-specific handles
    union {
        uart_inst_t* uart;
#if KMBOX_PIO_AVAILABLE
        struct {
            PIO pio;
            uint sm_rx;
            uint sm_tx;
            uint offset_rx;
            uint offset_tx;
        } pio_uart;
#endif
    } instance;
    
    // Ring buffers
    uint8_t __attribute__((aligned(RX_BUFFER_SIZE))) rx_buffer[RX_BUFFER_SIZE];
    uint8_t __attribute__((aligned(TX_BUFFER_SIZE))) tx_buffer[TX_BUFFER_SIZE];
    
    // Ring buffer indices
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    volatile uint16_t tx_head;
    volatile uint16_t tx_tail;
    
    // DMA channels
    int dma_rx_chan;
    int dma_tx_chan;
    
    // Statistics
    kmbox_interface_stats_t stats;
    
    // State flags
    bool initialized;
    bool tx_in_progress;
} kmbox_interface_state_t;

// Global interface state
static kmbox_interface_state_t g_interface = {
    .dma_rx_chan = -1,
    .dma_tx_chan = -1,
    .initialized = false
};

// Forward declarations
static bool init_uart(const kmbox_uart_config_t* config);
#if KMBOX_PIO_AVAILABLE
static bool init_pio_uart(const kmbox_pio_uart_config_t* config);
static void process_pio_uart(void);
static void pio_uart_dma_rx_setup(void);
static void pio_uart_dma_tx_setup(void);
static void pio_uart_irq_handler(void);
#endif
static void process_uart(void);
static void uart_dma_rx_setup(void);
static void dma_rx_irq_handler(void);

// Initialize the interface
bool kmbox_interface_init(const kmbox_interface_config_t* config)
{
    if (!config || g_interface.initialized) {
        return false;
    }
    
    // Clear state
    memset(&g_interface, 0, sizeof(g_interface));
    g_interface.dma_rx_chan = -1;
    g_interface.dma_tx_chan = -1;
    
    // Copy configuration
    g_interface.config = *config;
    
    // Initialize based on transport type
    bool success = false;
    switch (config->transport_type) {
        case KMBOX_TRANSPORT_UART:
            success = init_uart(&config->config.uart);
            break;
            
#if KMBOX_PIO_AVAILABLE
        case KMBOX_TRANSPORT_PIO_UART:
            success = init_pio_uart(&config->config.pio_uart);
            break;
#endif
            
        default:
            return false;
    }
    
    if (success) {
        g_interface.initialized = true;
    }
    
    return success;
}

// Initialize UART transport
static bool init_uart(const kmbox_uart_config_t* config)
{
    // Determine UART instance based on pins
    if (config->tx_pin == 0 && config->rx_pin == 1) {
        g_interface.instance.uart = uart0;
    } else if (config->tx_pin == 4 && config->rx_pin == 5) {
        g_interface.instance.uart = uart1;
    } else {
        return false;
    }
    
    // Initialize UART
    uart_init(g_interface.instance.uart, config->baudrate);

    // Configure pins
    gpio_set_function(config->tx_pin, GPIO_FUNC_UART);
    gpio_set_function(config->rx_pin, GPIO_FUNC_UART);
    
    // Set UART format
    uart_set_format(g_interface.instance.uart, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(g_interface.instance.uart, true);
    
    // Setup DMA if enabled
    if (config->use_dma) {
        uart_dma_rx_setup();
    }
    
    return true;
}

#if KMBOX_PIO_AVAILABLE
// Initialize PIO UART transport (RP2350 only)
static bool init_pio_uart(const kmbox_pio_uart_config_t* config)
{
    // Use dedicated PIO2 for KMBox on RP2350
    g_interface.instance.pio_uart.pio = KMBOX_PIO_INSTANCE;
    
    // Claim and add RX program
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &uart_rx_mini_program,
            &g_interface.instance.pio_uart.pio,
            &g_interface.instance.pio_uart.sm_rx,
            &g_interface.instance.pio_uart.offset_rx,
            config->rx_pin, 1, true)) {
        return false;
    }
    
    // Claim and add TX program
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(
            &uart_tx_program,
            &g_interface.instance.pio_uart.pio,
            &g_interface.instance.pio_uart.sm_tx,
            &g_interface.instance.pio_uart.offset_tx,
            config->tx_pin, 1, true)) {
        // Cleanup RX if TX fails
        pio_remove_program_and_unclaim_sm(
            &uart_rx_mini_program,
            g_interface.instance.pio_uart.pio,
            g_interface.instance.pio_uart.sm_rx,
            g_interface.instance.pio_uart.offset_rx);
        return false;
    }
    
    // Initialize state machines
    uart_rx_mini_program_init(
        g_interface.instance.pio_uart.pio,
        g_interface.instance.pio_uart.sm_rx,
        g_interface.instance.pio_uart.offset_rx,
        config->rx_pin,
        config->baudrate);
        
    uart_tx_program_init(
        g_interface.instance.pio_uart.pio,
        g_interface.instance.pio_uart.sm_tx,
        g_interface.instance.pio_uart.offset_tx,
        config->tx_pin,
        config->baudrate);
    
    // Setup DMA if enabled
    if (config->use_dma) {
        pio_uart_dma_rx_setup();
        pio_uart_dma_tx_setup();
    }
    
    // Setup interrupts if enabled
    if (config->use_interrupts) {
        irq_add_shared_handler(
            pio_get_irq_num(g_interface.instance.pio_uart.pio, 0),
            pio_uart_irq_handler,
            PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
        pio_set_irqn_source_enabled(
            g_interface.instance.pio_uart.pio, 0,
            pio_get_rx_fifo_not_empty_interrupt_source(g_interface.instance.pio_uart.sm_rx),
            true);
        irq_set_enabled(pio_get_irq_num(g_interface.instance.pio_uart.pio, 0), true);
    }
    
    return true;
}

// Setup PIO UART DMA for RX
static void pio_uart_dma_rx_setup(void)
{
    g_interface.dma_rx_chan = dma_claim_unused_channel(true);
    
    dma_channel_config c = dma_channel_get_default_config(g_interface.dma_rx_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(g_interface.instance.pio_uart.pio, g_interface.instance.pio_uart.sm_rx, false));
    channel_config_set_ring(&c, true, __builtin_ctz(RX_BUFFER_SIZE));
    
    // Setup to read from PIO RX FIFO, reading from the uppermost byte (left-justified)
    dma_channel_configure(
        g_interface.dma_rx_chan,
        &c,
        g_interface.rx_buffer,
        (io_rw_8*)&g_interface.instance.pio_uart.pio->rxf[g_interface.instance.pio_uart.sm_rx] + 3,
        0xFFFF,
        true
    );
    
    dma_channel_set_irq1_enabled(g_interface.dma_rx_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_1, dma_rx_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);
}

// Setup PIO UART DMA for TX
static void pio_uart_dma_tx_setup(void)
{
    g_interface.dma_tx_chan = dma_claim_unused_channel(true);
    
    dma_channel_config c = dma_channel_get_default_config(g_interface.dma_tx_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(g_interface.instance.pio_uart.pio, g_interface.instance.pio_uart.sm_tx, true));
    
    // Will be configured per transfer
}

// PIO UART interrupt handler
static void pio_uart_irq_handler(void)
{
    // Update statistics and handle any PIO-specific IRQ processing
    if (pio_interrupt_get(g_interface.instance.pio_uart.pio, 0)) {
        pio_interrupt_clear(g_interface.instance.pio_uart.pio, 0);
        // Handle any specific interrupt processing
    }
}
#endif

// Setup UART DMA
static void uart_dma_rx_setup(void)
{
    g_interface.dma_rx_chan = dma_claim_unused_channel(true);
    
    dma_channel_config c = dma_channel_get_default_config(g_interface.dma_rx_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, uart_get_dreq(g_interface.instance.uart, false));
    channel_config_set_ring(&c, true, __builtin_ctz(RX_BUFFER_SIZE));
    
    dma_channel_configure(
        g_interface.dma_rx_chan,
        &c,
        g_interface.rx_buffer,
        &uart_get_hw(g_interface.instance.uart)->dr,
        0xFFFF,
        true
    );
    
    dma_channel_set_irq1_enabled(g_interface.dma_rx_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_1, dma_rx_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);
}

// DMA RX IRQ handler
static void dma_rx_irq_handler(void)
{
    if (g_interface.dma_rx_chan >= 0) {
        dma_hw->ints1 = 1u << g_interface.dma_rx_chan;
        dma_channel_set_trans_count(g_interface.dma_rx_chan, 0xFFFF, true);
    }
}

// Process interface tasks
void kmbox_interface_process(void)
{
    if (!g_interface.initialized) {
        return;
    }
    
    switch (g_interface.config.transport_type) {
        case KMBOX_TRANSPORT_UART:
            process_uart();
            break;
            
#if KMBOX_PIO_AVAILABLE
        case KMBOX_TRANSPORT_PIO_UART:
            process_pio_uart();
            break;
#endif
            
        default:
            break;
    }
}

// Process UART data
static void process_uart(void)
{
    uint16_t head = g_interface.rx_head;
    uint16_t tail = g_interface.rx_tail;
    
    // Update head from DMA if using DMA
    if (g_interface.config.config.uart.use_dma && g_interface.dma_rx_chan >= 0) {
        uint32_t write_addr = dma_channel_hw_addr(g_interface.dma_rx_chan)->write_addr;
        uint32_t buffer_start = (uint32_t)g_interface.rx_buffer;
        head = (write_addr - buffer_start) & RX_BUFFER_MASK;
    } else {
        // Non-DMA: read from UART FIFO
        while (uart_is_readable(g_interface.instance.uart)) {
            uint16_t next_head = (head + 1) & RX_BUFFER_MASK;
            if (next_head != tail) {
                g_interface.rx_buffer[head] = uart_getc(g_interface.instance.uart);
                head = next_head;
            } else {
                uart_getc(g_interface.instance.uart); // Discard
                g_interface.stats.errors++;
            }
        }
        g_interface.rx_head = head;
    }
    
    // Process received data
    while (tail != head) {
        uint16_t chunk_size;
        if (head > tail) {
            chunk_size = head - tail;
        } else {
            chunk_size = RX_BUFFER_SIZE - tail;
        }
        
        if (g_interface.config.on_command_received && chunk_size > 0) {
            g_interface.config.on_command_received(&g_interface.rx_buffer[tail], chunk_size);
            g_interface.stats.bytes_received += chunk_size;
        }
        
        tail = (tail + chunk_size) & RX_BUFFER_MASK;
    }
    
    g_interface.rx_tail = tail;
}

#if KMBOX_PIO_AVAILABLE
// Process PIO UART data
static void process_pio_uart(void)
{
    uint16_t head = g_interface.rx_head;
    uint16_t tail = g_interface.rx_tail;
    
    // Update head from DMA if using DMA
    if (g_interface.config.config.pio_uart.use_dma && g_interface.dma_rx_chan >= 0) {
        uint32_t write_addr = dma_channel_hw_addr(g_interface.dma_rx_chan)->write_addr;
        uint32_t buffer_start = (uint32_t)g_interface.rx_buffer;
        head = (write_addr - buffer_start) & RX_BUFFER_MASK;
    } else {
        // Non-DMA: read from PIO FIFO
        while (!pio_sm_is_rx_fifo_empty(g_interface.instance.pio_uart.pio, g_interface.instance.pio_uart.sm_rx)) {
            uint16_t next_head = (head + 1) & RX_BUFFER_MASK;
            if (next_head != tail) {
                g_interface.rx_buffer[head] = uart_rx_mini_program_getc(
                    g_interface.instance.pio_uart.pio, 
                    g_interface.instance.pio_uart.sm_rx);
                head = next_head;
            } else {
                // Discard to prevent FIFO overflow
                uart_rx_mini_program_getc(
                    g_interface.instance.pio_uart.pio, 
                    g_interface.instance.pio_uart.sm_rx);
                g_interface.stats.errors++;
            }
        }
        g_interface.rx_head = head;
    }
    
    // Process received data
    while (tail != head) {
        uint16_t chunk_size;
        if (head > tail) {
            chunk_size = head - tail;
        } else {
            chunk_size = RX_BUFFER_SIZE - tail;
        }
        
        if (g_interface.config.on_command_received && chunk_size > 0) {
            g_interface.config.on_command_received(&g_interface.rx_buffer[tail], chunk_size);
            g_interface.stats.bytes_received += chunk_size;
        }
        
        tail = (tail + chunk_size) & RX_BUFFER_MASK;
    }
    
    g_interface.rx_tail = tail;
    
    // Handle TX transmission for PIO
    if (g_interface.tx_head != g_interface.tx_tail && !g_interface.tx_in_progress) {
        // Start PIO TX transmission
        uint16_t tx_tail = g_interface.tx_tail;
        uint16_t tx_head = g_interface.tx_head;
        
        if (g_interface.config.config.pio_uart.use_dma && g_interface.dma_tx_chan >= 0) {
            // Use DMA for transmission
            uint16_t tx_size;
            if (tx_head > tx_tail) {
                tx_size = tx_head - tx_tail;
            } else {
                tx_size = TX_BUFFER_SIZE - tx_tail;
            }
            
            if (tx_size > 0) {
                dma_channel_config c = dma_channel_get_default_config(g_interface.dma_tx_chan);
                channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
                channel_config_set_read_increment(&c, true);
                channel_config_set_write_increment(&c, false);
                channel_config_set_dreq(&c, pio_get_dreq(g_interface.instance.pio_uart.pio, g_interface.instance.pio_uart.sm_tx, true));
                
                dma_channel_configure(
                    g_interface.dma_tx_chan,
                    &c,
                    &g_interface.instance.pio_uart.pio->txf[g_interface.instance.pio_uart.sm_tx],
                    &g_interface.tx_buffer[tx_tail],
                    tx_size,
                    true
                );
                
                g_interface.tx_in_progress = true;
                g_interface.tx_tail = (tx_tail + tx_size) & TX_BUFFER_MASK;
            }
        } else {
            // Non-DMA: write directly to PIO FIFO
            while (tx_tail != tx_head && !pio_sm_is_tx_fifo_full(g_interface.instance.pio_uart.pio, g_interface.instance.pio_uart.sm_tx)) {
                uart_tx_program_putc(
                    g_interface.instance.pio_uart.pio,
                    g_interface.instance.pio_uart.sm_tx,
                    g_interface.tx_buffer[tx_tail]);
                tx_tail = (tx_tail + 1) & TX_BUFFER_MASK;
            }
            g_interface.tx_tail = tx_tail;
            
            if (tx_tail == tx_head) {
                g_interface.tx_in_progress = false;
            }
        }
    }
}
#endif

// Send data through the interface
bool kmbox_interface_send(const uint8_t* data, size_t len)
{
    if (!g_interface.initialized || !data || len == 0) {
        return false;
    }
    
    // Check available space
    uint16_t head = g_interface.tx_head;
    uint16_t tail = g_interface.tx_tail;
    uint16_t available = (tail - head - 1) & TX_BUFFER_MASK;
    
    if (available < len) {
        g_interface.stats.errors++;
        return false;
    }
    
    // Copy to TX buffer
    for (size_t i = 0; i < len; i++) {
        g_interface.tx_buffer[head] = data[i];
        head = (head + 1) & TX_BUFFER_MASK;
    }
    
    g_interface.tx_head = head;
    g_interface.stats.bytes_sent += len;
    
    // Start transmission if not in progress
    if (!g_interface.tx_in_progress) {
        // TODO: Implement TX transmission
        g_interface.tx_in_progress = true;
    }
    
    return true;
}

// Check if interface is ready to send
bool kmbox_interface_is_ready(void)
{
    if (!g_interface.initialized) {
        return false;
    }
    
    uint16_t head = g_interface.tx_head;
    uint16_t tail = g_interface.tx_tail;
    uint16_t available = (tail - head - 1) & TX_BUFFER_MASK;
    
    return available > 0;
}

// Get interface statistics
void kmbox_interface_get_stats(kmbox_interface_stats_t* stats)
{
    if (stats) {
        *stats = g_interface.stats;
    }
}

// Deinitialize the interface
void kmbox_interface_deinit(void)
{
    if (!g_interface.initialized) {
        return;
    }
    
    // Stop DMA
    if (g_interface.dma_rx_chan >= 0) {
        dma_channel_abort(g_interface.dma_rx_chan);
        dma_channel_unclaim(g_interface.dma_rx_chan);
    }
    
    if (g_interface.dma_tx_chan >= 0) {
        dma_channel_abort(g_interface.dma_tx_chan);
        dma_channel_unclaim(g_interface.dma_tx_chan);
    }
    
    // Deinitialize transport
    switch (g_interface.config.transport_type) {
        case KMBOX_TRANSPORT_UART:
            uart_deinit(g_interface.instance.uart);
            break;
            
#if KMBOX_PIO_AVAILABLE
        case KMBOX_TRANSPORT_PIO_UART:
            // Disable interrupts
            if (g_interface.config.config.pio_uart.use_interrupts) {
                pio_set_irqn_source_enabled(
                    g_interface.instance.pio_uart.pio, 0,
                    pio_get_rx_fifo_not_empty_interrupt_source(g_interface.instance.pio_uart.sm_rx),
                    false);
                irq_remove_handler(pio_get_irq_num(g_interface.instance.pio_uart.pio, 0), pio_uart_irq_handler);
                if (!irq_has_shared_handler(pio_get_irq_num(g_interface.instance.pio_uart.pio, 0))) {
                    irq_set_enabled(pio_get_irq_num(g_interface.instance.pio_uart.pio, 0), false);
                }
            }
            
            // Clean up PIO programs and state machines
            pio_remove_program_and_unclaim_sm(
                &uart_rx_mini_program,
                g_interface.instance.pio_uart.pio,
                g_interface.instance.pio_uart.sm_rx,
                g_interface.instance.pio_uart.offset_rx);
            pio_remove_program_and_unclaim_sm(
                &uart_tx_program,
                g_interface.instance.pio_uart.pio,
                g_interface.instance.pio_uart.sm_tx,
                g_interface.instance.pio_uart.offset_tx);
            break;
#endif
            
        default:
            break;
    }
    
    g_interface.initialized = false;
}

// Get current transport type
kmbox_transport_type_t kmbox_interface_get_transport_type(void)
{
    return g_interface.initialized ? g_interface.config.transport_type : KMBOX_TRANSPORT_NONE;
}