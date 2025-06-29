#if !defined(DEFINES_H)
#define DEFINES_H

#define FIFO_DEPTH 64
#define MAX_REPORT_SIZE 8
#ifndef PICO_DEFAULT_WS2812_PIN
#define PICO_DEFAULT_WS2812_PIN 21  // Default Neopixel pin for RP2040/RP2350
#endif
#ifndef NEOPIXEL_PIN
#define NEOPIXEL_PIN PICO_DEFAULT_WS2812_PIN
#endif

#endif