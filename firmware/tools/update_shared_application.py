"""Update only a known board's app after verifying a complete private backup."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys


TARGETS = {
    'cardputer-adv': ('50:78:7d:cd:fb:98', 8, 'signa1', 0x320000, 'cardputer_adv'),
    'esp32-s3': ('44:bd:8d:69:9e:74', 16, 'app0', 0x10000, 'devkitc_n16r8'),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--board', choices=TARGETS, required=True)
    ap.add_argument('--port', required=True)
    ap.add_argument('--backup', type=Path, required=True)
    args = ap.parse_args()
    mac, mb, label, offset, env = TARGETS[args.board]
    backup = args.backup.resolve()
    manifest = json.loads((backup/'manifest.json').read_text())
    blob = (backup/'full-flash.bin').read_bytes()
    assert len(blob) == mb*1024*1024
    assert manifest['mac'].lower() == mac
    assert hashlib.sha256(blob).hexdigest() == manifest['sha256']
    fw = Path(__file__).resolve().parents[1]/'.pio/build'/env/'firmware.bin'
    image = fw.read_bytes()
    spec = importlib.util.spec_from_file_location('partitions', Path.home()/'.platformio/packages/framework-arduinoespressif32/tools/gen_esp32part.py')
    parts = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(parts)
    table = parts.PartitionTable.from_binary(blob[0x8000:0x9000])
    table.verify()
    entries = [p for p in table if p.name == label and p.type == 0 and p.offset == offset]
    assert len(entries) == 1 and len(image) <= entries[0].size
    tool = Path.home()/'.platformio/packages/tool-esptoolpy/esptool.py'
    base = [sys.executable, str(tool), '--chip', 'esp32s3', '--port', args.port, '--baud', '460800']

    def run(name, options):
        result = subprocess.run(base+options, capture_output=True, text=True, timeout=120)
        (backup/(name+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode:
            raise RuntimeError(name+' failed; inspect private log')
        return result.stdout

    identity = run('update-identity', ['flash_id'])
    assert 'mac: '+mac in identity.lower()
    live_table = backup/'partition-pre-update.bin'
    run('update-preflight', ['--after', 'no_reset', 'read_flash', '0x8000', '0x1000', str(live_table)])
    assert live_table.read_bytes() == blob[0x8000:0x9000], 'partition table changed since backup'
    boot = backup/'preserved-boot-region.bin'
    partition = backup/'preserved-partition.bin'
    boot.write_bytes(blob[:0x8000])
    partition.write_bytes(blob[0x8000:0x9000])
    print(f'Backup and slot verified; app-only write {args.board} at {offset:#x}', flush=True)
    run('application-write', ['--after', 'no_reset', 'write_flash', '--flash_mode', 'keep',
        '--flash_freq', 'keep', '--flash_size', 'keep', hex(offset), str(fw)])
    run('application-and-preservation-verify', ['verify_flash', '0x0', str(boot), '0x8000',
        str(partition), hex(offset), str(fw)])
    report = dict(board=args.board, mac=mac, application_offset=hex(offset), bytes=len(image),
        sha256=hashlib.sha256(image).hexdigest(), backup_sha256=manifest['sha256'],
        verified='device digest matched application, untouched boot region and partition table')
    (backup/'application-update.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
