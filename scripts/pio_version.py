import os

from SCons.Script import Import

Import("env")

# If provided (CI/release), this makes FW_VERSION match the git tag (e.g. v1.2.3)
fw = os.getenv("FW_VERSION", "v1.0.0")

# Always define FW_VERSION for C++ code
env.Append(CPPDEFINES=[("FW_VERSION", fw)])

# Callback to replace __VERSION__ in data files before building filesystem
def process_data_files(source, target, env):
    data_dir = os.path.join(env["PROJECT_DIR"], "data")
    html_file = os.path.join(data_dir, "index.html")
    
    if os.path.exists(html_file):
        with open(html_file, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Replace __VERSION__ with actual version
        if '__VERSION__' in content:
            content = content.replace('__VERSION__', fw)
            with open(html_file, 'w', encoding='utf-8') as f:
                f.write(content)
            print(f"[VERSION] Cache-busting: Updated index.html with version {fw}")

# Hook into buildfs target (runs before filesystem upload)
env.AddPreAction("$BUILD_DIR/spiffs.bin", process_data_files)
