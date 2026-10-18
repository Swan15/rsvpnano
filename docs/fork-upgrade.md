# Fork upgrade to v0.1.2

This fork integrates upstream v0.1.2 (`f9d2a80`). The old custom display and timer
implementations have been replaced by upstream's theme files and configurable routines.

## Custom themes and timers

Copy `themes/amber.toml`, `themes/matrix.toml`, and `themes/cyber.toml` to `/themes`
on the microSD card, or upload them through the companion theme manager. Select a
theme on the device. The built-in Default theme replaces the old Classic option.

Fresh timer configurations include Pomodoro (25 minutes work, 5 minutes break)
and Pomodoro 50/10 (50 minutes work, 10 minutes break), both with four rounds.
Existing `/config/focus.toml` routines are retained. Add the second preset through
the device or companion editor if a configuration already exists. See
[configuration](configuration.md#focus-timers) for the TOML format.

## Build the original 3.49-inch LCD board

Initialize the new source dependencies before building:

```sh
git submodule update --init --recursive
pio test -e native_test
pio run -e waveshare_esp32s3_touch_lcd_349_rev1
```

Upstream uses Arduino ESP32 3.3.9 and C++23. The old
`waveshare_esp32s3_usb_msc` environment is now
`waveshare_esp32s3_touch_lcd_349_rev1`, with USB mass storage enabled.
Native tests require a C++23 host compiler. The web companion requires JDK 17
for the CI-compatible `bash ./gradlew checkWeb --no-daemon` check.

Back up the SD card before installing the new firmware. Reader visibility
preferences changed in v0.1.0; review the appearance editor after upgrading.
Builds and source integration do not verify physical display, touch, or power behavior.

The web and mobile companions now live under `companion/`; the old handwritten
`web/` app and standalone `ios/` layout have been replaced. Upstream's Pages
workflow restricts publication to its own repository. Configure fork deployment
before publishing a separate hosted companion. Firmware and asset sources remain
configurable; the defaults still point to upstream. Installing an upstream OTA
build replaces this fork's timer defaults, while SD theme files remain separate.

## Original 3.49-inch board capabilities

The [Waveshare hardware documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.49)
lists 2.4 GHz 802.11 b/g/n Wi-Fi, Bluetooth LE 5, a 172 by 640 capacitive
touchscreen, microSD storage, a six-axis IMU, RTC, battery charging, a speaker
connection, and a dual microphone array with an ES7210 input codec.

RSVP Nano uses Wi-Fi for local-network/direct-hotspot companion sync, RSS
downloads, and OTA updates. It supports RSVP and page reading, chapters, saved
progress, appearance settings, installable fonts and language packs, focus
timers, USB transfer, SD diagnostics, and screensavers.

Microphone hardware is present, but this firmware has no ES7210 driver,
recording path, speech recognition, or voice commands. Its audio interface
currently supports speaker beeps through the ES8311 output codec. Bluetooth
hardware support likewise does not imply a Bluetooth sync or audio feature.
