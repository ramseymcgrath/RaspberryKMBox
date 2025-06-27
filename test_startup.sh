#!/bin/bash

# Test script to verify startup sequence for all three firmware variants

echo "=== PIOKMBox Startup Sequence Test ==="
echo ""

# Function to test a specific build
test_build() {
    local target=$1
    local description=$2
    
    echo "Testing $description..."
    echo "Building firmware..."
    
    if ./build.sh $target --clean; then
        echo "✓ Build successful for $target"
        echo ""
        echo "Expected startup behavior:"
        echo "1. Onboard LED should start blinking immediately (heartbeat)"
        echo "2. Serial output should appear on UART (TX: GPIO 0, RX: GPIO 1)"
        echo "3. NeoPixel should show blue (booting) color after ~3 seconds"
        echo "4. 5V power control:"
        
        case $target in
            "rp2040")
                echo "   - GPIO 18 should start LOW and go HIGH after boot"
                ;;
            "rp2350")
                echo "   - No 5V control (plain USB)"
                ;;
            "rp2350_max3421e")
                echo "   - VBUS controlled via MAX3421E GPIO0"
                ;;
        esac
        
        echo ""
        echo "5. After full initialization, NeoPixel color indicates status:"
        echo "   - Green: USB device only"
        echo "   - Orange: USB host only"
        echo "   - Cyan: Both active"
        echo "   - Magenta: Mouse connected"
        echo "   - Yellow: Keyboard connected"
        echo "   - Pink: Both HID devices connected"
        echo ""
    else
        echo "✗ Build failed for $target"
        return 1
    fi
}

# Test all three variants
echo "=== Testing RP2040 with PIO USB ==="
test_build "rp2040" "RP2040 with PIO USB"

echo "=== Testing RP2350 with PIO USB ==="
test_build "rp2350" "RP2350 with PIO USB"

echo "=== Testing RP2350 with MAX3421E ==="
test_build "rp2350_max3421e" "RP2350 with MAX3421E"

echo ""
echo "=== Summary of Changes ==="
echo "1. Added LED heartbeat in main loop (500ms interval)"
echo "2. Fixed serial output initialization sequence"
echo "3. Corrected 5V power control logic:"
echo "   - RP2040 + PIO USB: Controls GPIO 18"
echo "   - RP2350 + PIO USB: No control needed"
echo "   - RP2350 + MAX3421E: VBUS via MAX3421E GPIO0"
echo "4. Fixed NeoPixel power sequencing"
echo "5. Added platform detection logging"
echo ""
echo "To monitor serial output:"
echo "  screen /dev/tty.usbmodem* 115200"
echo "  or"
echo "  minicom -D /dev/tty.usbmodem* -b 115200"