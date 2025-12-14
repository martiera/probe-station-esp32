#!/bin/bash
# Manual script to gzip web files and upload SPIFFS
# Usage: ./upload_spiffs.sh [env_name]
# Examples:
#   ./upload_spiffs.sh          # Uses 'release' environment
#   ./upload_spiffs.sh ttgo-t-display

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DATA_DIR="$PROJECT_DIR/data"
ENV_NAME="${1:-release}"

echo "=== SPIFFS Upload Script ==="
echo "Project: $PROJECT_DIR"
echo "Data dir: $DATA_DIR"
echo "Environment: $ENV_NAME"
echo ""

# Check if data directory exists
if [ ! -d "$DATA_DIR" ]; then
    echo "Error: data/ directory not found"
    exit 1
fi

# Gzip web files
echo "=== Compressing web files ==="
for file in "$DATA_DIR"/*.html "$DATA_DIR"/*.css "$DATA_DIR"/*.js; do
    if [ -f "$file" ]; then
        filename=$(basename "$file")
        gz_file="${file}.gz"
        
        # Check if source is newer than gzipped version
        if [ -f "$gz_file" ] && [ "$gz_file" -nt "$file" ]; then
            echo "[SKIP] $filename.gz (up to date)"
            continue
        fi
        
        # Get original size
        orig_size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null)
        
        # Compress
        gzip -9 -k -f "$file"
        
        # Get compressed size
        gz_size=$(stat -f%z "$gz_file" 2>/dev/null || stat -c%s "$gz_file" 2>/dev/null)
        
        # Calculate reduction
        if [ "$orig_size" -gt 0 ]; then
            reduction=$(echo "scale=1; (1 - $gz_size / $orig_size) * 100" | bc)
            echo "[GZIP] $filename: $orig_size -> $gz_size bytes (${reduction}% reduction)"
        else
            echo "[GZIP] $filename: compressed"
        fi
    fi
done

echo ""
echo "=== Files in data/ ==="
ls -la "$DATA_DIR"

echo ""
echo "=== Uploading SPIFFS ==="
cd "$PROJECT_DIR"
~/.platformio/penv/bin/platformio run -e "$ENV_NAME" -t uploadfs

echo ""
echo "=== Done! ==="
