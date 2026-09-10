# Endgame 15-Button Firmware v4.0.0

This release contains the tested final 15-button firmware and its complete keymap and feedback guide.

## Highlights

- Eight ZMK Studio layers in the requested order:
  1. Default
  2. Control Settings
  3. Device Settings
  4. Status Report
  5. Feedback Control
  6. Drag Scroll
  7. Scroll Mode Switch
  8. Fine Cursor Movement Feedback Control
- Descriptive ZMK Studio button labels for status and feedback actions
- Battery charge report on the Status Report layer
  - one long vibration per completed 25%
  - one short vibration per additional completed 5%
  - green, orange, or red LED feedback according to charge range
- Status-report vibration cancellation when another feedback action starts
- Standard and high-resolution scrolling
- Pointer and twist sensitivity controls
- Bluetooth profile, ZMK Studio, device, lighting, and vibration controls
- Complete 10-page PDF guide covering every key, layer, LED indication, and vibration pattern

## Files

- `endgame-final-15-button-firmware-v4.0.0.uf2` — tested firmware image
- `Endgame-15-Button-Firmware-Guide-v4.0.0.pdf` — complete keymap and feedback guide
- `SHA256SUMS.txt` — SHA-256 checksums for the UF2 and PDF
- `BUILD-INFO.txt` — release and source provenance

## Source

Tested source commit: `4fbc394b1ea7bc97e88d9c300831be74ad17828b`

Only the trackball firmware needs to be flashed. The dongle firmware does not need to be changed. After changing layouts, use **Restore Stock Settings** in ZMK Studio, then disconnect and reconnect the trackball.
