#!/bin/bash
#
# ESP32 Temperature Monitor - Upload Script
# Builds and uploads firmware with optional flash erase
#
# Usage:
#   ./scripts/upload.sh              # Normal upload (default version)
#   ./scripts/upload.sh v1.0.29      # Upload with specific version
#   ./scripts/upload.sh --erase      # Full flash erase before upload
#   ./scripts/upload.sh v1.0.29 --erase --fs  # Upload with version, erase, and upload filesystem
#

set -e  # Exit on error

# Get latest git tag and increment for default version
LATEST_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "v1.0.0")
# Extract version number (e.g., v1.0.28 -> 28)
LATEST_NUM=$(echo "$LATEST_TAG" | grep -oE '[0-9]+$')
# Increment version
NEXT_NUM=$((LATEST_NUM + 1))
# Extract prefix (e.g., v1.0.28 -> v1.0)
PREFIX=$(echo "$LATEST_TAG" | sed 's/[0-9]*$//')
DEFAULT_VERSION="${PREFIX}${NEXT_NUM}"

# Default values
VERSION="${FW_VERSION:-$DEFAULT_VERSION}"
ERASE=false
UPLOAD_FS=false
ENV="release"
MONITOR=true

# Parse arguments
for arg in "$@"; do
    case $arg in
        --erase)
            ERASE=true
            shift
            ;;
        --fs|--filesystem)
            UPLOAD_FS=true
            shift
            ;;
        --no-monitor)
            MONITOR=false
            shift
            ;;
        --env=*)
            ENV="${arg#*=}"
            shift
            ;;
        v*.*)
            VERSION="$arg"
            shift
            ;;
        *)
            # Unknown option
            ;;
    esac
done

echo "╔════════════════════════════════════════════════════════════╗"
echo "║  ESP32 Temperature Monitor - Upload Script                ║"
echo "╠════════════════════════════════════════════════════════════╣"
echo "║  Version:     $VERSION"
echo "║  Environment: $ENV"
echo "║  Erase Flash: $ERASE"
echo "║  Upload FS:   $UPLOAD_FS"
echo "║  Monitor:     $MONITOR"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Step 1: Clean build directories
echo "→ Cleaning build directories..."
~/.platformio/penv/bin/platformio run -t clean

# Remove old build artifacts
if [ -d ".pio/build/$ENV" ]; then
    echo "→ Removing .pio/build/$ENV directory..."
    rm -rf ".pio/build/$ENV"
fi

# Step 2: Optional flash erase (for fresh installs or fixing corruption)
if [ "$ERASE" = true ]; then
    echo ""
    echo "⚠️  WARNING: Erasing entire flash (this will wipe all data including WiFi config)!"
    echo "Press Ctrl+C to cancel, or Enter to continue..."
    read -r
    echo "→ Erasing flash..."
    ~/.platformio/penv/bin/platformio run -e $ENV -t erase
    echo "✓ Flash erased successfully"
fi

# Step 3: Upload filesystem (SPIFFS) if requested
if [ "$UPLOAD_FS" = true ]; then
    echo ""
    echo "→ Uploading filesystem (SPIFFS)..."
    FW_VERSION=$VERSION ~/.platformio/penv/bin/platformio run -e $ENV -t uploadfs
    echo "✓ Filesystem uploaded successfully"
fi

# Step 4: Build and upload firmware
echo ""
echo "→ Building and uploading firmware..."
FW_VERSION=$VERSION ~/.platformio/penv/bin/platformio run -e $ENV -t upload

echo ""
echo "✓ Upload complete!"

# Step 5: Start serial monitor
if [ "$MONITOR" = true ]; then
    echo ""
    echo "→ Starting serial monitor (Ctrl+C to exit)..."
    echo ""
    ~/.platformio/penv/bin/platformio device monitor --baud 115200
else
    echo ""
    echo "Skipping monitor. To monitor manually, run:"
    echo "  platformio device monitor --baud 115200"
fi
