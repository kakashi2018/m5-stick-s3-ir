#!/usr/bin/env python3
"""Minimal M5StickS3 built-in IR RX regression fix for Bruce 1.16.1.

One-line, release-specific fix: restore INPUT_PULLUP on GPIO42's
configured receiver pin. This deliberately does NOT replace Bruce's
original IRremoteESP8266 receiver/transmitter implementation.
"""
import hashlib
from pathlib import Path
import sys

SOURCE = Path('src/modules/ir/ir_read.cpp')
UPSTREAM_GIT_BLOB_SHA = '2f9cc95680dc2b6484718dcf130a320f73ac7997'
NEEDLE = '    setup_ir_pin(bruceConfigPins.irRx, INPUT);'
REPLACE = '    setup_ir_pin(bruceConfigPins.irRx, INPUT_PULLUP);'


def blob_sha(data: bytes) -> str:
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()


def main(root: Path) -> None:
    file = root / SOURCE
    data = file.read_bytes()
    actual_sha = blob_sha(data)
    if actual_sha != UPSTREAM_GIT_BLOB_SHA:
        raise ValueError(
            f'Unexpected Bruce source (Git blob {actual_sha}); '
            'the fix applies only to the official Bruce 1.16.1 tag. '
            'Do not apply this script over the previous RMT-patched source.'
        )
    source = data.decode('utf-8')
    if source.count(NEEDLE) != 1:
        raise ValueError(f'Expected one INPUT occurrence, found {source.count(NEEDLE)}')
    modified = source.replace(NEEDLE, REPLACE, 1)
    file.write_text(modified, encoding='utf-8')
    print(f'Applied one-line GPIO42 INPUT_PULLUP fix: {file}')
    print('Kept all stock Bruce IR receive/transmit code unchanged.')
    print('NOTE: On StickS3, USB charging can disable IR receiver power; test on battery.')

if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: python3 apply_ir_fix.py path/to/fresh/Bruce-1.16.1')
    try:
        main(Path(sys.argv[1]).expanduser().resolve())
    except Exception as e:
        raise SystemExit(f'Patch FAILED: {e}')
