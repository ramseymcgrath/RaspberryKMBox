# Using Pico SDK Default Pin Definitions

## Overview

The Pico SDK provides default pin definitions for common peripherals based on the board type. By using these defaults, we can simplify our code and make it more portable across different Pico boards.

## SDK Defaults We're Using

### 1. **LED Pin** (`PICO_DEFAULT_LED_PIN`)
- **SDK Default**: Automatically set based on board
  - Pico: GPIO 25
  - Pico W: Connected to wireless chip
  - Feather boards: GPIO 13
- **Our Usage**: `PIN_LED` uses SDK default when available

### 2. **NeoPixel/WS2812 Pin** (`PICO_DEFAULT_WS2812_PIN`)
- **SDK Default**: Set for boards with built-in NeoPixel
  - Adafruit Feather RP2040: GPIO 16
  - Adafruit Feather RP2350: GPIO 21
- **Our Usage**: `PIN_NEOPIXEL` uses SDK default when available

### 3. **SPI Pins** (for MAX3421E)
- **SDK Defaults**:
  - `PICO_DEFAULT_SPI`: SPI instance (spi0 or spi1)
  - `PICO_DEFAULT_SPI_SCK_PIN`: Clock pin
  - `PICO_DEFAULT_SPI_TX_PIN`: MOSI pin
  - `PICO_DEFAULT_SPI_RX_PIN`: MISO pin
- **Our Usage**: MAX3421E implementation uses these defaults

### 4. **Button Pin** (`PICO_DEFAULT_BUTTON_PIN`)
- **SDK Default**: Not always defined (board-specific)
- **Our Usage**: Falls back to GPIO 7 if not defined

## How It Works

In `defines.h`, we check if the SDK has already defined these pins:

```c
// Use SDK default for LED
#ifndef PIN_LED
#ifdef PICO_DEFAULT_LED_PIN
#define PIN_LED                 PICO_DEFAULT_LED_PIN
#else
#define PIN_LED                 (13u)   // Fallback for Feather boards
#endif
#endif
```

## Board Configuration in CMake

The board type is set in CMakeLists.txt, which determines which SDK defaults are available:

```cmake
set(PICO_BOARD adafruit_feather_rp2350)  # Sets board-specific defaults
```

## Benefits

1. **Portability**: Code works across different Pico boards without modification
2. **Simplicity**: No need to manually define pins that the SDK already knows
3. **Correctness**: SDK defaults are tested and verified for each board
4. **Maintainability**: Fewer hardcoded values to maintain

## Custom Pins

Some pins remain custom-defined because they're specific to our hardware:
- `PIN_USB_HOST_DP/DM`: PIO USB host pins (GPIO 16/17)
- `PIN_USB_5V`: USB host power control (GPIO 18)
- `NEOPIXEL_POWER`: NeoPixel power control (GPIO 20)
- `MAX3421E_CS_PIN`: SPI chip select (GPIO 9)

## Building for Different Boards

The build script automatically sets the correct board type:

```bash
./build.sh rp2040              # Uses pico board defaults
./build.sh rp2350              # Uses pico2 board defaults
./build.sh rp2350_max3421e     # Uses adafruit_feather_rp2350 defaults
```

## Debugging Pin Assignments

To see which pins are being used, check the build output or add debug prints:

```c
printf("LED Pin: %d\n", PIN_LED);
printf("NeoPixel Pin: %d\n", PIN_NEOPIXEL);
#ifdef PICO_DEFAULT_SPI_SCK_PIN
printf("SPI SCK Pin: %d\n", PICO_DEFAULT_SPI_SCK_PIN);
#endif