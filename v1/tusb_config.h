/*
 * The MIT License (MIT)
 * TinyUSB Configuration for Dual USB Stack (Device + Host)
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

#define CFG_TUSB_OS               OPT_OS_PICO

// CRITICAL: Enable device stack on native USB controller (port 0) ONLY
#define CFG_TUD_ENABLED           1

// CRITICAL: Enable host stack with pio-usb on separate controller (port 1) ONLY
#define CFG_TUH_ENABLED           1
#define CFG_TUH_RPI_PIO_USB       1

// CRITICAL: Enable dual mode support with strict separation
#define CFG_TUSB_RHPORT0_MODE     OPT_MODE_DEVICE
#define CFG_TUSB_RHPORT1_MODE     OPT_MODE_HOST

// IMPROVED: Enable debugging for descriptor issues
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG            0  // Disable debug output
#endif

// IMPROVED: Disable logging for production use
#define CFG_TUSB_DEBUG_PRINTF     printf
#define CFG_TUD_LOG_LEVEL         0  // Disable device logging
#define CFG_TUH_LOG_LEVEL         0  // Disable host logging

/* USB DMA on some MCUs can only access a specific SRAM region with restriction on alignment.
 * Tinyusb use follows macros to declare transferring memory so that they can be put
 * into those specific section.
 */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN        __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION (Port 0 - Native USB)
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

// IMPROVED: Larger endpoint 0 buffer for better descriptor handling
#define CFG_TUD_EP0_BUFSIZE       256

//------------- CLASS -------------//
#define CFG_TUD_HID               1

// HID buffer size - should be sufficient to hold ID (if any) + Data
#define CFG_TUD_HID_EP_BUFSIZE    64

//--------------------------------------------------------------------
// HOST CONFIGURATION (Port 1 - PIO USB)
//--------------------------------------------------------------------

// IMPROVED: Larger enumeration buffer for complex device descriptors
#define CFG_TUH_ENUMERATION_BUFSIZE 512  // Increased from 256

// IMPROVED: Hub support with reasonable device limits
#define CFG_TUH_HUB               1
#define CFG_TUH_DEVICE_MAX        (CFG_TUH_HUB ? 4 : 1) // hub typically has 4 ports

// IMPROVED: HID configuration with larger buffers
#define CFG_TUH_HID               4
#define CFG_TUH_HID_EPIN_BUFSIZE  128  // Increased from 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 128  // Increased from 64

// IMPROVED: Control transfer timeout and retry settings
#define CFG_TUH_API_EDPT_XFER     1    // Enable asynchronous transfers

// IMPROVED: Endpoint configuration
#define CFG_TUH_ENUMERATION_BUFSIZE 512  // Ensure this is defined only once
#define CFG_TUH_EP0_BUFSIZE       256    // Larger control endpoint buffer

// IMPROVED: Memory management
#define CFG_TUH_MEM_SECTION       CFG_TUSB_MEM_SECTION
#define CFG_TUH_MEM_ALIGN         CFG_TUSB_MEM_ALIGN

//--------------------------------------------------------------------
// DESCRIPTOR RETRIEVAL OPTIMIZATION
//--------------------------------------------------------------------

// These help with descriptor retrieval timing and reliability
#define CFG_TUH_DEVICE_DEV_DESC_SIZE    18    // Standard device descriptor size
#define CFG_TUH_DEVICE_CONFIG_DESC_SIZE 255   // Max config descriptor size

//--------------------------------------------------------------------
// PIO USB SPECIFIC OPTIMIZATIONS
//--------------------------------------------------------------------

// IMPROVED: PIO USB timing optimizations
#ifdef CFG_TUH_RPI_PIO_USB
    // Larger buffers help with PIO USB timing issues
    #undef CFG_TUH_ENUMERATION_BUFSIZE
    #define CFG_TUH_ENUMERATION_BUFSIZE 1024  // Even larger for PIO USB

    // More generous timeouts for PIO USB
    #define CFG_TUH_TASK_QUEUE_SZ     16      // Larger task queue
#endif

//--------------------------------------------------------------------
// TIMING AND RELIABILITY SETTINGS
//--------------------------------------------------------------------

// IMPROVED: Enable control transfer validation
#define CFG_TUH_STRICT_CONTROL_XFER   0    // Allow more flexible control transfers

// IMPROVED: Memory pool configuration for better descriptor handling
#define CFG_TUH_MEMPOOL_SZ        2048     // Larger memory pool

//--------------------------------------------------------------------
// DEBUG AND LOGGING CONFIGURATION
//--------------------------------------------------------------------

// Temporary debug settings to help identify descriptor issues
// TODO: Reduce these in production builds
#ifdef DEBUG_DESCRIPTORS
    #undef CFG_TUD_LOG_LEVEL
    #undef CFG_TUH_LOG_LEVEL
    #define CFG_TUD_LOG_LEVEL     0    // Disable device logging even in debug mode
    #define CFG_TUH_LOG_LEVEL     0    // Disable host logging even in debug mode
    
    #define CFG_TUSB_DEBUG_PRINTF printf
#endif

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */