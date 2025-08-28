/*
 * KMBox Serial Command Handler
 * Integrates kmbox-commands library with the PIOKMBox firmware
 * Uses dedicated UART1 for KMBox serial input (separate from debug UART0)
 * On RP2350: Uses PIO UART with dedicated PIO2 for hardware offloading
 */

#include "kmbox_serial_handler.h"
#include "kmbox_interface.h"
#include "lib/kmbox-commands/kmbox_commands.h"
#include "usb_hid.h"
#include "led_control.h"
#include "defines.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include <stdio.h>

// Ring buffer for non-blocking UART reception
// Use power-of-2 size for efficient modulo operation
#define UART_RX_BUFFER_SIZE 256
#define UART_RX_BUFFER_MASK (UART_RX_BUFFER_SIZE - 1)
static volatile uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint16_t uart_rx_head = 0;
static volatile uint16_t uart_rx_tail = 0;

// UART RX interrupt handler for high-performance non-blocking reception
static void on_uart_rx(void) {
    while (uart_is_readable(KMBOX_UART)) {
        uint8_t ch = uart_getc(KMBOX_UART);
        
        // Calculate next head position using bitwise AND (faster than modulo)
        uint16_t next_head = (uart_rx_head + 1) & UART_RX_BUFFER_MASK;
        
        // Store character if buffer not full
        if (next_head != uart_rx_tail) {
            uart_rx_buffer[uart_rx_head] = ch;
            uart_rx_head = next_head;
        }
        // If buffer full, oldest data is discarded
    }
}

// Callback invoked by PIO UART DMA handler. Copies bytes into the ring buffer.
// When attached, pio UART DMA will write directly into uart_rx_buffer.
static void pio_rx_to_ringbuffer(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        uint8_t ch = data[i];
        uint16_t next_head = (uart_rx_head + 1) & UART_RX_BUFFER_MASK;
        if (next_head != uart_rx_tail) {
            uart_rx_buffer[uart_rx_head] = ch;
            uart_rx_head = next_head;
        } else {
            // Buffer full; drop oldest (same policy as IRQ handler)
            break;
        }
    }
}

// Get character from ring buffer (non-blocking)
static int uart_rx_getchar(void) {
    if (uart_rx_head == uart_rx_tail) {
        return -1; // Buffer empty
    }
    
    uint8_t ch = uart_rx_buffer[uart_rx_tail];
    uart_rx_tail = (uart_rx_tail + 1) & UART_RX_BUFFER_MASK;
    return ch;
}

// Peek for a full line in the ring buffer and copy it into dst (no terminator).
// Returns true if a full line was copied; out_len receives the length copied and
// term_len/term_buf are filled with the terminator bytes (if any).
static bool ringbuf_peek_line_and_copy(char *dst, size_t dst_size, size_t *out_len, char *term_buf, uint8_t *term_len)
{
    uint16_t head = uart_rx_head;
    uint16_t tail = uart_rx_tail;
    if (head == tail) return false; // empty

    // Scan from tail to head for a terminator
    uint16_t idx = tail;
    uint16_t found = UINT16_MAX;
    while (idx != head) {
        uint8_t ch = uart_rx_buffer[idx & UART_RX_BUFFER_MASK];
        if (ch == '\n' || ch == '\r') { found = idx; break; }
        idx = (idx + 1) & UART_RX_BUFFER_MASK;
    }
    if (found == UINT16_MAX) return false; // no full line

    // Determine terminator length (handle \r\n)
    uint8_t tlen = 1;
    char tbuf[2] = { (char)uart_rx_buffer[found & UART_RX_BUFFER_MASK], 0 };
    // if \r and next is \n, consider two-byte terminator
    if (tbuf[0] == '\r') {
        uint16_t next = (found + 1) & UART_RX_BUFFER_MASK;
        if (next != head && uart_rx_buffer[next] == '\n') {
            tbuf[1] = '\n';
            tlen = 2;
        }
    }

    // Compute line length (exclude terminator)
    size_t line_len = 0;
    uint16_t scan = tail;
    while (scan != found) { line_len++; scan = (scan + 1) & UART_RX_BUFFER_MASK; }

    // Truncate if necessary
    if (line_len >= dst_size) line_len = dst_size - 1;

    // Copy possibly wrapped data
    uint16_t first_chunk = UART_RX_BUFFER_SIZE - (tail & UART_RX_BUFFER_MASK);
    if (first_chunk > line_len) first_chunk = line_len;
    memcpy(dst, (const void *)&uart_rx_buffer[tail & UART_RX_BUFFER_MASK], first_chunk);
    if (line_len > first_chunk) {
        memcpy(dst + first_chunk, (const void *)&uart_rx_buffer[0], line_len - first_chunk);
    }
    dst[line_len] = '\0';

    // Advance tail past the line and its terminator
    uint16_t new_tail = (found + tlen) & UART_RX_BUFFER_MASK;
    // Update shared tail atomically (single word write is atomic on RP2040)
    uart_rx_tail = new_tail;

    if (out_len) *out_len = line_len;
    if (term_len) *term_len = tlen;
    if (term_buf && tlen > 0) { term_buf[0] = tbuf[0]; if (tlen == 2) term_buf[1] = tbuf[1]; }
    return true;
}

// Initialize the serial handler
void kmbox_serial_init(void)
{
    bool pio_ok = false;
    printf("PIO UART disabled via KMBOX_ENABLE_PIO_UART - using hardware UART1 fallback\n");

    uart_init(KMBOX_UART, KMBOX_UART_BAUDRATE);
    
    // Set up GPIO pins for UART1
    gpio_set_function(KMBOX_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(KMBOX_UART_RX_PIN, GPIO_FUNC_UART);
    
    // Enable UART FIFOs for better performance
    uart_set_fifo_enabled(KMBOX_UART, true);
    
    // Set up UART RX interrupt for non-blocking reception
    int uart_irq = (KMBOX_UART == uart0) ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(uart_irq, on_uart_rx);
    irq_set_enabled(uart_irq, true);
    
    // Enable UART RX interrupt
    uart_set_irq_enables(KMBOX_UART, true, false);
    // Initialize the kmbox commands module
    kmbox_commands_init();
    
    printf("KMBox serial handler initialized on UART1 (TX: GPIO%d, RX: GPIO%d) @ %d baud\n",
           KMBOX_UART_TX_PIN, KMBOX_UART_RX_PIN, KMBOX_UART_BAUDRATE);
}

// Process any available serial input
void kmbox_serial_task(void)
{
    // Get current time
    uint32_t current_time_ms = to_ms_since_boot(get_absolute_time());
    // Fast path: check for a full line and hand it to parser in one call
    char linebuf[KMBOX_CMD_BUFFER_SIZE];
    size_t line_len = 0;
    char termbuf[2];
    uint8_t termlen = 0;
    while (ringbuf_peek_line_and_copy(linebuf, sizeof(linebuf), &line_len, termbuf, &termlen)) {
        kmbox_process_serial_line(linebuf, line_len, termbuf, termlen, current_time_ms);
    }

    // Fallback: process any remaining single bytes (partial line building)
    int c;
    while ((c = uart_rx_getchar()) != -1) {
        kmbox_process_serial_char((char)c, current_time_ms);
    }
    
    // Update button states (handles timing for releases)
    kmbox_update_states(current_time_ms);
}

// Send mouse report with kmbox button states
bool kmbox_send_mouse_report(void)
{
    // Check if USB is ready
    if (!tud_hid_ready()) {
        return false;
    }
    
    // Get the report data from kmbox commands
    uint8_t buttons;
    int8_t x, y, wheel, pan;
    kmbox_get_mouse_report(&buttons, &x, &y, &wheel, &pan);
    
    // Send the report using TinyUSB
    bool success = tud_hid_mouse_report(REPORT_ID_MOUSE, buttons, x, y, wheel, pan);
    
    if (success) {
        // Trigger rainbow effect periodically when KMBox commands are processed
        static uint32_t rainbow_counter = 0;
        if (++rainbow_counter % 50 == 0) {
            neopixel_trigger_rainbow_effect();
        }
    }
    
    return success;
}
