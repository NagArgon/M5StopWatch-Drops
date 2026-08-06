# M5StopWatch-Drops

IMU-driven physics toy apps for the M5Stack StopWatch (ESP32-S3, 1.75" round AMOLED).

Tilt the device and watch your badge images tumble and pile up as round drops!

## Apps

### DROPS

Turns the images registered in the built-in Badge app into round "drops" that roll and
pile up under real gravity, sensed by the BMI270 IMU.

- Physics: circle-circle collision with low restitution (drops pile up rather than bounce),
  rolling rotation on contact, circular arena matching the round display
- Drops are baked from your Badge images (`/spiflash/badge/slot_N.jpg`) at app launch,
  center-cropped into circles. Falls back to pastel color balls with card-suit marks
  when no badge image is registered
- **Tap**: add a drop (up to 20) / **Long press**: remove a drop
- **Button A/B**: cycle drop skins — mix (a different image per drop) → each image → color balls
- Drop size auto-adjusts so that about half of the arena stays filled
- Rendering bypasses LVGL: drops are composited off-screen with M5GFX `pushRotateZoom`
  and the full frame is pushed to the panel in one DMA transfer, avoiding the banded
  partial-update artifacts of LVGL software rendering

### UP

A minimal tilt indicator: an arrow that always points to the real-world "up"
(opposite of gravity), with tilt angle readout and a FLAT state when the device lies flat.
Built first to verify the IMU axis mapping; kept as a bonus app.

## Demo

https://x.com/JANK_Hurrymoon/status/2085164394531668334

## Hardware

- [M5Stack StopWatch](https://shop.m5stack.com/products/m5stack-stopwatch-dev-kit-esp32-s3) (ESP32-S3 + 1.75" round AMOLED touch, BMI270 IMU)
- USB-C cable

## Build

### Tool Chain

[ESP-IDF v5.5.4](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/index.html)

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Build & Flash

```bash
idf.py build
idf.py flash
```

## Based On

This firmware is based on the official
[m5stack/M5StopWatch-UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo) (MIT License).
The `DROPS` (`main/apps/app_drops/`) and `UP` (`main/apps/app_up_indicator/`) apps are
original work added on top of it, along with rendering/performance tweaks
(compiler optimization, internal-RAM LVGL draw buffers, dual-core draw units).

## License

MIT License — see [LICENSE](LICENSE).

## Acknowledgments

- [m5stack/M5StopWatch-UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo) — base firmware, HAL and app framework
- [Forairaaaaa/mooncake](https://github.com/Forairaaaaa/mooncake) / [smooth_ui_toolkit](https://github.com/Forairaaaaa/smooth_ui_toolkit) — app framework & UI toolkit
- [M5GFX](https://github.com/m5stack/M5GFX) (LovyanGFX) — fast sprite rotation/composition
