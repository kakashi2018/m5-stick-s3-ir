# Bruce 1.16.1 M5StickS3 — experimental native-RMT IR learn/replay source patch

## IMPORTANT — this is not a flashable `.bin`

The original uploaded `Bruce-m5stack-sticks3.bin` is byte-for-byte identical to Bruce's official
**1.16.1** asset (SHA-256 `f4da587db0275e334231de0d458b96285d4cf9f223cf53c462b265ca66e32c4b`).
The application is compiled Xtensa machine code. Retrofitting M5Stack's RMT peripheral
into that executable is not a safe one-byte modification, so this package **does not alter the binary**.

This package provides an experimental **source-code patch** targeting Bruce **1.16.1 exactly**.
It has not been compiled or exercised on a physical M5StickS3 in this environment. Build it
and test it before treating it as fixed.

## What the patch changes

- Compiles a StickS3-specific RMT backend only for the `m5stack-sticks3` build target.
- Uses native **RMT reception** on the configured IR RX GPIO, normally **GPIO42**.
- Restores `INPUT_PULLUP` and allows 300 ms for the receiver supply to stabilize.
- Enables the external power rail and disables the internal speaker amplifier while receiving.
- Learns and saves received commands in Bruce's `type: raw`, `frequency: 38000`, `data: ...` format.
- Uses native hardware **RMT TX** for raw replay (normally GPIO46, 38 kHz carrier).
- Leaves TV-B-Gone and *parsed protocol* TX paths unchanged. It does **not** guarantee that
  every parsed code or protocol transmits correctly. Other boards are unchanged.

## Apply + compile on Linux (e.g. SteamOS)

Install Python 3, Git, PlatformIO and build dependencies. Then, from a terminal:

```bash
git clone --branch 1.16.1 --depth 1 https://github.com/BruceDevices/firmware.git bruce-1.16.1
python3 /path/to/this-folder/apply_ir_fix.py bruce-1.16.1
cd bruce-1.16.1
pio run -e m5stack-sticks3
```

Expected compiled application path if the build succeeds:

```text
.pio/build/m5stack-sticks3/firmware.bin
```

To program with PlatformIO on a connected board:

```bash
pio run -e m5stack-sticks3 -t upload
```

**Back up your existing configuration/data before flashing.** If using a `.bin` web flasher,
check its required firmware format and address first. Don't treat an app-only binary as a full
flash image, and don't use a guessed offset.

## Tests on hardware

1. Boot the new firmware. Open **Infrared > IR Read > Custom Read**.
2. Point a remote at the M5StickS3 from roughly 30–50 cm. Capture a button; avoid testing at
   point-blank range. M5Stack recommends facing the IR receiver directly.
3. Save it as a raw command, then use the on-screen emulate option to replay it. Also load it
   under **Custom IR** to confirm saved raw replay.
4. Verify TV-B-Gone still works. If RMT initialization fails, inspect the Serial Monitor.
5. Also test with USB disconnected if IR RX appears idle; the StickS3 power rail can behave
   differently with USB connected.

## Caveats and limitations

- **Experimental and unverified.** This is a best-effort integration of the manufacturer's
  recommended ESP-IDF 5 RMT approach into Bruce's current UI. Some protocols need more than
  the captured raw pulse train, or a different modulation carrier frequency.
- Captured codes default to 38 kHz, because a demodulated IR RX sensor does not disclose the
  original carrier frequency. Most ordinary consumer remote protocols use around 38 kHz.
- If the RMT channel cannot be allocated due to a conflict, the existing IR receive routine is
  deliberately not used as a fallback, because that routine is reportedly unreliable on StickS3.
- The patch applies only to exact official 1.16.1 source files; it validates their git blob SHA
  before modifying any of them, to avoid silently corrupting other releases.

## Technical references

- M5Stack official StickS3 IR example: https://docs.m5stack.com/en/arduino/m5sticks3/ir_nec
- Bruce hardware issue and pull-up regression: https://github.com/BruceDevices/firmware/issues/2730
- Bruce Custom IR TX issue: https://github.com/BruceDevices/firmware/issues/2423
