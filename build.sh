#!/usr/bin/env bash

# ========================================================================
# TinyUSB Dual Host/Device HID Build Script for RP2040/RP2350
# ========================================================================

# Enable strict error handling
set -euo pipefail
IFS=$'\n\t'

# ========================================================================
# Configuration
# ========================================================================

# Check Bash version for associative array support
if ((BASH_VERSINFO[0] >= 4)); then
  # Bash 4+ supports associative arrays
  declare -A BOARD_CONFIGS=(
    [rp2040]="adafruit_feather_rp2040_usb_host"
    [rp2040_optimized]="adafruit_feather_rp2040_usb_host_optimized"
    [rp2350]="adafruit_feather_rp2350"
    [rp2350_xiao]="seeed_xiao_rp2350"
  )
  
  # Function to get board name
  get_board_name() {
    echo "${BOARD_CONFIGS[$1]}"
  }
else
  # Fallback for Bash 3 (macOS default)
  # Simple function to map chip to board name
  get_board_name() {
    case "$1" in
      "rp2040") echo "adafruit_feather_rp2040_usb_host" ;;
      "rp2040_optimized") echo "adafruit_feather_rp2040_usb_host_optimized" ;;
      "rp2350") echo "adafruit_feather_rp2350" ;;
      "rp2350_xiao") echo "seeed_xiao_rp2350" ;;
      *) echo "unknown" ;;
    esac
  }
fi

# Output file name
OUTPUT_FILE="PIOKMbox.uf2"

# Default settings
DEFAULT_TARGET="both"
DEFAULT_JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
UART_BAUD=115200
UART_TX_PIN=0
UART_RX_PIN=1

# Color codes for output formatting
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# ========================================================================
# Helper Functions
# ========================================================================

# Print a formatted header
print_header() {
  echo -e "\n${BOLD}${BLUE}==========================================${NC}"
  echo -e "${BOLD}${BLUE}$1${NC}"
  echo -e "${BOLD}${BLUE}==========================================${NC}"
}

# Print a success message
print_success() {
  echo -e "${GREEN}$1${NC}"
}

# Print an error message
print_error() {
  echo -e "${RED}$1${NC}" >&2
}

# Print a warning message
print_warning() {
  echo -e "${YELLOW}$1${NC}"
}

# Print usage information
usage() {
  cat << EOF
${BOLD}Usage:${NC} $0 [target] [options]

${BOLD}Targets:${NC}
  rp2040        - Build for RP2040 (Adafruit Feather RP2040 USB Host)
  rp2350        - Build for RP2350 (Adafruit Feather RP2350)
  rp2350_xiao   - Build for RP2350 (Seeed XIAO RP2350)
  both          - Build for both RP2040 and RP2350 Feather boards (default)

${BOLD}Options:${NC}
  --clean       - Clean build directories before building
  --jobs=N      - Set number of parallel build jobs (default: $DEFAULT_JOBS)
  --help, -h    - Display this help message

${BOLD}Examples:${NC}
  $0                    # Build both targets
  $0 rp2040             # Build only RP2040
  $0 rp2350 --clean     # Clean and build only RP2350
  $0 both --jobs=8      # Build both targets with 8 parallel jobs
EOF
  exit 1
}

# Check if a command exists
command_exists() {
  command -v "$1" &> /dev/null
}

# Check if a file exists
file_exists() {
  [[ -f "$1" ]]
}

# ========================================================================
# Build Functions
# ========================================================================

# Build a specific target
build_target() {
  local chip="$1"
  local board="$(get_board_name "$chip")"
  
  # Use build directory names that match VS Code tasks
  if [[ "$chip" == "rp2040" ]]; then
    local build_dir="build-pico"
  elif [[ "$chip" == "rp2350_xiao" ]]; then
    local build_dir="build-xiao-rp2350"
  else
    local build_dir="build-pico2"
  fi
  
  print_header "Building for $chip (${board})"
  
  # Clean build directory if requested
  if [[ "$CLEAN" == true ]]; then
    print_warning "Cleaning $build_dir directory..."
    rm -rf "$build_dir"
  fi
  
  # Create and enter build directory
  mkdir -p "$build_dir"
  pushd "$build_dir" > /dev/null || {
    print_error "Failed to enter directory $build_dir"
    return 1
  }
  
  # Configure with CMake
  echo "Configuring with CMake for $board..."
  if ! cmake .. -DTARGET_BOARD="$board"; then
    print_error "CMake configuration failed for $board!"
    popd > /dev/null || true
    return 1
  fi
  
  # Build the project
  echo "Building project for $board with $JOBS parallel jobs..."
  if ! make -j"$JOBS"; then
    print_error "Build failed for $board!"
    popd > /dev/null || true
    return 1
  fi
  
  print_success "Build successful for $board!"
  echo "Generated files in $build_dir:"
  
  # List output files
  if ! ls -la *.uf2 *.elf 2>/dev/null; then
    print_warning "No output files found"
  fi
  
  popd > /dev/null || true
  return 0
}

# Flash firmware to a device
flash_firmware() {
  local chip="$1"
  local board="$(get_board_name "$chip")"
  
  # Use build directory names that match VS Code tasks
  if [[ "$chip" == "rp2040" ]]; then
    local build_dir="build-pico"
  elif [[ "$chip" == "rp2350_xiao" ]]; then
    local build_dir="build-xiao-rp2350"
  else
    local build_dir="build-pico2"
  fi
  
  local firmware_path="$build_dir/$OUTPUT_FILE"
  
  print_header "Flashing $chip (${board}) firmware"
  
  # Check if firmware exists
  if ! file_exists "$firmware_path"; then
    print_error "Firmware file not found: $firmware_path"
    return 1
  fi
  
  # Check if picotool is available
  if ! command_exists picotool; then
    print_error "Picotool not found. Please install it to flash firmware."
    print_manual_flash_instructions "$chip"
    return 1
  fi
  
  # Try to flash the firmware
  echo "Attempting to flash with picotool..."
  if picotool load "$firmware_path" -fx; then
    print_success "Firmware flashed successfully for $chip!"
    print_success "Device should now be running the new $chip firmware."
    return 0
  else
    print_error "Picotool flash failed for $chip."
    print_manual_flash_instructions "$chip"
    return 1
  fi
}

# Print manual flashing instructions
print_manual_flash_instructions() {
  local chip="$1"
  
  # Use build directory names that match VS Code tasks
  if [[ "$chip" == "rp2040" ]]; then
    local build_dir="build-pico"
  elif [[ "$chip" == "rp2350_xiao" ]]; then
    local build_dir="build-xiao-rp2350"
  else
    local build_dir="build-pico2"
  fi
  
  cat << EOF

${BOLD}Manual flashing instructions for $chip:${NC}
1. Hold BOOTSEL button while connecting USB
2. Copy the firmware file to the RPI-RP2 drive:
   ${build_dir}/${OUTPUT_FILE}
3. The device will reboot and run the firmware

${BOLD}UART output available at ${UART_BAUD} baud on pins:${NC}
- TX: GPIO ${UART_TX_PIN}
- RX: GPIO ${UART_RX_PIN}
EOF
}

# ========================================================================
# Main Script
# ========================================================================

# Parse command line arguments
TARGET="$DEFAULT_TARGET"
CLEAN=false
JOBS="$DEFAULT_JOBS"

while [[ $# -gt 0 ]]; do
  case "$1" in
    rp2040|rp2350|rp2350_xiao|both)
      TARGET="$1"
      ;;
    --clean)
      CLEAN=true
      ;;
    --jobs=*)
      JOBS="${1#*=}"
      if ! [[ "$JOBS" =~ ^[0-9]+$ ]] || [[ "$JOBS" -lt 1 ]]; then
        print_error "Invalid job count: $JOBS. Must be a positive integer."
        usage
      fi
      ;;
    --help|-h)
      usage
      ;;
    *)
      print_error "Unknown argument: $1"
      usage
      ;;
  esac
  shift
done

# Validate target
if [[ "$TARGET" != "rp2040" && "$TARGET" != "rp2350" && "$TARGET" != "rp2350_xiao" && "$TARGET" != "both" ]]; then
  print_error "Invalid target: $TARGET"
  usage
fi

# Print build configuration
print_header "TinyUSB Dual Host/Device HID Build Script"
echo "Target: $TARGET"
echo "Parallel jobs: $JOBS"
[[ "$CLEAN" == true ]] && echo "Clean build requested"

# Initialize counters
SUCCESS_COUNT=0
TOTAL_COUNT=0

# Build targets
case "$TARGET" in
  "rp2040")
    TOTAL_COUNT=1
    if build_target "rp2040"; then
      SUCCESS_COUNT=1
      echo ""
      read -rp "Would you like to flash the RP2040 firmware? (y/n) " response
      if [[ "$response" =~ ^[Yy]$ ]]; then
        flash_firmware "rp2040"
      fi
    fi
    ;;
  "rp2350")
    TOTAL_COUNT=1
    if build_target "rp2350"; then
      SUCCESS_COUNT=1
      echo ""
      read -rp "Would you like to flash the RP2350 firmware? (y/n) " response
      if [[ "$response" =~ ^[Yy]$ ]]; then
        flash_firmware "rp2350"
      fi
    fi
    ;;
  "rp2350_xiao")
    TOTAL_COUNT=1
    if build_target "rp2350_xiao"; then
      SUCCESS_COUNT=1
      echo ""
      read -rp "Would you like to flash the Seeed XIAO RP2350 firmware? (y/n) " response
      if [[ "$response" =~ ^[Yy]$ ]]; then
        flash_firmware "rp2350_xiao"
      fi
    fi
    ;;
  "both")
    TOTAL_COUNT=2
    if build_target "rp2040"; then
      SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    fi
    if build_target "rp2350"; then
      SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    fi
    
    if [[ $SUCCESS_COUNT -gt 0 ]]; then
      echo ""
      echo "Available builds:"
      file_exists "build-pico/$OUTPUT_FILE" && echo "  - RP2040: build-pico/$OUTPUT_FILE"
      file_exists "build-pico2/$OUTPUT_FILE" && echo "  - RP2350: build-pico2/$OUTPUT_FILE"
      
      echo ""
      echo "Select which firmware to flash:"
      echo "  1) RP2040"
      echo "  2) RP2350"
      echo "  3) Skip flashing"
      read -r choice
      
      case $choice in
        1)
          if file_exists "build-pico/$OUTPUT_FILE"; then
            flash_firmware "rp2040"
          else
            print_error "RP2040 build not available"
          fi
          ;;
        3)
          if file_exists "build-pico2/$OUTPUT_FILE"; then
            flash_firmware "rp2350"
          else
            print_error "RP2350 build not available"
          fi
          ;;
        *)
          echo "Skipping flash"
          ;;
      esac
    fi
    ;;
esac

# Print build summary
print_header "Build Summary"
echo "Successfully built: $SUCCESS_COUNT/$TOTAL_COUNT targets"

if [[ $SUCCESS_COUNT -eq 0 ]]; then
  print_error "All builds failed!"
  exit 1
elif [[ $SUCCESS_COUNT -lt $TOTAL_COUNT ]]; then
  print_warning "Some builds failed. Check the output above for details."
  exit 1
else
  print_success "All builds completed successfully!"
fi

# Print manual flashing instructions if any builds succeeded
if [[ $SUCCESS_COUNT -gt 0 ]]; then
  print_header "Manual Flashing Instructions"
  echo "1. Hold BOOTSEL button while connecting USB"
  echo "2. Copy the appropriate .uf2 file to the RPI-RP2 drive:"
  file_exists "build-pico/$OUTPUT_FILE" && echo "   - For RP2040: build-pico/$OUTPUT_FILE"
  file_exists "build-pico2/$OUTPUT_FILE" && echo "   - For RP2350: build-pico2/$OUTPUT_FILE"
  echo "3. The device will reboot and run the firmware"
  
  echo ""
  echo "UART output available at ${UART_BAUD} baud on pins:"
  echo "- TX: GPIO ${UART_TX_PIN}"
  echo "- RX: GPIO ${UART_RX_PIN}"
fi

exit 0