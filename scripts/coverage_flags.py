"""
PlatformIO extra script to add LLVM coverage flags to linker
"""
Import("env")

# Add coverage flag to linker
env.Append(LINKFLAGS=["-fprofile-instr-generate"])
