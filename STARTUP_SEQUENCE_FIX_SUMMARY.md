# PIOKMBox Startup Sequence Fix Summary

## Issues Identified and Fixed

### 1. **Missing LED Heartbeat Implementation**
- **Problem**: The onboard LED (GPIO 13) was not blinking as a heartbeat indicator
- **Fix**: Added heartbeat logic in the main application loop with 500ms interval
- **Files Modified**: 
  - `PIOKMbox.c`: Added heartbeat state tracking and toggle logic
  - `defines.h`: Added `LED_HEARTBEAT_INTERVAL_MS` constant

### 2. **Serial Output Loss After Clock Change**
- **Problem**: Serial output was lost due to multiple stdio reinitializations after clock changes
- **Fix**: Improved initialization sequence with proper delays and single clock change
- **Files Modified**: 
  - `PIOKMbox.c`: Fixed stdio initialization sequence with proper delays

### 3. **Incorrect 5V Power Control Logic**
- **Problem**: 5V power control was not properly conditioned on platform and USB host mode
- **Fix**: Added proper conditional compilation for each configuration:
  - **RP2040 + PIO USB**: Controls GPIO 18 for 5V power
  - **RP2350 + PIO USB**: No 5V control needed (plain USB)
  - **RP2350 + MAX3421E**: VBUS controlled via MAX3421E GPIO0
- **Files Modified**: 
  - `PIOKMbox.c`: Fixed power control initialization
  - `usb_hid_init.c`: Updated `usb_host_enable_power()` and `usb_host_stack_reset()`

### 4. **NeoPixel Power Sequencing Issues**
- **Problem**: NeoPixel power was enabled too early, causing initialization failures
- **Fix**: Proper power sequencing with delays and initialization order
- **Files Modified**: 
  - `led_control.c`: Fixed `neopixel_init()` and `neopixel_enable_power()`

### 5. **Missing Platform Detection Logging**
- **Problem**: No clear indication of which platform/configuration was running
- **Fix**: Added platform detection logging during startup
- **Files Modified**: 
  - `PIOKMbox.c`: Added platform and USB host mode logging

## Startup Sequence Overview

### All Platforms:
1. **GPIO Initialization** (immediate)
   - Onboard LED configured and starts heartbeat
   - Platform-specific 5V control pins configured (but kept OFF)
   - NeoPixel power pin configured (but kept OFF)

2. **Cold Boot Delay** (2 seconds)
   - Allows hardware to stabilize

3. **Clock Configuration** (120MHz)
   - System clock set to 120MHz for PIO USB
   - UART reinitialized after clock change

4. **System Initialization**
   - USB HID module initialized
   - DMA manager initialized
   - Watchdog initialized (but not started)

5. **USB Device Stack** (Core 0)
   - USB device initialized on native USB controller

6. **NeoPixel Initialization**
   - Power enabled after device stack is ready
   - Shows blue "booting" status

7. **USB Host Power Enable**
   - Platform-specific power control activated

8. **Core 1 Launch**
   - USB host tasks start on Core 1

9. **Main Loop**
   - LED heartbeat active
   - NeoPixel shows system status
   - All USB functionality operational

## Platform-Specific Behaviors

### RP2040 with PIO USB
- Controls GPIO 18 for 5V USB host power
- Power starts LOW and goes HIGH after boot sequence
- Uses PIO state machines for USB host

### RP2350 with PIO USB  
- No 5V power control (uses plain USB)
- Enhanced hardware acceleration available
- Better DMA performance

### RP2350 with MAX3421E
- VBUS controlled via MAX3421E's GPIO0
- SPI communication with MAX3421E chip
- External USB host controller

## Testing Instructions

1. Build the firmware:
   ```bash
   ./build.sh [rp2040|rp2350|rp2350_max3421e]
   ```

2. Flash the firmware:
   - Hold BOOTSEL while connecting USB
   - Copy the .uf2 file to RPI-RP2 drive

3. Monitor serial output:
   ```bash
   screen /dev/tty.usbmodem* 115200
   ```

4. Verify startup sequence:
   - [ ] Onboard LED starts blinking immediately
   - [ ] Serial output appears with platform detection
   - [ ] NeoPixel shows blue after ~3 seconds
   - [ ] 5V power enables at appropriate time
   - [ ] System reaches ready state

## NeoPixel Status Colors
- **Blue**: Booting
- **Green**: USB device only
- **Orange**: USB host only  
- **Cyan**: Both USB device and host active
- **Magenta**: Mouse connected
- **Yellow**: Keyboard connected
- **Pink**: Both mouse and keyboard connected
- **Red**: Error state
- **Purple**: USB suspended

## Troubleshooting

If serial output is still missing:
1. Check UART connections (TX: GPIO 0, RX: GPIO 1)
2. Verify baud rate is 115200
3. Try different terminal program
4. Check if onboard LED is blinking (indicates firmware is running)

If LEDs are not working:
1. Verify power connections
2. Check if GPIO pins match your hardware
3. Ensure NeoPixel power pin (GPIO 20) is connected
4. Verify WS2812 data pin (GPIO 21) connection