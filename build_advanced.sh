#!/bin/bash
# Advanced build script for PIOKMbox project
# Supports multiple boards and build configurations

set -e  # Exit on error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
BUILD_TYPE="Debug"
TARGET_BOARD="pico"
BUILD_DIR="build"
CLEAN_BUILD=false
RUN_TESTS=false
FLASH_AFTER_BUILD=false
VERBOSE=false
PARALLEL_JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Function to print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to display usage
usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Advanced build script for PIOKMbox project

OPTIONS:
    -b, --board BOARD       Target board (default: pico)
                           Options: pico, pico2, adafruit_feather_rp2040_usb_host,
                                   adafruit_feather_rp2350, seeed_xiao_rp2350
    -t, --type TYPE        Build type (default: Debug)
                           Options: Debug, Release, MinSizeRel, RelWithDebInfo
    -d, --dir DIR          Build directory (default: build)
    -c, --clean            Clean build (remove build directory first)
    -j, --jobs N           Number of parallel jobs (default: auto-detect)
    -f, --flash            Flash the firmware after successful build
    -T, --test             Run tests after build
    -v, --verbose          Enable verbose output
    -h, --help             Display this help message

EXAMPLES:
    # Basic debug build for Pico
    $0

    # Release build for Pico 2
    $0 --board pico2 --type Release

    # Clean release build with flashing
    $0 --clean --type Release --flash

    # Build with tests
    $0 --test

EOF
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -b|--board)
            TARGET_BOARD="$2"
            shift 2
            ;;
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -d|--dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -c|--clean)
            CLEAN_BUILD=true
            shift
            ;;
        -j|--jobs)
            PARALLEL_JOBS="$2"
            shift 2
            ;;
        -f|--flash)
            FLASH_AFTER_BUILD=true
            shift
            ;;
        -T|--test)
            RUN_TESTS=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Validate build type
VALID_BUILD_TYPES=("Debug" "Release" "MinSizeRel" "RelWithDebInfo")
if [[ ! " ${VALID_BUILD_TYPES[@]} " =~ " ${BUILD_TYPE} " ]]; then
    print_error "Invalid build type: ${BUILD_TYPE}"
    print_info "Valid options: ${VALID_BUILD_TYPES[*]}"
    exit 1
fi

# Validate board
VALID_BOARDS=("pico" "pico2" "adafruit_feather_rp2040_usb_host" "adafruit_feather_rp2350" "seeed_xiao_rp2350")
if [[ ! " ${VALID_BOARDS[@]} " =~ " ${TARGET_BOARD} " ]]; then
    print_error "Invalid board: ${TARGET_BOARD}"
    print_info "Valid options: ${VALID_BOARDS[*]}"
    exit 1
fi

# Display build configuration
echo "======================================"
echo "PIOKMbox Advanced Build Configuration"
echo "======================================"
print_info "Board: ${TARGET_BOARD}"
print_info "Build Type: ${BUILD_TYPE}"
print_info "Build Directory: ${BUILD_DIR}"
print_info "Parallel Jobs: ${PARALLEL_JOBS}"
print_info "Clean Build: ${CLEAN_BUILD}"
print_info "Run Tests: ${RUN_TESTS}"
print_info "Flash After Build: ${FLASH_AFTER_BUILD}"
echo "======================================"

# Check for required tools
print_info "Checking for required tools..."

check_tool() {
    if ! command -v $1 &> /dev/null; then
        print_error "$1 is not installed or not in PATH"
        exit 1
    fi
}

check_tool cmake
check_tool arm-none-eabi-gcc
check_tool arm-none-eabi-g++

if [ "$FLASH_AFTER_BUILD" = true ]; then
    check_tool picotool
fi

print_success "All required tools found"

# Clean build directory if requested
if [ "$CLEAN_BUILD" = true ]; then
    print_info "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
    print_success "Build directory cleaned"
fi

# Create build directory
mkdir -p "${BUILD_DIR}"

# Configure CMake
print_info "Configuring CMake..."

CMAKE_ARGS=(
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    -DTARGET_BOARD="${TARGET_BOARD}"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

if [ "$RUN_TESTS" = true ]; then
    CMAKE_ARGS+=(-DBUILD_TESTING=ON)
fi

if [ "$VERBOSE" = true ]; then
    CMAKE_ARGS+=(-DENABLE_VERBOSE_BUILD=ON)
fi

if ! cmake "${CMAKE_ARGS[@]}"; then
    print_error "CMake configuration failed"
    exit 1
fi

print_success "CMake configuration complete"

# Build the project
print_info "Building project..."

BUILD_ARGS=(
    --build "${BUILD_DIR}"
    --parallel "${PARALLEL_JOBS}"
)

if [ "$VERBOSE" = true ]; then
    BUILD_ARGS+=(--verbose)
fi

if ! cmake "${BUILD_ARGS[@]}"; then
    print_error "Build failed"
    exit 1
fi

print_success "Build complete"

# Display binary size
print_info "Binary size information:"
cmake --build "${BUILD_DIR}" --target size

# Run tests if requested
if [ "$RUN_TESTS" = true ]; then
    print_info "Running tests..."
    if ! (cd "${BUILD_DIR}" && ctest --output-on-failure); then
        print_error "Tests failed"
        exit 1
    fi
    print_success "All tests passed"
fi

# Flash firmware if requested
if [ "$FLASH_AFTER_BUILD" = true ]; then
    print_info "Flashing firmware..."
    
    # Check if device is connected
    if ! picotool info &> /dev/null; then
        print_warning "No device detected. Please connect your device in BOOTSEL mode."
        print_info "Press and hold BOOTSEL button while connecting USB cable."
        print_info "Waiting for device..."
        
        # Wait for device
        TIMEOUT=30
        ELAPSED=0
        while ! picotool info &> /dev/null && [ $ELAPSED -lt $TIMEOUT ]; do
            sleep 1
            ELAPSED=$((ELAPSED + 1))
            echo -n "."
        done
        echo
        
        if [ $ELAPSED -ge $TIMEOUT ]; then
            print_error "Timeout waiting for device"
            exit 1
        fi
    fi
    
    if ! cmake --build "${BUILD_DIR}" --target flash; then
        print_error "Flashing failed"
        exit 1
    fi
    
    print_success "Firmware flashed successfully"
fi

# Generate build report
BUILD_INFO="${BUILD_DIR}/BUILD_INFO.md"
if [ -f "${BUILD_INFO}" ]; then
    print_info "Build information saved to: ${BUILD_INFO}"
fi

# Display final summary
echo
echo "======================================"
echo "Build Summary"
echo "======================================"
print_success "Build completed successfully!"
print_info "Firmware location: ${BUILD_DIR}/PIOKMbox.uf2"
print_info "To flash manually: picotool load -f ${BUILD_DIR}/PIOKMbox.uf2"

# Copy compile_commands.json to root for IDE support
if [ -f "${BUILD_DIR}/compile_commands.json" ]; then
    cp "${BUILD_DIR}/compile_commands.json" .
    print_info "Updated compile_commands.json for IDE support"
fi

exit 0