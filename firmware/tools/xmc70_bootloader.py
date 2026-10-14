"""Preserve the verified XMC 20:7018 correction for the local DevKitC target.

Only the two audited complete image hashes are accepted. A framework/config
change requires a fresh review instead of applying offsets to an unknown image.
This board-specific workaround substitutes family 0x70 for 0x41.
"""

import hashlib
import os
from pathlib import Path
import struct
import tempfile


ORIGINAL_SHA256 = "2a71d69b471e20c2bac7fb469f3c6a807b3ebee780e348e5889db0da849ca363"
PATCHED_SHA256 = "13d80b69843d8d69a561a5ef286264f9eaff3cbe52f23a36189039168ea3cad0"
ROUTINE_OFFSET = 0x1B1C
ROUTINE_SIZE = 72
PATCHED_ROUTINE = bytes.fromhex(
    "3641002090758d020c0266c93780987480807466d90e82c8ed0c190cda87ba21"
    "9d02060700a2a070a7990982c8e90c190c9ac6f9ff5c0aa7990a82c8eb0c19"
    "f628dd9020741df000"
)


def correct_image(image):
    """Return the exact verified correction, or refuse an unreviewed image."""
    digest = hashlib.sha256(image).hexdigest()
    if digest == PATCHED_SHA256:
        return image
    if digest != ORIGINAL_SHA256:
        raise ValueError(
            "Unrecognized DevKitC bootloader SHA256 " + digest
            + "; upload stopped. Review the new bootloader before updating the XMC guard."
        )

    patched = bytearray(image)
    patched[ROUTINE_OFFSET:ROUTINE_OFFSET + ROUTINE_SIZE] = PATCHED_ROUTINE
    position = 24
    checksum = 0xEF
    for _ in range(patched[1]):
        _, length = struct.unpack_from("<II", patched, position)
        position += 8
        for value in patched[position:position + length]:
            checksum ^= value
        position += length
    checksum_offset = ((position + 16) // 16) * 16 - 1
    patched[checksum_offset] = checksum
    patched[checksum_offset + 1:] = hashlib.sha256(
        patched[:checksum_offset + 1]
    ).digest()
    result = bytes(patched)
    if hashlib.sha256(result).hexdigest() != PATCHED_SHA256:
        raise ValueError("XMC correction did not reproduce the verified image; upload stopped.")
    return result


def correct_file(path):
    """Validate before touching the file, and atomically replace when needed."""
    path = Path(path)
    original = path.read_bytes()
    corrected = correct_image(original)
    if corrected == original:
        return False
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
            temporary = Path(output.name)
            output.write(corrected)
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()
    return True


def prepare_bootloader(source, target, env):
    path = Path(env.subst("$BUILD_DIR/bootloader.bin"))
    try:
        changed = correct_file(path)
    except (OSError, ValueError) as error:
        print("[XMC70] ERROR: " + str(error))
        return 1
    print("[XMC70] " + ("Corrected" if changed else "Verified")
          + " bootloader for flash 20:7018, SHA256 " + PATCHED_SHA256)
    return 0


def before_upload(source, target, env):
    expected = Path(env.subst("$BUILD_DIR/bootloader.bin")).resolve()
    extra_images = env.get("FLASH_EXTRA_IMAGES", [])
    boot_images = [
        Path(env.subst(str(path))).resolve()
        for offset, path in extra_images
        if int(str(offset), 0) == 0
    ]
    # This platform's nobuild mode uploads only the application and omits
    # FLASH_EXTRA_IMAGES. Still validate/correct the existing bootloader file.
    if extra_images and boot_images != [expected]:
        print("[XMC70] ERROR: Unexpected upload bootloader path; upload stopped.")
        return 1
    return prepare_bootloader(source, target, env)


def register(env):
    if env.subst("$PIOENV") != "devkitc_n16r8":
        raise ValueError("XMC70 hook is only supported in devkitc_n16r8")
    env.AddPostAction("$BUILD_DIR/bootloader.bin", prepare_bootloader)
    # Also cover incremental uploads and `pio run -t nobuild -t upload`.
    env.AddPreAction("upload", before_upload)


if "Import" in globals():
    Import("env")
    register(env)
