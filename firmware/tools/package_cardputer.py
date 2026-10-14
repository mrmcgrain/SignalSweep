"""Package an already-built ADV firmware without connecting to any device."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packages', type=Path, default=Path.home() / '.platformio' / 'packages')
    args = parser.parse_args()
    firmware = Path(__file__).resolve().parents[1]
    build = firmware / '.pio' / 'build' / 'cardputer_adv'
    tool = args.packages / 'tool-esptoolpy' / 'esptool.py'
    ota = args.packages / 'framework-arduinoespressif32' / 'tools' / 'partitions' / 'boot_app0.bin'
    parts = [(0, build / 'bootloader.bin'), (0x8000, build / 'partitions.bin'),
             (0xe000, ota), (0x10000, build / 'firmware.bin')]
    for _, path in parts:
        if not path.is_file():
            parser.error(f'Missing {path}. Build cardputer_adv first.')
    if not tool.is_file():
        parser.error(f'Missing esptool at {tool}; set --packages.')
    app = build / 'SignalSweep-cardputer-adv-app.bin'
    factory = build / 'SignalSweep-cardputer-adv-factory.bin'
    launcher = build / 'SignalSweep-cardputer-adv.bin'
    capture = build / 'SignalSweep-cardputer-adv-capture.bin'
    shutil.copyfile(build / 'firmware.bin', app)
    cmd = [sys.executable, str(tool), '--chip', 'esp32s3', 'merge_bin', '-o', str(factory),
           '--flash_mode', 'dio', '--flash_freq', '80m', '--flash_size', '8MB']
    for offset, path in parts:
        cmd.extend([hex(offset), str(path)])
    subprocess.run(cmd, check=True)
    # Launcher parses this merged container, extracts the app and creates
    # the declared ssweepfs data partition. It does not flash the bootloader.
    shutil.copyfile(factory, launcher)
    shutil.copyfile(launcher, capture)
    source_files = ['platformio.ini', 'partitions_cardputer.csv'] + [str(p.relative_to(firmware)) for p in sorted((firmware / 'src').rglob('*')) if p.is_file() and 'bench' not in p.parts]
    manifest = {
        'board': 'cardputer-adv', 'capture_supported': True,
        'capture_backend': 'internal RAM queues; foreground SD/USB drain; 256-byte packet prefix',
        'minimum_app_partition_bytes': (app.stat().st_size + 0xffff) & ~0xffff,
        'hardware_validation': 'see docs/cardputer-adv-port.md for live results; packaging does not flash',
        'flash_mode': 'dio', 'flash_frequency': '80m', 'flash_size': '8MB',
        'source_sha256': {p: hashlib.sha256((firmware / p).read_bytes()).hexdigest() for p in source_files},
        'artifacts': []
    }
    for path, offset, role in [(app, '0x10000', 'application only; requires existing ssweepfs partition; use SD container for initial install'),
                              (factory, '0x0', 'direct-flash image; replaces existing bootloader/partition layout'),
                              (launcher, None, 'M5Launcher SD install container; app plus ssweepfs partition metadata'),
                              (capture, None, 'capture-enabled alias of Launcher SD install container')]:
        manifest['artifacts'].append({'file': path.name, 'bytes': path.stat().st_size,
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'offset': offset, 'role': role})
    (build / 'SignalSweep-cardputer-adv-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(manifest['artifacts'], indent=2))


if __name__ == '__main__':
    main()
