"""Read a connected, identity-checked ESP32-S3 into a new private backup directory."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', required=True)
    ap.add_argument('--baud', type=int, choices=[115200, 460800, 921600], default=460800)
    ap.add_argument('--mac', required=True)
    ap.add_argument('--size-mb', type=int, choices=[8, 16], required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    tool = Path.home()/'.platformio/packages/tool-esptoolpy/esptool.py'
    base = [sys.executable, str(tool), '--chip', 'esp32s3', '--port', args.port, '--baud', str(args.baud)]
    probe = subprocess.run(base+['flash_id'], capture_output=True, text=True, timeout=30)
    (args.out/'identity.log').write_text(probe.stdout+probe.stderr)
    if probe.returncode or ('mac: '+args.mac.lower()) not in probe.stdout.lower():
        raise RuntimeError('connected board identity did not match; no writes performed')
    if f'Detected flash size: {args.size_mb}MB' not in probe.stdout:
        raise RuntimeError('flash capacity differs from requested backup size')
    chunks = []
    count = args.size_mb*2
    for i in range(count):
        part = args.out/f'part-{i:02d}.bin'
        for attempt in range(3):
            cmd = base+['--after', 'hard_reset' if i == count-1 else 'no_reset',
                        'read_flash', hex(i*0x80000), '0x80000', str(part)]
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
            (args.out/f'part-{i:02d}-attempt-{attempt}.log').write_text(result.stdout+result.stderr)
            if result.returncode == 0 and part.exists() and part.stat().st_size == 0x80000:
                break
        else:
            raise RuntimeError(f'chunk {i} failed; incomplete backup, do not flash')
        chunks.append(part.read_bytes())
        print(f'Verified {i+1}/{count} chunks', flush=True)
    blob = b''.join(chunks)
    assert len(blob) == args.size_mb*1024*1024
    digest = hashlib.sha256(blob).hexdigest()
    (args.out/'full-flash.bin').write_bytes(blob)
    (args.out/'full-flash.sha256').write_text(digest+'  full-flash.bin\n')
    (args.out/'manifest.json').write_text(json.dumps(dict(bytes=len(blob), sha256=digest,
        mac=args.mac, port=args.port, transfer='512 KiB reads with esptool stub digest validation',
        privacy='private full flash; never commit or mirror'), indent=2)+'\n')
    print('Full backup verified: '+digest, flush=True)


if __name__ == '__main__':
    main()
