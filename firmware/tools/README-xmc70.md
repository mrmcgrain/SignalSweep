# DevKitC XMC bootloader upload correction

The verified ESP32-S3 DevKitC board has XMC flash ID `20:7018`. Its installed
Arduino/ESP-IDF 4.4.7 bootloader rejects family `0x70` and repeatedly resets.
`xmc70_bootloader.py` preserves the board-specific correction that was tested
with a complete boot-region readback and working SignalSweep BLE commands.

The local environment in `firmware/devkitc.local.ini` enables it with:

```ini
[env:devkitc_n16r8]
extra_scripts = post:tools/xmc70_bootloader.py
```

Keep the existing board, memory, partition and build-flag settings in that
environment. The local INI is already ignored by Git; retain it when moving this
local board setup to another checkout. The hook itself contains no absolute
machine paths and does not depend on private backups or diagnostic binaries.

Normal use:

```powershell
pio run --project-dir 'G:\esp32\signalSweep\SignalSweep\firmware' -e devkitc_n16r8 -t upload --upload-port COM8
```

The post-build action corrects a newly generated bootloader. The pre-upload
action also checks incremental uploads and corrects a known original image
before PlatformIO opens the serial port. An already-corrected image is unchanged.
In this platform's `nobuild` mode the uploader omits the extra images and writes
only the application; the hook still validates the existing bootloader file.

The hook only accepts these complete image hashes:

- Original: `2a71d69b471e20c2bac7fb469f3c6a807b3ebee780e348e5889db0da849ca363`
- Corrected: `13d80b69843d8d69a561a5ef286264f9eaff3cbe52f23a36189039168ea3cad0`

Unknown images stop the build or upload. A framework, flash-mode or other
bootloader change requires inspecting the new image and verifying a correction
before updating the guard. Do not bypass it by merely replacing the hashes.

This remains a target-specific substitution of XMC family `0x70` for `0x41`,
with vendor and capacity checks retained. It is not an upstream general fix and
must not be enabled for other boards. Esptool's independent XMC warning may
persist. Application detection accuracy and phone integration require separate
validation.

The original recovery copy and rollback instructions remain in
`firmware/diagnostics/bootloader-2026-10-14/README.md`. Removing the hook allows
normal uploads to restore the original faulty bootloader on this board.

PlatformIO's hook API is documented at
https://docs.platformio.org/en/latest/scripting/actions.html.
