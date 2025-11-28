#!/usr/bin/env python3
"""
BlueBuzzah v2 Build System
==========================

Compiles Python source files to .mpy bytecode using mpy-cross for optimized
deployment to CircuitPython devices.

Usage:
    python build/build.py                    # Standard build
    python build/build.py --clean            # Clean build
    python build/build.py --no-compile       # Debug build (keep .py)
    python build/build.py -O2                # Optimized build
    python build/build.py --verbose          # Verbose output

Module: build
Version: 2.0.0
Author: BlueBuzzah Team
"""

import argparse
import json
import platform
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


# =============================================================================
# Configuration
# =============================================================================

# Default CircuitPython version
DEFAULT_CIRCUITPY_VERSION = "9.2.4"

# Files to keep as .py (CircuitPython requirements)
# These files must remain as .py in the final deployment
KEEP_AS_PY = {
    "main.py",
    "code.py",
    "boot.py",
}


# =============================================================================
# Terminal Colors
# =============================================================================


class Colors:
    """ANSI color codes for terminal output."""

    HEADER = "\033[95m"
    OKBLUE = "\033[94m"
    OKCYAN = "\033[96m"
    OKGREEN = "\033[92m"
    WARNING = "\033[93m"
    FAIL = "\033[91m"
    ENDC = "\033[0m"
    BOLD = "\033[1m"

    @classmethod
    def disable(cls):
        """Disable color output."""
        cls.HEADER = cls.OKBLUE = cls.OKCYAN = cls.OKGREEN = ""
        cls.WARNING = cls.FAIL = cls.ENDC = cls.BOLD = ""


# =============================================================================
# Build Configuration
# =============================================================================


@dataclass
class BuildConfig:
    """Build configuration settings."""

    circuitpy_version: str = DEFAULT_CIRCUITPY_VERSION
    clean_build: bool = False
    compile_enabled: bool = True
    verbose: bool = False
    optimization_level: int = 0


@dataclass
class BuildStats:
    """Build statistics tracking."""

    files_compiled: int = 0
    files_copied: int = 0
    files_skipped: int = 0
    compilation_errors: int = 0
    total_source_size: int = 0
    total_output_size: int = 0

    def print_summary(self):
        """Print build statistics summary."""
        print(f"\n{Colors.BOLD}Build Summary:{Colors.ENDC}")
        print("=" * 50)
        print(f"  Files compiled:  {Colors.OKGREEN}{self.files_compiled}{Colors.ENDC}")
        print(f"  Files copied:    {Colors.OKBLUE}{self.files_copied}{Colors.ENDC}")
        print(f"  Files skipped:   {self.files_skipped}")

        if self.compilation_errors > 0:
            print(
                f"  Errors:          {Colors.FAIL}{self.compilation_errors}{Colors.ENDC}"
            )

        if self.total_source_size > 0:
            savings = self.total_source_size - self.total_output_size
            savings_pct = (savings / self.total_source_size) * 100
            print(f"\n  Source size:     {self._format_size(self.total_source_size)}")
            print(f"  Output size:     {self._format_size(self.total_output_size)}")
            print(
                f"  Space saved:     {Colors.OKGREEN}{self._format_size(savings)} ({savings_pct:.1f}%){Colors.ENDC}"
            )

    @staticmethod
    def _format_size(size_bytes: int) -> str:
        """Format byte size as human-readable string."""
        for unit in ["B", "KB", "MB", "GB"]:
            if size_bytes < 1024.0:
                return f"{size_bytes:.1f} {unit}"
            size_bytes /= 1024.0
        return f"{size_bytes:.1f} TB"


# =============================================================================
# Build System
# =============================================================================


class BlueBuzzahBuilder:
    """
    Build system for BlueBuzzah CircuitPython application.

    Simple recursive build that handles any project structure.
    """

    def __init__(
        self, base_path: Optional[Path] = None, config: Optional[BuildConfig] = None
    ):
        """
        Initialize the builder.

        Args:
            base_path: Base path to project root (default: auto-detect)
            config: Build configuration (default: BuildConfig())
        """
        if base_path is None:
            # Auto-detect: go up one level from build/ to project root
            base_path = Path(__file__).parent.parent

        self.base_path = base_path
        self.dist_path = base_path / "dist"
        self.config = config or BuildConfig()
        self.stats = BuildStats()
        self.mpy_cross_path: Optional[Path] = None

    def get_mpy_cross(self) -> Path:
        """Get mpy-cross binary from build/ directory based on OS."""
        # Detect operating system
        system = platform.system()

        # Map OS to binary name
        if system == "Linux":
            binary_name = "mpy-cross-linux"
        elif system == "Darwin":  # macOS
            binary_name = "mpy-cross-macos"
        elif system == "Windows":
            binary_name = "mpy-cross-windows.exe"
        else:
            raise RuntimeError(f"Unsupported operating system: {system}")

        # Construct path to binary in build/ directory
        mpy_cross_path = self.base_path / "build" / binary_name

        # Check if binary exists
        if not mpy_cross_path.exists():
            print(
                f"  {Colors.FAIL}✗ mpy-cross binary not found: {mpy_cross_path}{Colors.ENDC}"
            )
            raise RuntimeError(f"mpy-cross binary not found for {system}")

        # Ensure binary is executable on Unix-like systems
        if system in ["Linux", "Darwin"]:
            try:
                mpy_cross_path.chmod(0o755)
            except Exception as e:
                print(
                    f"  {Colors.WARNING}⚠ Could not set executable permission: {e}{Colors.ENDC}"
                )

        print(f"  {Colors.OKGREEN}✓ Using mpy-cross: {mpy_cross_path}{Colors.ENDC}")
        return mpy_cross_path

    def compile_file(self, source_file: Path, output_file: Path) -> bool:
        """Compile a single Python file to .mpy using mpy-cross."""
        # Ensure output directory exists
        output_file.parent.mkdir(parents=True, exist_ok=True)

        # Build mpy-cross command
        cmd = [str(self.mpy_cross_path), "-o", str(output_file)]

        # Add optimization level
        if self.config.optimization_level > 0:
            cmd.append(f"-O{self.config.optimization_level}")

        # Add architecture flag for nRF52840 (ARM Cortex-M4F)
        cmd.append("-march=armv7m")

        cmd.append(str(source_file))

        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)

            if result.returncode != 0:
                print(
                    f"  {Colors.FAIL}✗ Compilation failed: {source_file.relative_to(self.base_path)}{Colors.ENDC}"
                )
                if self.config.verbose and result.stderr:
                    print(f"    Error: {result.stderr}")
                self.stats.compilation_errors += 1
                return False

            # Update statistics
            source_size = source_file.stat().st_size
            output_size = output_file.stat().st_size
            self.stats.total_source_size += source_size
            self.stats.total_output_size += output_size
            self.stats.files_compiled += 1

            # Always show compilation progress
            savings = source_size - output_size
            savings_pct = (savings / source_size * 100) if source_size > 0 else 0
            print(
                f"  {Colors.OKGREEN}✓{Colors.ENDC} {source_file.relative_to(self.base_path)} → {output_file.relative_to(self.dist_path)} ({savings_pct:.1f}% smaller)"
            )

            return True

        except subprocess.TimeoutExpired:
            print(
                f"  {Colors.FAIL}✗ Compilation timeout: {source_file.relative_to(self.base_path)}{Colors.ENDC}"
            )
            self.stats.compilation_errors += 1
            return False
        except Exception as e:
            print(
                f"  {Colors.FAIL}✗ Error: {source_file.relative_to(self.base_path)}: {e}{Colors.ENDC}"
            )
            self.stats.compilation_errors += 1
            return False

    def copy_file(self, source_file: Path, output_file: Path) -> bool:
        """Copy a file to output directory."""
        try:
            output_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_file, output_file)
            self.stats.files_copied += 1

            # Always show copy progress
            print(
                f"  {Colors.OKBLUE}→{Colors.ENDC} {source_file.relative_to(self.base_path)} → {output_file.relative_to(self.dist_path)}"
            )

            return True

        except Exception as e:
            print(
                f"  {Colors.FAIL}✗ Copy failed: {source_file.relative_to(self.base_path)}: {e}{Colors.ENDC}"
            )
            return False

    def process_file(self, source_file: Path, strip_src: bool = False) -> bool:
        """
        Process a single file (copy or compile).

        Args:
            source_file: Path to source file
            strip_src: If True, strip 'src/' prefix from output path

        Returns:
            True if successful, False if errors occurred
        """
        rel_path = source_file.relative_to(self.base_path)

        # Strip 'src/' prefix if requested
        if strip_src and rel_path.parts[0] == "src":
            rel_path = Path(*rel_path.parts[1:])

        # Handle Python files
        if source_file.suffix == ".py":
            # Check if file should be kept as .py (by filename)
            if source_file.name in KEEP_AS_PY:
                output_file = self.dist_path / rel_path
                return self.copy_file(source_file, output_file)
            elif self.config.compile_enabled:
                # Compile to .mpy
                output_file = self.dist_path / rel_path.with_suffix(".mpy")
                return self.compile_file(source_file, output_file)
            else:
                # No compilation mode - copy as .py
                output_file = self.dist_path / rel_path
                return self.copy_file(source_file, output_file)
        else:
            # Copy non-Python files as-is (.mpy, .json, etc.)
            output_file = self.dist_path / rel_path
            return self.copy_file(source_file, output_file)

    def process_all_files(self) -> bool:
        """
        Process all source files from src/ directory.
        Files from src/ are placed at root of dist/ (src/ prefix stripped).

        Returns:
            True if successful, False if errors occurred
        """
        success = True
        src_path = self.base_path / "src"

        # Process entire src/ directory recursively, stripping src/ prefix
        if src_path.exists():
            for source_file in src_path.rglob("*"):
                # Skip directories, __pycache__, and lib/ folder inside src/
                if source_file.is_dir():
                    continue
                if "__pycache__" in source_file.parts:
                    if self.config.verbose:
                        print(f"  ⊘ Skipped: {source_file.relative_to(self.base_path)}")
                    self.stats.files_skipped += 1
                    continue
                # Skip all dotfiles (files starting with .)
                if source_file.name.startswith("."):
                    if self.config.verbose:
                        print(
                            f"  ⊘ Skipped (dotfile): {source_file.relative_to(self.base_path)}"
                        )
                    self.stats.files_skipped += 1
                    continue
                # Skip markdown files
                if source_file.suffix.lower() == ".md":
                    if self.config.verbose:
                        print(
                            f"  ⊘ Skipped (markdown): {source_file.relative_to(self.base_path)}"
                        )
                    self.stats.files_skipped += 1
                    continue
                # Skip src/lib/ - libraries come from root lib/
                if "lib" in source_file.relative_to(src_path).parts:
                    if self.config.verbose:
                        print(
                            f"  ⊘ Skipped (use root lib/): {source_file.relative_to(self.base_path)}"
                        )
                    self.stats.files_skipped += 1
                    continue
                success &= self.process_file(source_file, strip_src=True)
        else:
            print(f"  {Colors.WARNING}⚠ src/ directory not found{Colors.ENDC}")

        return success

    def copy_libraries(self) -> bool:
        """
        Copy lib/ directory from workspace root to dist/lib/.

        Returns:
            True if successful, False if errors occurred
        """
        lib_path = self.base_path / "lib"

        if not lib_path.exists():
            if self.config.verbose:
                print(f"  {Colors.WARNING}⚠ lib/ directory not found{Colors.ENDC}")
            return True  # Not an error if no lib/ exists

        try:
            dest_lib = self.dist_path / "lib"
            if dest_lib.exists():
                shutil.rmtree(dest_lib)

            shutil.copytree(lib_path, dest_lib)

            # Count library files
            lib_count = sum(1 for _ in dest_lib.rglob("*") if _.is_file())
            print(
                f"  {Colors.OKGREEN}✓ Copied lib/ directory ({lib_count} files){Colors.ENDC}"
            )

            return True

        except Exception as e:
            print(f"  {Colors.FAIL}✗ Failed to copy lib/: {e}{Colors.ENDC}")
            return False

    def create_settings_file(self) -> bool:
        """Create default settings.json with Primary role."""
        try:
            settings = {"deviceRole": "Primary"}
            settings_file = self.dist_path / "settings.json"

            with open(settings_file, "w") as f:
                json.dump(settings, f, indent=2)

            if self.config.verbose:
                print(
                    f"  {Colors.OKBLUE}→{Colors.ENDC} Created settings.json (default: Primary)"
                )

            return True

        except Exception as e:
            print(f"  {Colors.FAIL}✗ Failed to create settings.json: {e}{Colors.ENDC}")
            return False

    def create_required_directories(self) -> bool:
        """Create required directory structure in dist/."""
        required_dirs = [
            "profile_data",  # Therapy profile storage (renamed to avoid conflict with profiles.py module)
        ]

        try:
            for dir_name in required_dirs:
                dir_path = self.dist_path / dir_name
                dir_path.mkdir(parents=True, exist_ok=True)

                if self.config.verbose:
                    print(
                        f"  {Colors.OKBLUE}→{Colors.ENDC} Created directory: {dir_name}/"
                    )

            print(f"  {Colors.OKGREEN}✓ Created required directories{Colors.ENDC}")
            return True

        except Exception as e:
            print(f"  {Colors.FAIL}✗ Failed to create directories: {e}{Colors.ENDC}")
            return False

    def clean_dist(self) -> bool:
        """Clean the dist/ directory."""
        if self.dist_path.exists():
            print(f"  {Colors.WARNING}Cleaning dist/...{Colors.ENDC}")
            try:
                shutil.rmtree(self.dist_path)
                print(f"  {Colors.OKGREEN}✓ Cleaned{Colors.ENDC}")
                return True
            except Exception as e:
                print(f"  {Colors.FAIL}✗ Failed to clean: {e}{Colors.ENDC}")
                return False
        return True

    def validate_build(self) -> bool:
        """Validate the build output."""
        print(f"\n{Colors.BOLD}Validating build...{Colors.ENDC}")

        # Check that dist/ exists
        if not self.dist_path.exists():
            print(f"  {Colors.FAIL}✗ dist/ directory not found{Colors.ENDC}")
            return False

        # Check for required files
        required_files = ["main.py", "boot.py", "settings.json"]
        for filename in required_files:
            if not (self.dist_path / filename).exists():
                print(f"  {Colors.FAIL}✗ Missing: {filename}{Colors.ENDC}")
                return False

        # Verify settings.json
        try:
            with open(self.dist_path / "settings.json") as f:
                config = json.load(f)
                if "deviceRole" not in config:
                    print(
                        f"  {Colors.FAIL}✗ settings.json missing deviceRole{Colors.ENDC}"
                    )
                    return False
        except Exception as e:
            print(f"  {Colors.FAIL}✗ Invalid settings.json: {e}{Colors.ENDC}")
            return False

        # Check for lib/ directory (optional but recommended)
        if not (self.dist_path / "lib").exists():
            print(
                f"  {Colors.WARNING}⚠ lib/ directory not found (may cause import errors){Colors.ENDC}"
            )

        # Check for compilation errors
        if self.stats.compilation_errors > 0:
            print(
                f"  {Colors.FAIL}✗ Build had {self.stats.compilation_errors} error(s){Colors.ENDC}"
            )
            return False

        print(f"  {Colors.OKGREEN}✓ Validation passed{Colors.ENDC}")
        return True

    def build(self) -> bool:
        """
        Execute the complete build process.

        Returns:
            True if build successful, False otherwise
        """
        print(f"\n{Colors.BOLD}{Colors.HEADER}BlueBuzzah v2 Build System{Colors.ENDC}")
        print("=" * 60)
        print(f"CircuitPython Version: {self.config.circuitpy_version}")
        print(
            f"Compilation: {'Enabled' if self.config.compile_enabled else 'Disabled'}"
        )
        print(f"Optimization Level: -O{self.config.optimization_level}")
        print()

        try:
            # Step 1: Clean if requested
            if self.config.clean_build:
                if not self.clean_dist():
                    return False

            # Step 2: Get mpy-cross (if compilation enabled)
            if self.config.compile_enabled:
                print(f"{Colors.BOLD}Getting mpy-cross{Colors.ENDC}")
                print("-" * 60)
                self.mpy_cross_path = self.get_mpy_cross()
                print()

            # Step 3: Process all files (single recursive pass)
            print(f"{Colors.BOLD}Building project{Colors.ENDC}")
            print("-" * 60)
            success = self.process_all_files()
            if not success:
                print(f"{Colors.WARNING}⚠ Errors occurred during build{Colors.ENDC}")
            print()

            # Step 4: Copy libraries
            print(f"{Colors.BOLD}Copying libraries{Colors.ENDC}")
            print("-" * 60)
            if not self.copy_libraries():
                return False
            print()

            # Step 5: Create settings.json
            print(f"{Colors.BOLD}Creating configuration{Colors.ENDC}")
            print("-" * 60)
            if not self.create_settings_file():
                return False
            if not self.create_required_directories():
                return False
            print()

            # Step 6: Validate build
            if not self.validate_build():
                return False
            print()

            # Step 7: Print statistics
            self.stats.print_summary()
            print()

            print(
                f"{Colors.OKGREEN}{Colors.BOLD}✓ Build completed successfully!{Colors.ENDC}"
            )
            print(f"\nOutput: {self.dist_path}/")
            return True

        except Exception as e:
            print(f"\n{Colors.FAIL}Build failed: {e}{Colors.ENDC}")
            if self.config.verbose:
                import traceback

                traceback.print_exc()
            return False


# =============================================================================
# CLI
# =============================================================================


def main():
    """Main CLI entry point."""
    parser = argparse.ArgumentParser(
        description="Build BlueBuzzah v2 for CircuitPython deployment",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python build/build.py                    # Standard build
  python build/build.py --clean            # Clean build
  python build/build.py --no-compile       # Debug build (keep .py)
  python build/build.py -O2                # Optimized build
  python build/build.py --verbose          # Verbose output
        """,
    )

    parser.add_argument(
        "--clean", action="store_true", help="Clean dist/ before building"
    )

    parser.add_argument(
        "--no-compile",
        action="store_true",
        help="Skip compilation (copy .py for debugging)",
    )

    parser.add_argument(
        "-O",
        "--optimize",
        type=int,
        choices=[0, 1, 2, 3],
        default=0,
        help="Optimization level (0=none, 1-3=increasing)",
    )

    parser.add_argument("--verbose", action="store_true", help="Verbose output")

    parser.add_argument(
        "--no-color", action="store_true", help="Disable colored output"
    )

    args = parser.parse_args()

    # Disable colors if requested
    if args.no_color:
        Colors.disable()

    # Create build configuration
    config = BuildConfig(
        clean_build=args.clean,
        compile_enabled=not args.no_compile,
        verbose=args.verbose,
        optimization_level=args.optimize,
    )

    # Create builder and run
    builder = BlueBuzzahBuilder(config=config)
    success = builder.build()

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
