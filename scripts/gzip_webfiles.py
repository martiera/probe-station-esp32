"""
Pre-build script to gzip web files for SPIFFS.
AsyncWebServer automatically serves .gz files if they exist.
This significantly reduces transfer size and memory usage.
Also injects firmware version into HTML for cache busting.
"""

import gzip
import os
import shutil
import re

Import("env")

def get_firmware_version(env):
    """Extract firmware version from build flags."""
    build_flags = env.get("BUILD_FLAGS", [])
    for flag in build_flags:
        if isinstance(flag, str) and flag.startswith("-DFW_VERSION="):
            return flag.split("=", 1)[1]
    # Check environment variable
    return os.environ.get("FW_VERSION", "dev")

def gzip_webfiles(source, target, env):
    """Gzip HTML, CSS, and JS files in the data directory."""
    data_dir = os.path.join(env.get("PROJECT_DIR"), "data")
    version = get_firmware_version(env)
    
    if not os.path.exists(data_dir):
        print("Data directory not found")
        return
    
    # Files to compress
    extensions = ['.html', '.css', '.js']
    
    for filename in os.listdir(data_dir):
        filepath = os.path.join(data_dir, filename)
        
        # Skip directories and already gzipped files
        if os.path.isdir(filepath) or filename.endswith('.gz'):
            continue
        
        # Check if extension matches
        _, ext = os.path.splitext(filename)
        if ext.lower() not in extensions:
            continue
        
        gz_filepath = filepath + '.gz'
        
        # Always regenerate to ensure version is current
        # Read the file
        with open(filepath, 'rb') as f:
            content = f.read()
        
        # Replace __VERSION__ placeholder with actual version (for HTML files)
        if ext.lower() == '.html':
            content = content.replace(b'__VERSION__', version.encode('utf-8'))
            print(f"[GZIP] Injected version {version} into {filename}")
        
        # Compress the file
        print(f"[GZIP] Compressing {filename}...")
        with gzip.open(gz_filepath, 'wb', compresslevel=9) as f_out:
            f_out.write(content)
        
        # Report compression ratio
        orig_size = os.path.getsize(filepath)
        gz_size = os.path.getsize(gz_filepath)
        ratio = (1 - gz_size / orig_size) * 100
        print(f"[GZIP] {filename}: {orig_size} -> {gz_size} bytes ({ratio:.1f}% reduction)")

# Register as pre-action for uploadfs
env.AddPreAction("uploadfs", gzip_webfiles)
# Also run before building filesystem image
env.AddPreAction("$BUILD_DIR/spiffs.bin", gzip_webfiles)
