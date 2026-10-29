#!/usr/bin/env python3
"""Patch a fresh Bruce 1.16.1 checkout for M5StickS3 IR receive and FastLED build.

IR workaround: restore INPUT_PULLUP in Bruce 1.16.1 ir_read.cpp.
Build workaround: pin FastLED 3.10.3 to avoid a conflict with Bruce's -DFP=1.
"""
import hashlib
from pathlib import Path
import sys

SOURCE = Path("src/modules/ir/ir_read.cpp")
UPSTREAM_SHA = "2f9cc95680dc2b6484718dcf130a320f73ac7997"
IR_OLD = "    setup_ir_pin(bruceConfigPins.irRx, INPUT);"
IR_NEW = "    setup_ir_pin(bruceConfigPins.irRx, INPUT_PULLUP);"
PIO_OLD = "fastled/FastLED @^3.10.3"
PIO_NEW = "fastled/FastLED @ 3.10.3"


def git_blob_sha(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def main(repo: Path) -> None:
    file = repo / SOURCE
    data = file.read_bytes()
    if git_blob_sha(data) != UPSTREAM_SHA:
        raise RuntimeError("IR source differs from official Bruce 1.16.1; use a fresh checkout")
    s = data.decode("utf-8")
    if s.count(IR_OLD) != 1:
        raise RuntimeError("Expected one IR receiver initialization statement")

    ini = repo / "platformio.ini"
    pio = ini.read_text(encoding="utf-8")
    if pio.count(PIO_OLD) != 1:
        raise RuntimeError("Expected one original FastLED dependency declaration")

    # Validate everything before writing anything.
    file.write_text(s.replace(IR_OLD, IR_NEW, 1), encoding="utf-8")
    ini.write_text(pio.replace(PIO_OLD, PIO_NEW, 1), encoding="utf-8")
    print("SUCCESS: enabled IR receiver INPUT_PULLUP on GPIO42")
    print("SUCCESS: pinned FastLED to 3.10.3 (fixes FP macro conflict)")
    print("NOTE: this IR workaround is not guaranteed to resolve all RX power/RMT issues")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python apply_ir_fix.py /path/to/Bruce-1.16.1")
    try:
        main(Path(sys.argv[1]).resolve())
    except Exception as e:
        raise SystemExit(f"Patch FAILED: {e}")
