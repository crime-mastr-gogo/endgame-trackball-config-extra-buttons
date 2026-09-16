# Ankur's Customised Endgame

Clean 15-button firmware derived from the official efog configuration at
`099d79d8bd7800169dc5b1b86ee6f17aea4d2d75`. This work exists only on the
`ankurs-customised-endgame` branch; the original and earlier experiment
branches are retained unchanged.

## Hardware contract

- Fifteen inputs: eight original buttons, Left A/B, Right A/B, IO1/2/3.
- All seven added switches are active-low with internal pull-ups.
- Logical order: original eight, Left A, Left B, Right A, Right B, IO1, IO2,
  IO3. Studio draws Left B before Left A to match the physical view.
- Former encoder inputs are independent direct scanners; no encoder driver is
  built and IO1/IO2 are never probed as LED hardware.
- IO3 is never driven as an output.
- 10 ms press and 15 ms release debounce.

## Controls

The default layer implements Copy/Snipe, Paste/Ball-scroll, Delete, Enter,
left/right/middle click, Screenshot, Drag Lock, and the three momentary control
layers. Control and Device layers contain sensitivity, Bluetooth, scrolling,
Studio, reset, clear and power actions. Status and Feedback layers provide
reports and independent LED/vibration toggles.

Destructive actions execute only after the same button has remained pressed
for at least two seconds and is then released. A connection or activity-state
transition invalidates an armed action. This protects power-off, sensitivity
reset and Bluetooth clearing from short or interrupted presses.

## Persistent state

One versioned six-byte `ankur/v1` record stores pointer level, twist level,
scroll mode, LED enable and vibration enable. It is validated before use.
Changes are coalesced for 2.5 seconds and unchanged records are not rewritten.

Pointer and twist sensitivity each expose 20 bounded levels. The defaults are
pointer level 7 (`0.20`) and twist level 5 (`0.166667`). Values never wrap.
Standard scrolling is the default and applies a 16x full-notch scaler; the
high-resolution mode omits that scaler.

## Output protections

`src/feedback.c` is the sole owner of the vibration motor and its supply pin.
It copies caller patterns, validates every duration, limits patterns to 32
steps/6 seconds, caps a single step at 600 ms, enforces cooldown, prevents
same-priority restart extension, and permits only higher-priority preemption.
Motor timing runs on a dedicated work queue. Disabling vibration immediately
cancels work and removes motor power.

LED rendering and event changes are serialized. The motor supply is separate
from the LED supply. Outputs remain muted until saved settings have loaded and
are shut down for idle/sleep. Critical alerts and protected operations have
priority over ordinary status feedback. Status patterns are bounded and built
on the stack because the motor service copies them synchronously.

Drag Lock is forcibly released on normal left-click, endpoint/profile/USB
changes, idle/sleep and power-off so a stuck mouse button is not carried across
a lifecycle transition.

## Verification

Run `python3 -m unittest discover -s tests -v`. The suite checks the physical
mapping, pull-ups, all eight 15-binding layers, saved-state validation and
limits, destructive-action classification, and motor copying/cooldown/
preemption/cancellation behavior. GitHub Actions also builds the complete ZMK
firmware and stores it as the `ankurs-customised-endgame` artifact.

Production builds retain the Studio USB-UART transport but omit USB debug
logging and the unused encoder-follower module to preserve flash and RAM
headroom.

Compilation and host regression tests cannot prove electrical safety, sleep
current, sensor operation, Bluetooth/ESB range or physical switch behavior.
Those require the final hardware test. Keep the known-working UF2 and restore
ZMK Studio stock settings after changing to this different 15-button layout.
