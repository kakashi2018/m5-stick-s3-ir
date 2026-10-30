# Bruce 1.16.1 M5StickS3 IR — RMT + receiver power V2 (EXPERIMENTAL)

This is a source/build package, **NOT** a verified working firmware binary. It applies only to the exact upstream Bruce 1.16.1 tag.

## What this changes

- Uses ESP-IDF 5 **RMT receiver** on the selected IR Rx GPIO (select Default GPIO42 in Bruce), rather than IRremoteESP8266 GPIO IRQ decode. RMT captures generic raw pulses for learning and saving.
- Native RMT raw retransmission via configured IR TX GPIO (select Default GPIO46). Parsed IR protocols and TV-B-Gone use Bruce's existing transmitter code.
- Calls `M5.Speaker.end()` and separately turns off the M5PM1 PA enable to prevent IR RX electrical interference.
- Keeps StickS3 external IR power on while inside the Infrared menu, even when USB charging is detected by the status bar, then restores normal handling when leaving.
- Waits for the receiver to stabilize, enables INPUT_PULLUP, prints RMT diagnostics to Serial.
- Pins FastLED 3.10.3 to avoid the Bruce `FP` macro conflict.

## Install to your existing GitHub repo

Replace these **three root/repo files** with extracted files of the same names:

1. `apply_ir_fix.py`
2. `sticks3_rmt_ir.h`
3. `.github/workflows/build-bruce-sticks3-rmt.yml`

Commit to default branch. Actions > Build Bruce M5StickS3 RMT IR + Power Fix V2 > Run workflow.

After successful build, download the artifact; extract `Bruce-m5stack-sticks3.bin`. This is a **merged firmware** for ESP32-S3; flash at address **0x0**. Back up your original image/config and keep the stock Bruce release as recovery.

**Important:** This patch has not been compiled or tested on physical StickS3. The cloud workflow will catch compiler errors; only a hardware test can verify IR reception.

## Hardware check

1. Test first with USB disconnected (battery); this avoids charging-state interference.
2. Infrared > Config: RX = Default GPIO42; TX = Default GPIO46.
3. Infrared > IR Read > Custom Read: use a genuine IR remote at 40–60 cm; avoid bright sunlight and pointing at the display instead of receiver.
4. If still no signal, attach Serial Monitor at 115200 and check for `StickS3 RMT IR RX pin=42, power=1` or any `ERROR: ... RMT` line.
5. For a definitive hardware baseline, compare with M5Stack's official StickS3 IR RMT Receiver Arduino example: https://docs.m5stack.com/en/arduino/m5sticks3/ir_nec

## Limitations

- This adapts raw IR capture/replay, not the existing full protocol-aware decoder. IR Read will save RAW codes on this target.
- The earlier INPUT_PULLUP-only build cannot implement RMT and should not be mistaken for this firmware.
- RMT RX might be affected by other concurrently active RMT users, and the firmware can require further debugging.
