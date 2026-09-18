# Ankur customised Endgame build progress

## Current baseline

Known-good customised firmware baseline:

`63b5cf7b213d1db459f3c289174508f48cbf9c75`

Known-good ESB lifecycle correction checkpoint:

`032a86be19c4cb2a258d5d6ff8283f91aacd9f30`

At the ESB checkpoint, production, debug and contract-check workflows all passed.

## Current improvement pass

The next commit applies the 18 September reliability/performance/cleanup pass:

- complete USB/BLE HID clearing and neutral mouse-report transmission
- BLE disconnect lifecycle cleanup
- factory-reset HID cleanup
- improved shutdown/settings error handling
- preserved ESB lifecycle corrections
- feedback rail power gating with a single GPIO owner
- pointer hot-path scroll-mode cache
- removal of obsolete encoder/adaptive-feedback/follower/auto-hold/BLE-shell code
- ESB shell relay disabled while preserving ESB HID/reliability features
- removal of stale behaviors and dead source/bindings
- full 40-character direct dependency pins
- immutable GitHub Action and build-container pins
- linker-reported flash/RAM limits
- serious compiler/Kconfig diagnostic checks
- expanded host contract/timing tests

## Firmware requirements

`REQUIREMENTS.md` remains authoritative.

The Drag Scroll processing chain remains the proven EFog chain:

XY scaler -> axis clamper -> XY-to-scroll mapper -> Y inversion ->
drag-scroll acceleration -> scroll rotation.

The custom firmware differs only in activation: a 300 ms hold on the top-right
button activates Drag Scroll instead of EFog's dedicated scroll layer.

## Required workflows

- `18 SEPT CODE IMPROVEMENTS`
- `18 SEPT CODE IMPROVEMENTS DEBUG`
- `18 SEPT CODE IMPROVEMENTS CONTRACT CHECKS`

Artifacts remain:

- `ankurs-customised-endgame-production`
- `ankurs-customised-endgame-debug`

A green build is required before flashing, and automated tests do not replace
physical trackball acceptance testing.
