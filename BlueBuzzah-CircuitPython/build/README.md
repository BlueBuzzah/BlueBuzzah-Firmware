# BlueBuzzah v2 Build System

Compiles Python source files to `.mpy` bytecode for optimized CircuitPython deployment on the Adafruit Feather nRF52840 Express.

## Quick Start

```bash
# Standard build
python build/build.py

# Clean build (recommended for production)
python build/build.py --clean

# Optimized build
python build/build.py --clean -O2

# Debug build (no compilation)
python build/build.py --no-compile
```

## Command Line Options

| Option                     | Description                                            |
| -------------------------- | ------------------------------------------------------ |
| `--clean`                  | Remove `dist/` before building                         |
| `--no-compile`             | Skip compilation, copy all files as `.py` (debug mode) |
| `-O, --optimize {0,1,2,3}` | Optimization level (0=none, 2=recommended, 3=max)      |
| `--verbose`                | Show detailed file operations and skipped files        |
| `--no-color`               | Disable colored output                                 |

## How It Works

The build process:

1. **Uses local mpy-cross** binary from `build/` directory (auto-detected for macOS/Linux/Windows)
2. **Processes source files** from `src/`:
   - Compiles `.py` files to `.mpy` bytecode with `-march=armv7m` flag for nRF52840
   - Keeps required files as `.py` (`main.py`, `boot.py`, `code.py`)
   - Skips dotfiles, markdown files, and `__pycache__` directories
3. **Copies libraries** from root `lib/` directory
4. **Creates configuration** files and required directories
5. **Validates** the build output

### CircuitPython .py Requirements

CircuitPython **requires** these files to remain as readable `.py` source:

- `main.py` - Main entry point (CircuitPython looks for this)
- `code.py` - Alternative entry point
- `boot.py` - Boot configuration (runs before USB mounts)

These cannot be compiled to `.mpy` because CircuitPython needs to read them directly at startup.

All other Python files are compiled to `.mpy` for better performance and smaller size.

## Build Output

The `dist/` folder contains everything needed for deployment:

```
dist/
├── main.py              # Entry point (kept as .py)
├── boot.py              # Boot config (kept as .py)
├── settings.json        # Device configuration
├── profiles/            # Therapy profile storage
├── lib/                 # CircuitPython libraries
└── *.mpy                # Compiled application code
```

Simply copy the contents of `dist/` to the CIRCUITPY drive.

## Examples

### Production Build

Clean, optimized build for deployment:

```bash
python build/build.py --clean -O2
```

### Debug Build

Keep all files as `.py` for easier debugging on device:

```bash
python build/build.py --no-compile --clean
```

### Verbose Build

See every file operation and what's being skipped:

```bash
python build/build.py --verbose
```

## Optimization Levels

| Level | Use Case          | Size Reduction | Debug Info |
| ----- | ----------------- | -------------- | ---------- |
| `-O0` | Development       | ~40-60%        | Full       |
| `-O1` | Testing           | ~50-70%        | Most       |
| `-O2` | Production        | ~60-80%        | Some       |
| `-O3` | Space-constrained | ~70-85%        | Minimal    |

**Recommendation:** `-O0` for development, `-O2` for production.

## What Gets Skipped

The build automatically excludes:

- Dotfiles (`.DS_Store`, `.gitignore`, etc.)
- Markdown files (`.md`)
- Python cache (`__pycache__/`)
- Source `lib/` folder (use root `lib/` instead)

Use `--verbose` to see what's being skipped during the build.

## Configuration

**KEEP_AS_PY** - Files to keep as `.py` (line 41 in `build/build.py`):

```python
KEEP_AS_PY = {
    "main.py",
    "code.py",
    "boot.py",
}
```

## Best Practices

- Use `--clean` for production builds to ensure no stale files
- Use `-O2` for deployed devices, `-O0` (default) for development
- Use `--no-compile` when debugging issues on the device
- Always test builds on real hardware before deployment
- Keep `dist/` in `.gitignore` (build artifacts should not be committed)

## Target Hardware

This build system is configured for:

- **Board:** Adafruit Feather nRF52840 Express
- **Architecture:** ARM Cortex-M4F (ARMv7-M)
- **CircuitPython:** v9.2.x

The compiled `.mpy` files are optimized specifically for this hardware.
