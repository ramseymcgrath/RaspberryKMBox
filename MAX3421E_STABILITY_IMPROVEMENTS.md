# MAX3421E Stability Improvements

## Summary of Changes

This document summarizes the stability improvements made to address MAX3421E boot issues.

### 1. Early Heartbeat LED Initialization

**File: PIOKMbox.c**
- Moved LED initialization to the very beginning of `main()` 
- Added heartbeat LED pattern during cold boot stabilization (toggles every 250ms)
- LED provides immediate visual feedback that the system is booting

### 2. Extended Stabilization Delays

**File: PIOKMbox.c**
- Added extra 1 second to `COLD_BOOT_STABILIZATION_MS` for MAX3421E
- Added 3-second stabilization delay specifically for MAX3421E before initialization
- Added heartbeat pattern during all long delays to show system is alive

**File: defines.h**
- Added MAX3421E-specific timing constants:
  - `MAX3421E_COLD_BOOT_DELAY_MS`: 3000ms extra delay for cold boot
  - `MAX3421E_INIT_RETRY_DELAY_MS`: 2000ms between init retries
  - `MAX3421E_VBUS_STABILIZATION_MS`: 500ms for VBUS stabilization
  - `MAX3421E_SPI_INIT_DELAY_MS`: 10ms after SPI init
  - `MAX3421E_REG_ACCESS_DELAY_US`: 1μs for register access

### 3. MAX3421E Initialization Improvements

**File: PIOKMbox.c**
- Added retry logic with up to 3 attempts for MAX3421E initialization
- Added register verification (checks revision register for 0x12 or 0x13)
- Added comprehensive register dump function for debugging
- Prints all key MAX3421E registers on failure and success
- Added VBUS verification after enabling

### 4. Background Retry Mechanism

**File: PIOKMbox.c**
- Added background retry in `core1_task_loop()` if initial init fails
- Retries every 5 seconds up to 10 times
- System continues to operate while retrying initialization
- Logs success when retry succeeds

### 5. SPI Communication Improvements

**File: max3421e_impl.c**
- Initialize CS pin first and ensure it's high before SPI init
- Start SPI at lower speed (4MHz) then increase to 20MHz (was 26MHz)
- Added explicit SPI format configuration
- Added microsecond delays around CS assertions
- Added delays between register accesses for stability

### 6. Debug Output Enhancements

- Early UART initialization for debug output
- Clear boot sequence messages showing each stage
- Register dumps on initialization failure
- Retry attempt logging with detailed status

## Usage

The system will now:
1. Show immediate LED heartbeat on power-up
2. Print boot sequence to UART early
3. Attempt MAX3421E initialization with retries
4. Continue operating even if initial init fails
5. Retry initialization in background
6. Print detailed debug info if issues occur

## Testing

To test the improvements:
1. Power cycle the device (cold boot)
2. Watch for LED heartbeat starting immediately
3. Monitor UART output for boot sequence
4. Check for MAX3421E register dumps if init fails
5. Verify system continues to retry in background

## Register Reference

Key MAX3421E registers printed in debug:
- `REVISION` (0x90): Should be 0x12 or 0x13
- `IOPINS1` (0xA0): GPIO state (bit 0 = VBUS)
- `IOPINS2` (0xA8): GPIO direction
- `HIRQ` (0xC8): Host interrupt flags
- `MODE` (0xD8): Operating mode
- `HCTL` (0xE8): Host control
- `HRSL` (0xF8): Host result