#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/vreg.h"

#include "defines.h"
#include "pio_usb.h"
#include "tusb.h"

// Board-specific defines
#include "boards/pico.h"

// Neopixel PIO program
#include "ws2812.pio.h"

// Configuration
#define FIFO_DEPTH 64
#define NEOPIXEL_PIN PICO_DEFAULT_WS2812_PIN

// Compile-time clock validation
#define SYS_CLK_MHZ 120
#if ((SYS_CLK_MHZ % 120) != 0)
#error "System clock must be 120 or 240 MHz for PIO-USB timing"
#endif

// Mouse report structure
typedef struct {
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
    int8_t pan;
} __attribute__((packed)) mouse_report_t;

// FIFO buffer entry
typedef struct {
    mouse_report_t report;
    uint32_t timestamp;
    bool valid;
} fifo_entry_t;

// Global state with inter-core synchronization
static struct {
    queue_t report_queue;
    fifo_entry_t queue_storage[FIFO_DEPTH];
    
    // Mouse device info (protected by multicore lock)
    volatile uint16_t mouse_vid;
    volatile uint16_t mouse_pid;
    volatile uint8_t mouse_bInterval;
    volatile bool mouse_connected;
    volatile uint8_t mouse_dev_addr;
    volatile uint8_t mouse_instance;
    volatile bool needs_reenumeration;
    
    // Timing
    absolute_time_t last_poll_time;
    absolute_time_t last_send_time;
    volatile uint32_t poll_interval_us;
    
    // Neopixel (moved to PIO1)
    PIO pio;
    uint sm;
    
    // Device enumeration state (protected by multicore lock)
    volatile bool device_mounted;
    volatile bool host_ready;
    
    // Last valid report for rate limiting
    mouse_report_t last_report;
    bool have_last_report;
} g_state = {0};

// Neopixel colors
#define COLOR_OFF     0x000000
#define COLOR_INIT    0x000080  // Blue - initializing
#define COLOR_HOST    0x008000  // Green - host ready
#define COLOR_DEVICE  0x800000  // Red - device mode
#define COLOR_ACTIVE  0x808000  // Yellow - active passthrough
#define COLOR_ERROR   0x800080  // Purple - error

// Function prototypes
static void core1_main(void);
static void init_neopixel(void);
static void set_neopixel_color(uint32_t color);
static void update_status_led(void);
static void force_device_reenumeration(void);
static void safe_set_mouse_info(uint16_t vid, uint16_t pid, uint8_t interval, bool connected, uint32_t poll_interval_us);
static bool safe_get_mouse_connected(void);
static bool safe_get_device_mounted(void);
static void safe_set_device_mounted(bool mounted);
static void safe_set_host_ready(bool ready);
static uint32_t safe_get_poll_interval_us(void);

//--------------------------------------------------------------------+
// Inter-core synchronization helpers
//--------------------------------------------------------------------+

static void safe_set_mouse_info(uint16_t vid, uint16_t pid, uint8_t interval, bool connected, uint32_t poll_interval_us) {
    multicore_lockout_start_blocking();
    g_state.mouse_vid = vid;
    g_state.mouse_pid = pid;
    g_state.mouse_bInterval = interval;
    g_state.mouse_connected = connected;
    g_state.poll_interval_us = poll_interval_us;
    if (connected && g_state.device_mounted) {
        g_state.needs_reenumeration = true;
    }
    multicore_lockout_end_blocking();
    // Keep __sev() here since this is the critical sync point for timing
    __sev();
}

static bool safe_get_mouse_connected(void) {
    multicore_lockout_start_blocking();
    bool connected = g_state.mouse_connected;
    multicore_lockout_end_blocking();
    return connected;
}

static bool safe_get_device_mounted(void) {
    multicore_lockout_start_blocking();
    bool mounted = g_state.device_mounted;
    multicore_lockout_end_blocking();
    return mounted;
}

static void safe_set_device_mounted(bool mounted) {
    multicore_lockout_start_blocking();
    g_state.device_mounted = mounted;
    multicore_lockout_end_blocking();
}

static void safe_set_host_ready(bool ready) {
    multicore_lockout_start_blocking();
    g_state.host_ready = ready;
    multicore_lockout_end_blocking();
}

static uint32_t safe_get_poll_interval_us(void) {
    multicore_lockout_start_blocking();
    uint32_t interval = g_state.poll_interval_us;
    multicore_lockout_end_blocking();
    return interval;
}

//--------------------------------------------------------------------+
// Device Stack (Core 0)
//--------------------------------------------------------------------+

// Device descriptors - will be updated with mouse VID/PID
static tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x2E8A,  // Default Raspberry Pi Foundation
    .idProduct          = 0x0003,  // Default Pico
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

enum {
    ITF_NUM_HID = 0,
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define EPNUM_HID_IN        0x81

// Mouse HID report descriptor (uses report ID 1)
static uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_MOUSE()
};

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_MOUSE, sizeof(desc_hid_report), EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 1)
};

// String descriptors
static char const* string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 }, // 0: supported language is English (0x0409)
    "Mouse Passthrough",           // 1: Manufacturer
    "USB Mouse Relay",             // 2: Product
    "123456789",                   // 3: Serials
};

// Device callbacks
uint8_t const* tud_descriptor_device_cb(void) {
    // Update VID/PID from connected mouse
    if (safe_get_mouse_connected()) {
        multicore_lockout_start_blocking();
        desc_device.idVendor = g_state.mouse_vid;
        desc_device.idProduct = g_state.mouse_pid;
        multicore_lockout_end_blocking();
    }
    return (uint8_t const*)&desc_device;
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    // Update bInterval in configuration if we have mouse info
    if (safe_get_mouse_connected() && g_state.mouse_bInterval > 0) {
        // Create a modified descriptor with the correct bInterval
        static uint8_t modified_config[CONFIG_TOTAL_LEN];
        memcpy(modified_config, desc_configuration, CONFIG_TOTAL_LEN);
        // Update the HID endpoint bInterval (last byte of HID descriptor)
        modified_config[CONFIG_TOTAL_LEN - 1] = g_state.mouse_bInterval;
        return modified_config;
    }
    return desc_configuration;
}

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    
    static uint16_t _desc_str[32];
    uint8_t chr_count;
    
    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (!(index < sizeof(string_desc_arr)/sizeof(string_desc_arr[0]))) return NULL;
        
        const char* str = string_desc_arr[index];
        chr_count = strlen(str);
        if (chr_count > 31) chr_count = 31;
        
        for(uint8_t i=0; i<chr_count; i++) {
            _desc_str[1+i] = str[i];
        }
    }
    
    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2*chr_count + 2);
    return _desc_str;
}

// HID callbacks
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len) {
    (void)instance;
    (void)report;
    (void)len;
    // Report sent successfully
}

uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return desc_hid_report;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

// Force device re-enumeration
static void force_device_reenumeration(void) {
    printf("Forcing device re-enumeration with new VID/PID\n");
    
    // Disconnect from USB
    tud_disconnect();
    sleep_ms(100);
    
    // Reconnect with new descriptors
    tud_connect();
    
    multicore_lockout_start_blocking();
    g_state.needs_reenumeration = false;
    multicore_lockout_end_blocking();
}

// Device mount/unmount callbacks
void tud_mount_cb(void) {
    safe_set_device_mounted(true);
    printf("Device mounted to PC\n");
    update_status_led();
}

void tud_umount_cb(void) {
    safe_set_device_mounted(false);
    printf("Device unmounted from PC\n");
    update_status_led();
}

//--------------------------------------------------------------------+
// Host Stack (Core 1)
//--------------------------------------------------------------------+

// Core 1 main function - Host stack
static void core1_main(void) {
    sleep_ms(10);
    
    printf("Core 1: Initializing USB host stack\n");
    
    // Configure PIO USB for host stack
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    
    // Initialize host stack on roothub port 1 (PIO USB)
    tuh_init(1);
    
    safe_set_host_ready(true);
    printf("Core 1: USB host stack ready\n");
    update_status_led();
    
    // Main host loop
    while (true) {
        tuh_task(); // TinyUSB host task
        tight_loop_contents();
    }
}

// Host HID callbacks
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    
    if (itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
        printf("Mouse mounted: dev_addr=%d, instance=%d\n", dev_addr, instance);
        
        // Get VID/PID
        uint16_t vid, pid;
        tuh_vid_pid_get(dev_addr, &vid, &pid);
        
        printf("Mouse VID:PID = %04x:%04x\n", vid, pid);
        
        // Get poll interval using TinyUSB API if available
        uint8_t poll_interval_ms = 1; // Default fallback
        
        #if (CFG_TUSB_VERSION_MAJOR > 0) || \
            ((CFG_TUSB_VERSION_MAJOR == 0) && (CFG_TUSB_VERSION_MINOR >= 18))
        // TinyUSB 0.18+ has this function
        poll_interval_ms = tuh_hid_get_poll_interval_ms(dev_addr, instance);
        printf("Got poll interval from TinyUSB API: %d ms\n", poll_interval_ms);
        #else
        // Fallback: parse endpoint descriptor from interface descriptor
        printf("Using fallback method to get poll interval\n");
        // Note: desc_report is HID report descriptor, not config descriptor
        // For older TinyUSB versions, we'd need to request config descriptor separately
        // For now, use default 1ms
        #endif
        
        printf("Using mouse poll interval: %d ms\n", poll_interval_ms);
        
        uint32_t poll_interval_us = poll_interval_ms * 1000;
        g_state.last_poll_time = get_absolute_time();
        
        // Update mouse info with inter-core safety
        multicore_lockout_start_blocking();
        g_state.mouse_dev_addr = dev_addr;
        g_state.mouse_instance = instance;
        multicore_lockout_end_blocking();
        
        safe_set_mouse_info(vid, pid, poll_interval_ms, true, poll_interval_us);
        
        update_status_led();
        
        // Start receiving reports
        if (!tuh_hid_receive_report(dev_addr, instance)) {
            printf("Error: cannot request mouse reports\n");
        }
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (dev_addr == g_state.mouse_dev_addr && instance == g_state.mouse_instance) {
        printf("Mouse unmounted\n");
        
        safe_set_mouse_info(0, 0, 0, false, 0);
        
        multicore_lockout_start_blocking();
        g_state.mouse_dev_addr = 0;
        g_state.mouse_instance = 0;
        multicore_lockout_end_blocking();
        
        // Clear the FIFO
        while (!queue_is_empty(&g_state.report_queue)) {
            fifo_entry_t dummy;
            queue_try_remove(&g_state.report_queue, &dummy);
        }
        
        g_state.have_last_report = false;
        
        update_status_led();
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    
    if (itf_protocol == HID_ITF_PROTOCOL_MOUSE && 
        dev_addr == g_state.mouse_dev_addr && 
        instance == g_state.mouse_instance) {
        
        // Respect bInterval timing
        absolute_time_t current_time = get_absolute_time();
        if (g_state.poll_interval_us > 0 && 
            absolute_time_diff_us(g_state.last_poll_time, current_time) < g_state.poll_interval_us) {
            // Too early, but still continue to receive next report
            if (!tuh_hid_receive_report(dev_addr, instance)) {
                printf("Error: cannot request next mouse report\n");
            }
            return;
        }
        g_state.last_poll_time = current_time;
        
        // Parse mouse report (standard 3-5 byte format)
        if (len >= 3) {
            fifo_entry_t entry = {0};
            
            // Use memcpy instead of DMA for small reports
            uint8_t temp_report[8]; // Standard HID mouse report size
            memcpy(temp_report, report, len);
            
            // Parse copied data
            entry.report.buttons = temp_report[0];
            entry.report.x = (int8_t)temp_report[1];
            entry.report.y = (int8_t)temp_report[2];
            if (len > 3) {
                entry.report.wheel = (int8_t)temp_report[3];
            }
            if (len > 4) {
                entry.report.pan = (int8_t)temp_report[4];
            }
            entry.timestamp = to_us_since_boot(current_time);
            entry.valid = true;
            
            // Store for rate limiting
            g_state.last_report = entry.report;
            g_state.have_last_report = true;
            
            // Add to FIFO queue (inter-core communication)
            if (!queue_try_add(&g_state.report_queue, &entry)) {
                // FIFO full, remove oldest and add new
                fifo_entry_t dummy;
                queue_try_remove(&g_state.report_queue, &dummy);
                queue_try_add(&g_state.report_queue, &entry);
            }
        }
    }
    
    // Continue to receive reports
    if (!tuh_hid_receive_report(dev_addr, instance)) {
        printf("Error: cannot request next report\n");
    }
}

//--------------------------------------------------------------------+
// Neopixel Implementation (Fixed: Moved to PIO1)
//--------------------------------------------------------------------+

static void init_neopixel(void) {
    // Initialize power pin if available
    #ifdef PICO_DEFAULT_WS2812_POWER_PIN
    gpio_init(PICO_DEFAULT_WS2812_POWER_PIN);
    gpio_set_dir(PICO_DEFAULT_WS2812_POWER_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_WS2812_POWER_PIN, 1);
    sleep_ms(1);
    #endif
    
    // Fixed: Use PIO1 to avoid conflict with pio_usb on PIO0
    g_state.pio = pio1;
    g_state.sm = 0;
    
    uint offset = pio_add_program(g_state.pio, &ws2812_program);
    ws2812_program_init(g_state.pio, g_state.sm, offset, NEOPIXEL_PIN, 800000, false);
    
    set_neopixel_color(COLOR_INIT);
}

static void set_neopixel_color(uint32_t color) {
    pio_sm_put_blocking(g_state.pio, g_state.sm, color << 8u);
}

static void update_status_led(void) {
    if (!g_state.host_ready) {
        set_neopixel_color(COLOR_INIT);
    } else if (safe_get_mouse_connected() && safe_get_device_mounted()) {
        set_neopixel_color(COLOR_ACTIVE);
    } else if (safe_get_mouse_connected()) {
        set_neopixel_color(COLOR_HOST);
    } else if (safe_get_device_mounted()) {
        set_neopixel_color(COLOR_DEVICE);
    } else {
        set_neopixel_color(COLOR_INIT);
    }
}

//--------------------------------------------------------------------+
// Main Application
//--------------------------------------------------------------------+

int main(void) {
    // System clock validation using compile-time constant
    uint32_t sys_clock_hz = SYS_CLK_MHZ * 1000000; // Convert MHz to Hz
    
    // Optional: Support 240 MHz with higher voltage
    uint32_t vreg_voltage = VREG_VOLTAGE_1_10;
    if (SYS_CLK_MHZ == 240) {
        vreg_voltage = VREG_VOLTAGE_1_20;
        printf("Setting VREG to 1.20V for 240 MHz operation\n");
    }
    
    vreg_set_voltage(vreg_voltage);
    sleep_ms(10);
    
    // Set system clock (required for USB timing)
    set_sys_clock_khz(SYS_CLK_MHZ * 1000, true);
    
    sleep_ms(10);
    
    // Validate system clock
    uint32_t actual_clock = clock_get_hz(clk_sys);
    if (actual_clock != sys_clock_hz) {
        printf("ERROR: System clock is %lu Hz, expected %lu Hz (%d MHz)\n", 
               actual_clock, sys_clock_hz, SYS_CLK_MHZ);
        printf("USB timing will be incorrect!\n");
        panic("Invalid system clock frequency");
    }
    
    // Initialize stdio
    stdio_init_all();
    
    printf("\n=== USB Mouse Passthrough Starting ===\n");
    printf("System clock: %lu Hz (%d MHz validated)\n", actual_clock, SYS_CLK_MHZ);
    
    // Initialize Neopixel (now on PIO1)
    init_neopixel();
    
    // Initialize FIFO queue for inter-core communication using pre-allocated storage
    queue_init(&g_state.report_queue, sizeof(fifo_entry_t), FIFO_DEPTH);
    g_state.report_queue.data = (uint8_t*)g_state.queue_storage;
    
    // Initialize timing
    g_state.last_send_time = get_absolute_time();
    
    // Reset and start core1 for host stack
    multicore_reset_core1();
    multicore_launch_core1(core1_main);
    
    // Initialize device stack on native USB (roothub port 0)
    tud_init(0);
    
    printf("Core 0: Device stack initialized\n");
    printf("Waiting for mouse connection...\n");
    
    // Main device loop (Core 0)
    while (true) {
        tud_task(); // TinyUSB device task
        
        // Check if re-enumeration is needed
        multicore_lockout_start_blocking();
        bool needs_reenumeration = g_state.needs_reenumeration;
        multicore_lockout_end_blocking();
        
        if (needs_reenumeration) {
            force_device_reenumeration();
        }
        
        // Fixed: Outbound rate limiting - respect mouse poll interval
        absolute_time_t current_time = get_absolute_time();
        bool can_send = true;
        
        uint32_t poll_interval_us = safe_get_poll_interval_us();
        if (poll_interval_us > 0) {
            uint64_t time_since_last_send = absolute_time_diff_us(g_state.last_send_time, current_time);
            can_send = (time_since_last_send >= poll_interval_us);
        }
        
        // Process reports from FIFO and send to PC
        if (can_send && tud_hid_ready()) {
            fifo_entry_t entry;
            bool have_report = queue_try_remove(&g_state.report_queue, &entry);
            
            if (have_report && entry.valid) {
                // Send new mouse report to PC
                // Fixed: Use correct report ID (1 for TUD_HID_REPORT_DESC_MOUSE)
                bool sent = tud_hid_mouse_report(1, 
                                               entry.report.buttons,
                                               entry.report.x, 
                                               entry.report.y,
                                               entry.report.wheel, 
                                               entry.report.pan);
                
                if (sent) {
                    g_state.last_send_time = current_time;
                } else {
                    printf("Warning: Failed to send mouse report\n");
                }
            } else if (g_state.have_last_report && safe_get_mouse_connected()) {
                // Fixed: Repeat last report when queue is empty (real mice do this)
                bool sent = tud_hid_mouse_report(1,
                                               g_state.last_report.buttons,
                                               g_state.last_report.x,
                                               g_state.last_report.y,
                                               g_state.last_report.wheel,
                                               g_state.last_report.pan);
                
                if (sent) {
                    g_state.last_send_time = current_time;
                }
            }
        }
        
        tight_loop_contents();
    }
    
    return 0;
}