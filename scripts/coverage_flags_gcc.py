"""
PlatformIO extra script to add GCC coverage flags to linker
"""
Import("env")

# Add coverage flags to linker (required for gcov runtime)
env.Append(LINKFLAGS=["--coverage"])
