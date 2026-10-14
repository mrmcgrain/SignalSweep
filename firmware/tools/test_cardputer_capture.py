"""Explicit short ADV/S3 hardware capture test. Saves private evidence in .pio.

Run only after installing capture firmware. No flashing or partition changes.
Uses USB to collect raw records or download a completed SD capture.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import struct
import time
import serial


def decoded_records(raw):
    records = []
    for line in raw.splitlines():
        if not line or line.startswith(b'#'):
            continue
        blob = base64.b64decode(line, validate=True)
        if len(blob) < 15:
            raise ValueError('short record')
        radio, seq, ts, channel, rssi, original, length = struct.unpack('<BIIBbHH', blob[:15])
        if radio not in (0, 1) or length > 256 or length > original or len(blob) != 15+length:
            raise ValueError('invalid SSCAP length or radio')
        records.append((radio, seq, channel))
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM6')
    parser.add_argument('--board', choices=['cardputer-adv', 'esp32-s3'], default='cardputer-adv')
    parser.add_argument('--destination', choices=['sd', 'usb'], default='sd')
    parser.add_argument('--seconds', type=int, default=5)
    parser.add_argument('--out', type=Path, default=Path(__file__).resolve().parents[1] / '.pio' / 'adv-capture-validation')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 60:
        parser.error('test duration must be 1..60 seconds')
    args.out.mkdir(parents=True, exist_ok=True)
    report = {'board': args.board, 'destination': args.destination, 'seconds': args.seconds, 'hardware_passed': False}
    stamp = time.strftime('%Y%m%d-%H%M%S')
    raw = bytearray()
    with serial.Serial(args.port, 115200, timeout=0.1, write_timeout=2) as link:
        time.sleep(2)  # opening native USB can reset the board

        def send(cmd):
            link.write((cmd+'\n').encode())
            link.flush()

        def messages(seconds):
            until = time.monotonic()+seconds
            while time.monotonic() < until:
                line = link.readline().strip()
                if not line:
                    continue
                if line.startswith((b'CAP:', b'SDF:')):
                    yield line
                else:
                    try:
                        yield json.loads(line)
                    except (ValueError, UnicodeDecodeError):
                        pass

        def wait(predicate, seconds=10):
            for msg in messages(seconds):
                if isinstance(msg, dict) and predicate(msg):
                    return msg
            raise TimeoutError('device response timeout')

        try:
            send('CMD:CFG')
            cfg = wait(lambda m: m.get('cfg'))
            if cfg.get('board') != args.board or not cfg.get('capture_supported'):
                raise RuntimeError('requested capture-enabled board firmware is not installed')
            if args.destination == 'sd' and not cfg.get('sd_supported'):
                raise RuntimeError('requested board does not support SD capture')
            send('CMD:CAP:START:'+str(args.seconds)+(':SD' if args.destination == 'sd' else ''))
            filename = None
            final = None
            for msg in messages(args.seconds+15):
                if isinstance(msg, bytes) and msg.startswith(b'CAP:'):
                    raw.extend(msg[4:]+b'\n')
                elif isinstance(msg, dict) and 'cap' in msg:
                    cap = msg['cap']
                    if cap.get('error'):
                        raise RuntimeError(cap['error'])
                    if cap.get('started'):
                        filename = cap.get('file')
                    if cap.get('done'):
                        final = cap
                        break
            if final is None:
                raise TimeoutError('capture did not finish')
            if args.destination == 'sd':
                if not filename:
                    raise RuntimeError('missing SD filename')
                offset = 0
                while True:
                    send(f'CMD:SD:GET:{filename}:{offset}')
                    chunk = bytearray()
                    done = None
                    for msg in messages(15):
                        if isinstance(msg, bytes) and msg.startswith(b'SDF:'):
                            chunk.extend(base64.b64decode(msg[4:], validate=True))
                        elif isinstance(msg, dict) and 'sdget' in msg:
                            page = msg['sdget']
                            if page.get('err'):
                                raise RuntimeError(page['err'])
                            if page.get('done') and page.get('off') == offset:
                                done = page
                                break
                    if not done or done['next'] != offset+len(chunk):
                        raise RuntimeError('incomplete SD download')
                    raw.extend(chunk)
                    if not done.get('more'):
                        break
                    if done['next'] <= offset:
                        raise RuntimeError('SD download made no progress')
                    offset = done['next']
            else:
                raw = bytearray(('#SSCAP v1 src=usb board='+args.board+'\n').encode())+raw
            records = decoded_records(raw)
            counts = [sum(r[0] == radio for r in records) for radio in (0, 1)]
            if counts != [final['wifi'], final['ble']] or len(records) != final.get('written', final['wifi']+final['ble']):
                raise RuntimeError('stored/received packet counts differ from final status')
            if not records:
                raise RuntimeError('no RF packets observed, reception remains unverified')
            status = {}
            if args.board == 'cardputer-adv':
                send('CMD:ADV:STATUS')
                status = wait(lambda m: m.get('adv'))
                if status.get('capturing') or status.get('capture_pending'):
                    raise RuntimeError('capture did not relinquish radio')
            # Detector broadcasts are a second witness that scanning restarted.
            telemetry = wait(lambda m: m.get('status') == 'success' and m.get('mode') == 2, 5)
            report.update(hardware_passed=True, wifi=counts[0], ble=counts[1], drops=final['drops'],
                          bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(), file=filename,
                          heap=status.get('heap'), min_heap=status.get('min_heap'),
                          detector_resumed=telemetry.get('mode') == 2)
        except Exception as exc:
            send('CMD:CAP:STOP')
            report['error'] = str(exc)
        finally:
            capture_path = args.out / f'{stamp}-{args.destination}.SSC'
            if raw:
                capture_path.write_bytes(raw)
            (args.out / f'{stamp}-{args.destination}.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    return 0 if report['hardware_passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
