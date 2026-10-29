#!/usr/bin/env python3
"""Apply StickS3 RMT raw learn/replay patch to an EXACT Bruce 1.16.1 source checkout.

Usage: python3 apply_ir_fix.py /path/to/firmware
No firmware binary is patched. This edits 4 source files and copies 1 header.
"""
import hashlib
from pathlib import Path
import sys

EXPECTED = {
    'src/modules/ir/ir_read.cpp': '2f9cc95680dc2b6484718dcf130a320f73ac7997',
    'src/modules/ir/ir_read.h': 'eacf0754a18aa5103c05617187cbac254431170e',
    'src/modules/ir/custom_ir.cpp': '3e32ce009c389aa653cd86d205ee1a76847889af',
    'boards/m5stack-sticks3/m5stack-sticks3.ini': 'acea8ca5b1f957232b67e997427e20a66dc83c90',
}

def git_blob_hash(data):
    return hashlib.sha1(f'blob {len(data)}\0'.encode() + data).hexdigest()

def replace_one(s, before, after, path):
    n = s.count(before)
    if n != 1:
        raise ValueError(f'{path}: expected exactly one match, found {n}: {before[:120]!r}')
    return s.replace(before, after, 1)

def patch(root):
    targets = {}
    for rel, sha in EXPECTED.items():
        fn = root / rel
        data = fn.read_bytes()
        if git_blob_hash(data) != sha:
            raise ValueError(f'{rel} is not the verified Bruce 1.16.1 source (SHA mismatch)')
        targets[rel] = data.decode('utf-8')
    header = root / 'src/modules/ir/sticks3_rmt_ir.h'
    if header.exists():
        raise ValueError(f'{header} already exists: stopping rather than overwriting')

    r = 'src/modules/ir/ir_read.cpp'
    s = targets[r]
    s = replace_one(s, '#include "ir_read.h"', '#include "ir_read.h"\n#ifdef STICKS3_RMT_IR\n#include <M5Unified.h>\n#endif', r)
    s = replace_one(s, '    irrecv.enableIRIn();', '''#ifdef STICKS3_RMT_IR
    // Manufacturer-required native RMT backend, not GPIO interrupt decoding.
    M5.Speaker.end();                       // Speaker amp interferes with IR RX
    M5.Power.setExtOutput(true, m5::ext_none); // Power StickS3 IR receiver
#else
    irrecv.enableIRIn();
#endif''', r)
    s = replace_one(s, '    setup_ir_pin(bruceConfigPins.irRx, INPUT);\n    if (headless) return;', '''#ifdef STICKS3_RMT_IR
    setup_ir_pin(bruceConfigPins.irRx, INPUT_PULLUP); // Restore 1.15 behavior
    delay(300); // Power-rail/receiver startup stabilization
    if (!stickRx.begin(bruceConfigPins.irRx)) {
        Serial.println("ERROR: unable to initialize StickS3 RMT IR RX");
    } else if (!stickRx.arm()) {
        Serial.println("ERROR: unable to arm StickS3 RMT IR RX");
    }
#else
    setup_ir_pin(bruceConfigPins.irRx, INPUT);
#endif
    if (headless) return;''', r)
    s = replace_one(s, 'void IrRead::begin() {\n    _read_signal = false;', 'void IrRead::begin() {\n    _read_signal = false;\n#ifdef STICKS3_RMT_IR\n    if (!stickRx.arm()) Serial.println("ERROR: StickS3 RMT capture restart failed");\n#endif', r)
    s = replace_one(s, '''void IrRead::read_signal() {
    if (_read_signal || !irrecv.decode(&results)) return;''', '''void IrRead::read_signal() {
#ifdef STICKS3_RMT_IR
    if (_read_signal) return;
    String captured;
    if (!stickRx.poll(captured)) return;
    raw = true; // Preserve any protocol without relying on GPIO-ISR decoding.
    _read_signal = true;
    _captured_raw_signal = captured;
    display_banner();
    padprint("RAW Data Captured:");
    tft.println(captured.substring(0, 45) + (captured.length() > 45 ? "..." : ""));
    display_btn_options();
    delay(200);
    return;
#endif
    if (_read_signal || !irrecv.decode(&results)) return;''', r)
    s = replace_one(s, '    _captured_raw_signal = "";\n    irrecv.resume();\n    begin();', '''    _captured_raw_signal = "";
#ifndef STICKS3_RMT_IR
    irrecv.resume();
#endif
    begin();''', r)
    s = replace_one(s, '''String IrRead::parse_raw_signal() {

    rawcode = resultToRawArray(&results);''', '''String IrRead::parse_raw_signal() {
#ifdef STICKS3_RMT_IR
    return _captured_raw_signal;
#else
    rawcode = resultToRawArray(&results);''', r)
    s = replace_one(s, '''    signal_code.trim();

    return signal_code;
}

void IrRead::append_to_file_str''', '''    signal_code.trim();

    return signal_code;
#endif
}

void IrRead::append_to_file_str''', r)
    s = replace_one(s, '''String IrRead::loop_headless(int max_loops) {

    while (!irrecv.decode(&results))''', '''String IrRead::loop_headless(int max_loops) {
#ifdef STICKS3_RMT_IR
    while (!stickRx.poll(_captured_raw_signal)) {
        if (--max_loops <= 0) { Serial.println("timeout"); return ""; }
        delay(1000);
    }
    raw = true;
    String output = "Filetype: IR signals file\\nVersion: 1\\n#\\n#\\n";
    strDeviceContent = "";
    append_to_file_str("Unknown");
    return output + strDeviceContent;
#else
    while (!irrecv.decode(&results))''', r)
    s = replace_one(s, '''    return r;
}

bool IrRead::write_file''', '''    return r;
#endif
}

bool IrRead::write_file''', r)
    # .save_device() contains a second, later resume; protect that too.
    s = replace_one(s, '''    irrecv.resume();
    begin();
}

String IrRead::loop_headless''', '''#ifndef STICKS3_RMT_IR
    irrecv.resume();
#endif
    begin();
}

String IrRead::loop_headless''', r) if '''    irrecv.resume();
    begin();
}

String IrRead::loop_headless''' in s else s
    targets[r] = s

    r = 'src/modules/ir/ir_read.h'
    s = targets[r]
    s = replace_one(s, '#include <globals.h>', '#include <globals.h>\n#ifdef STICKS3_RMT_IR\n#include "sticks3_rmt_ir.h"\n#endif', r)
    s = replace_one(s, '    IRrecv irrecv = IRrecv(bruceConfigPins.irRx, SAFE_STACK_BUFFER_SIZE / 2, 50);', '''    IRrecv irrecv = IRrecv(bruceConfigPins.irRx, SAFE_STACK_BUFFER_SIZE / 2, 50);
#ifdef STICKS3_RMT_IR
    StickS3RmtReceiver stickRx;
#endif''', r)
    targets[r] = s

    r = 'src/modules/ir/custom_ir.cpp'
    s = targets[r]
    s = replace_one(s, '#include "custom_ir.h"', '#include "custom_ir.h"\n#ifdef STICKS3_RMT_IR\n#include "sticks3_rmt_ir.h"\n#endif', r)
    # Avoid bringing up LEDC/PWM driver on the StickS3 native-RMT raw TX path.
    s = replace_one(s, '''    IRsend irsend(bruceConfigPins.irTx); // Set the GPIO to be used to sending the message.
    irsend.begin();
    if (!hideDefaultUI) { displayTextLine("Sending.."); }

    uint16_t dataBufferSize''', '''#ifndef STICKS3_RMT_IR
    IRsend irsend(bruceConfigPins.irTx);
    irsend.begin();
#endif
    if (!hideDefaultUI) { displayTextLine("Sending.."); }

    uint16_t dataBufferSize''', r)
    s = replace_one(s, '''    // Send raw command
    irsend.sendRaw(dataBuffer, count, frequency);

    if (bruceConfigPins.irTxRepeats > 0) {
        for (uint8_t i = 1; i <= bruceConfigPins.irTxRepeats; i++) {
            irsend.sendRaw(dataBuffer, count, frequency);
        }
    }

    free(dataBuffer);''', '''    // Native RMT TX on StickS3; existing IRsend path stays intact elsewhere.
#ifdef STICKS3_RMT_IR
    if (!sticks3RmtSendRaw(bruceConfigPins.irTx, dataBuffer, count, frequency,
                          bruceConfigPins.irTxRepeats)) {
        Serial.println("ERROR: StickS3 RMT raw IR TX failed");
    }
#else
    irsend.sendRaw(dataBuffer, count, frequency);
    if (bruceConfigPins.irTxRepeats > 0) {
        for (uint8_t i = 1; i <= bruceConfigPins.irTxRepeats; i++) {
            irsend.sendRaw(dataBuffer, count, frequency);
        }
    }
#endif
    free(dataBuffer);''', r)
    targets[r] = s

    r = 'boards/m5stack-sticks3/m5stack-sticks3.ini'
    s = targets[r]
    s = replace_one(s, '\t-DCORE_DEBUG_LEVEL=1', '\t-DCORE_DEBUG_LEVEL=1\n\t-DSTICKS3_RMT_IR=1', r)
    targets[r] = s

    # All checks succeeded. Only now mutate the source checkout.
    for rel, contents in targets.items():
        (root / rel).write_text(contents, encoding='utf-8')
    header.write_bytes((Path(__file__).parent / 'sticks3_rmt_ir.h').read_bytes())
    print('Patched Bruce 1.16.1 source with StickS3 native RMT raw IR RX/TX.')
    print('Build with: pio run -e m5stack-sticks3')
    print('This patch has not been compiled or hardware-tested here.')

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    try:
        patch(Path(sys.argv[1]).expanduser().resolve())
    except Exception as exc:
        print('No patch applied:', exc, file=sys.stderr)
        sys.exit(1)
