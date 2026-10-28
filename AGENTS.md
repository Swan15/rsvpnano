# Repository Guidelines

## Project Structure & Module Organization

RSVP Nano is an ESP32-S3 PlatformIO firmware project with companion tooling. Core firmware lives in `src/`, grouped by feature: `app/`, `reader/`, `storage/`, `display/`, `input/`, `sync/`, `rss/`, `update/`, `usb/`, `audio/`, and `timer/`. Native unit tests are under `test/`, with Arduino shims in `test/support/`. Board definitions are in `boards/`. Browser flasher and converter assets are in `web/`; Python release and conversion tools live in `tools/`. The iOS companion app is in `ios/RSVPNanoCompanion/`. Documentation and release notes are in `docs/`; bundled font licenses are in `third_party/`.

## Build, Test, and Development Commands

- `pio run`: build the default `waveshare_esp32s3_usb_msc` firmware environment.
- `pio run -e waveshare_esp32s3`: build the non-USB-MSC firmware variant.
- `pio run -t upload`: flash the default firmware to a connected device.
- `pio device monitor`: open the serial monitor at `115200`.
- `pio test -e native_test`: run the native Unity pacing tests used by CI.
- `python3 tools/export_web_firmware.py --version vX.Y.Z`: build and export release firmware artifacts for `web/firmware/`.

## Coding Style & Naming Conventions

C++ firmware uses `.h`/`.cpp` pairs, PascalCase class names, and descriptive feature filenames such as `ReadingLoop.cpp` or `StorageManager.h`. Keep changes inside existing module boundaries where possible. Use two-space indentation in C++ and web files, matching the source. Python tools should remain Python 3 scripts with clear argument parsing. Swift code follows standard SwiftUI naming: PascalCase types and lowerCamelCase properties/functions.

## Testing Guidelines

Add focused native tests under `test/test_<feature>/test_main.cpp` when logic can run without hardware. Use the Unity framework configured in `platformio.ini`. Keep hardware-dependent behavior isolated so reader, pacing, parsing, and storage decisions can be tested natively. Run `pio test -e native_test` before PRs that change reader pacing or shared firmware logic.

## Commit & Pull Request Guidelines

Recent commits use short, imperative summaries, for example `Add custom themes and pomodoro timer` and `Deploy web flasher from release workflow`. Follow that style and keep release commits explicit, such as `Finalize v0.0.5 release assets`. Pull requests should describe user-visible behavior, list test commands, link related issues, and include screenshots or device photos for display, web, or iOS UI changes.

## Security & Configuration Tips

Do not commit real Wi-Fi credentials, signing identities, provisioning profiles, or local OTA settings. Use `docs/ota.conf.example` as the OTA template. Treat `.pio/` outputs and local build artifacts as disposable unless a release workflow exports them.
