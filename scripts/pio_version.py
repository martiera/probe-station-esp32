import os

from SCons.Script import Import

Import("env")

# If provided (CI/release), this makes FW_VERSION match the git tag (e.g. v1.2.3)
fw = os.getenv("FW_VERSION")
if fw:
    # Define as a C string literal
    env.Append(CPPDEFINES=[("FW_VERSION", f'"{fw}"')])
