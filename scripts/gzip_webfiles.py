"""
Pre-build script to gzip web files for SPIFFS.
AsyncWebServer automatically serves .gz files if they exist.
This significantly reduces transfer size and memory usage.
"""

import gzip
import os
import shutil

Import("env")

def gzip_webfiles(source, target, env):
    """Gzip HTML, CSS, and JS files in the data directory."""
    data_dir = os.path.join(env.get("PROJECT_DIR"), "data")
    
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
        
        # Check if source is newer than gzipped version
        if os.path.exists(gz_filepath):
            src_mtime = os.path.getmtime(filepath)
            gz_mtime = os.path.getmtime(gz_filepath)
            if gz_mtime >= src_mtime:
                print(f"[GZIP] {filename}.gz is up to date")
                continue
        
        # Compress the file
        print(f"[GZIP] Compressing {filename}...")
        with open(filepath, 'rb') as f_in:
            with gzip.open(gz_filepath, 'wb', compresslevel=9) as f_out:
                shutil.copyfileobj(f_in, f_out)
        
        # Report compression ratio
        orig_size = os.path.getsize(filepath)
        gz_size = os.path.getsize(gz_filepath)
        ratio = (1 - gz_size / orig_size) * 100
        print(f"[GZIP] {filename}: {orig_size} -> {gz_size} bytes ({ratio:.1f}% reduction)")

# Register as pre-action for uploadfs
env.AddPreAction("uploadfs", gzip_webfiles)
# Also run before building filesystem image
env.AddPreAction("$BUILD_DIR/spiffs.bin", gzip_webfiles)
