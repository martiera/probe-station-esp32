import os

from SCons.Script import Import

Import("env")

# If provided (CI/release), this makes FW_VERSION match the git tag (e.g. v1.2.3)
fw = os.getenv("FW_VERSION")
if fw:
    # Define as an unquoted token (e.g. v1.2.3). `src/config.h` stringizes it.
    env.Append(CPPDEFINES=[("FW_VERSION", fw)])
