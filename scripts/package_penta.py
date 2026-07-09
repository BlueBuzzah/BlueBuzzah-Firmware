"""PlatformIO post-action: package PentaBuzzer (BlueBuzzah v3) flash images.

Produces $BUILD_DIR/firmware-v3.zip containing every image `pio run -t upload`
would flash (bootloader, partition table, boot_app0, application) plus a
manifest.json recording flash offsets, parameters, and per-part SHA256 hashes.
The BlueBuzzah-Updater application consumes this archive to flash v3 hardware.

Offsets and image paths are taken from the live SCons environment
(FLASH_EXTRA_IMAGES / ESP32_APP_OFFSET), never hardcoded, so the archive can
not drift from what PlatformIO actually flashes.

Version stamped into the manifest comes from $BLUEBUZZAH_VERSION (set by CI
from the release tag), defaulting to "0.0.0-dev" for local builds.
"""

import hashlib
import json
import os
import zipfile

Import("env")  # noqa: F821 - provided by SCons

MANIFEST_VERSION = 1
ARCHIVE_NAME = "firmware-v3.zip"


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def _flash_freq(board):
    # board f_flash is Hz like "80000000L" -> esptool-style "80m"
    raw = str(board.get("build.f_flash", "80000000L")).rstrip("L")
    return "%dm" % (int(raw) // 1_000_000)


def package(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    board = env.BoardConfig()

    parts = [
        (env.subst(str(offset)), env.subst(str(image)))
        for offset, image in env.get("FLASH_EXTRA_IMAGES", [])
    ]
    app_offset = env.subst("$ESP32_APP_OFFSET") or "0x10000"
    parts.append((app_offset, os.path.join(build_dir, env.subst("${PROGNAME}") + ".bin")))
    parts.sort(key=lambda p: int(p[0], 0))

    manifest = {
        "manifest_version": MANIFEST_VERSION,
        "board": env.subst("$PIOENV"),
        "chip": board.get("build.mcu", "esp32s3"),
        "flash": {
            "mode": board.get("build.flash_mode", "dio"),
            "freq": _flash_freq(board),
            "size": board.get("upload.flash_size", "8MB"),
        },
        "application_version": os.environ.get("BLUEBUZZAH_VERSION", "0.0.0-dev"),
        "parts": [
            {"path": os.path.basename(image), "offset": offset, "sha256": _sha256(image)}
            for offset, image in parts
        ],
    }

    archive = os.path.join(build_dir, ARCHIVE_NAME)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("manifest.json", json.dumps(manifest, indent=2))
        for _, image in parts:
            zf.write(image, os.path.basename(image))

    print("package_penta: wrote %s (%d parts)" % (archive, len(parts)))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", package)  # noqa: F821
