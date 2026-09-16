# Ankur's Customised Endgame

Experimental staged rewrite. Not a completed replacement for the final firmware.

Baseline: official efog config commit 099d79d8bd7800169dc5b1b86ee6f17aea4d2d75.
Existing branches must remain unchanged.

## Checkpoint 1: hardware foundation

- Reuse the proven composite scanner arrangement with separate former-encoder scanners.
- Fifteen physical inputs, seven added pull-ups, 10/15 ms press/release debounce.
- Preserve logical positions: original eight, Left A, Left B, Right A, Right B, IO1, IO2, IO3.
- Studio visual order: Left B, Left A, Right A, Right B, IO1, IO2, IO3.
- Remove encoder sensor nodes and prevent IO1/IO2 probing or IO3 output drive.
- Select RGB hardware explicitly with CONFIG_ANKUR_RGB_PRESENT.
- Keep official first-eight bindings and layers temporarily; added switches use F13–F19.
- Preserve upstream dependencies, USB reporting, bond clearing, and power configuration.

## Verification

Run: python3 -m unittest discover -s tests -v

Static tests do not prove compilation, electrical safety, sleep current or button operation.
The existing GitHub build workflow should compile this branch without publishing a release.
Do not flash until compilation is confirmed. Preserve the working UF2 and Studio settings.
Changing layouts may require Restore Stock Settings in Studio, which discards remapping.

## Pending checkpoints

Final keymap/layers, sensitivity and scrolling, unified feedback ownership,
settings validation, lifecycle cleanup, then complete regression/hardware testing.
The old branch is a reference, not a source to copy wholesale.
