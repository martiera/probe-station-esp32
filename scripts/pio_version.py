import os

from SCons.Script import Import

Import("env")

# If provided (CI/release), this makes FW_VERSION match the git tag (e.g. v1.2.3)
fw = os.getenv("FW_VERSION", "v1.0.0")

# Always define FW_VERSION for C++ code
env.Append(CPPDEFINES=[("FW_VERSION", fw)])
