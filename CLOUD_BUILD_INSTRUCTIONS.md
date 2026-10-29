# GitHub-hosted build: no PlatformIO or compilation on SteamOS

This is a source-and-build bundle, **not a compiled firmware binary**. It has not been tested on an M5StickS3. A successful GitHub Actions build does not by itself establish that IR works.

1. Create a new GitHub repository under your account (private is fine). Upload `apply_ir_fix.py` and `sticks3_rmt_ir.h` to its root, and upload `.github/workflows/build-bruce-sticks3-rmt.yml` to that same directory path (not into a subfolder of another folder). If the ZIP extraction created a `bruce_sticks3_rmt_fix` parent folder, upload its *contents*, not the parent folder.
2. In GitHub, select **Actions** > **Build Bruce M5StickS3 IR RMT 1.16.1** > **Run workflow**. Wait for the build result. GitHub's cloud runner downloads and compiles Bruce; SteamOS does not need PlatformIO.
3. If the build finishes successfully, select the run's **Artifacts** section and download `Bruce-M5StickS3-RMT-1.16.1-UNTESTED.zip`. Inside is `firmware.bin`. Keep your original official Bruce 1.16.1 image for recovery.
4. If the build fails, open the failed job's logs for the exact compile errors. The patch is experimental and may need updates to match compiler and library APIs.

M5Stack official RMT implementation for StickS3: https://docs.m5stack.com/en/arduino/m5sticks3/ir_nec

**Flashing:** The GitHub Actions artifact is an ESP32 application image, not automatically a full-flash factory image. Only flash using the correct tool and known application offset (e.g., preserving correct bootloader/partition table). Do not use an unverified address or claim hardware testing.
