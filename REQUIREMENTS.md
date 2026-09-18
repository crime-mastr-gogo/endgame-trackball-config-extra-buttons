# Ankur customised Endgame firmware requirements

Authoritative inputs: `firmware inputs.docx` and subsequent user confirmations.
These supersede the older v3.0.0 release notes. Baseline is upstream
099d79d8bd7800169dc5b1b86ee6f17aea4d2d75, including boot and acceleration fixes.
Implementation and testing status belongs in BUILD-PROGRESS.md, not this specification.

## Hardware and timing

Target efogtech_trackball_0 on the previously working PCB revision. Both encoders
are physically removed; LED and motor are stock. All seven added NO switches close
to GND and need active-low inputs with internal pull-ups. Preserve original input
electrical configuration. Debounce press/release: 15 ms each. Copy/fine and
paste/drag-scroll tap-hold: 300 ms. Protected actions: 2000 ms uninterrupted,
cancel on early release, fire once until release. Macro: 1000 ms hold, once per
hold, 30 ms key down and 30 ms inter-key gap.

| Position | Input | GPIO |
|---|---|---|
| 0 | Top left | P0.27 |
| 1 | Top right | P0.26 |
| 2 | Left upper | P0.01 |
| 3 | Right upper | P0.04 |
| 4 | Left lower | P0.21 |
| 5 | Right lower | P0.05 |
| 6 | Bottom left | P0.08 |
| 7 | Bottom right | P0.07 |
| 8 | Left A | P0.17 |
| 9 | Left B | P0.16 |
| 10 | Right A | P1.08 |
| 11 | Right B | P1.09 |
| 12 | IO1 | P0.11 |
| 13 | IO2 | P0.15 |
| 14 | IO3 | P0.20 |

Disable encoder drivers and sensor bindings. Studio displays original eight
unchanged, then Left A, Left B, Right A, Right B, IO1, IO2, IO3 in one row.
All 15 remappable; Studio locking and explicit unlock retained.

## Five user layers and keymap

Priority: Status > Feedback > Device > Control > Default. Layer holds are
momentary. T means transparent; on the base layer it has no action.

| Input | Default | Control | Device | Feedback | Status |
|---|---|---|---|---|---|
| Top left | Copy / hold Fine Cursor | T | T | T | T |
| Top right | Paste / hold Drag Scroll | T | T | T | T |
| Left upper | Delete | T | T | T | T |
| Right upper | Enter | T | T | T | T |
| Left lower | Left click | T | T | T | T |
| Right lower | Right click | T | T | T | T |
| Bottom left | Middle click | T | T | T | T |
| Bottom right | Win+Shift+S | T | T | T | T |
| Left A | Drag Lock toggle | Twist up | Hold power off | Vibration toggle | Twist report |
| Left B | Hold custom string | Twist down | Studio unlock | LED toggle | Profile report |
| Right A | T | Next endpoint | Scroll mode toggle | T | Pointer report |
| Right B | Hold Status | Previous endpoint | Hold clear current BT | T | T |
| IO1 | Hold Feedback | Pointer up | Hold clear all BT | T | Battery report |
| IO2 | Hold Device | Pointer down | T | T | T |
| IO3 | Hold Control | T | Hold reset sensitivities | T | T |

US QWERTY / Windows. Macro literal: `/;.l,kmj?:>L<KMJ` followed by Enter
(16 characters and Enter). Output must be identical whether Caps Lock is ON or
OFF. USB/BLE use host HID indicators; ESB uses the matching fixed-keystring
dongle indicator relay. Never toggle the host's Caps Lock state merely to type
the macro. Ensure modifier release and safe cancellation.
Fine Cursor uses proven 0.25 scaling and remainders; neither held mode changes
saved sensitivity. Drag Scroll retains upstream XY scroll processing. Drag Lock
toggles held left mouse, is not persistent, and releases on inactivity, endpoint
change/loss, reset, shutdown and recovery.

## Connectivity and power

Five Bluetooth profiles plus existing ESB receiver as endpoint six; wrap next /
previous through six. USB provides wired HID and Studio, restoring previous
wireless selection on disconnect. Clear current/all affects BT only, never ESB
pairing. Release HID state before switching. Preserve radio/pairing/channel
configuration. The only intentional dongle firmware exception is the companion
18 SEPT FIXED KEYSTRING HID-indicator relay required to report host Caps Lock
state over ESB. Sleep after 15 minutes battery inactivity. Keep
awake during active wired input or Studio use. On non-active state stop feedback,
cancel macros, release held HID and drag lock; wake restores saved preferences,
not temporary layers or locks.

## Scroll and sensitivity

Factory scroll mode standard; selectable high resolution at equivalent physical
scroll speed. Persist mode across sleep/power; factory reset restores standard.
20 bounded levels; attempted overflow stays at boundary and signals boundary.
Pointer default 7 (0.2), twist default 5 (0.166667). Protected reset restores both.

| Level | Pointer | Twist |
|---|---|---|
|1|0.100000|0.100000|
|2|0.116667|0.116667|
|3|0.133333|0.133333|
|4|0.150000|0.150000|
|5|0.166667|0.166667|
|6|0.183333|0.183333|
|7|0.200000|0.200000|
|8|0.216667|0.216667|
|9|0.233333|0.233333|
|10|0.250000|0.250000|
|11|0.305000|0.325000|
|12|0.360000|0.400000|
|13|0.415000|0.475000|
|14|0.470000|0.550000|
|15|0.525000|0.625000|
|16|0.580000|0.700000|
|17|0.635000|0.775000|
|18|0.690000|0.850000|
|19|0.745000|0.925000|
|20|0.800000|1.000000|

## Feedback contract

LED ceiling 50%; timings in ms alternate motor ON/OFF starting ON. LED effect
lasts entire pattern unless stated. Rainbow means smooth rainbow breathing.
Preferences persist, default ON. Off acknowledges before disabling; on enables
before acknowledging. Respect each channel's preference independently. Effects,
macro and settings work must not block input processing.

| Event | Motor pattern | LED |
|---|---|---|
| Pointer up | 100 150 250 | Red |
| Pointer down | 250 150 100 | Green |
| Pointer min | 100 150 100 150 100 | Rainbow |
| Pointer max | 250 150 250 150 250 | Rainbow |
| Twist up | 100 150 250 | Magenta |
| Twist down | 250 150 100 | Cyan |
| Twist min | 100 150 100 150 100 | Rainbow |
| Twist max | 250 150 250 150 250 | Rainbow |
| Standard scroll | 100 150 150 150 250 | Three green flashes |
| High-res scroll | 250 150 250 150 100 | Three red flashes |
| BT1 | 150 | Red |
| BT2 | 150 150 150 | Purple |
| BT3 | 150 150 150 150 150 | Green |
| BT4 | 150 150 150 150 150 150 150 | Blue |
| BT5 | 150 150 150 150 150 150 150 150 150 | Magenta |
| ESB | 250 150 250 150 250 | Rainbow |
| Control layer | 150 | Blue |
| Device layer | 150 150 150 | Red |
| Feedback layer | 150 150 150 150 150 | Green |
| Status layer | 150 150 150 150 150 150 150 | Magenta |
| Fine Cursor held | None | Rainbow while held |
| Drag Scroll held | None | Rainbow while held |
| Drag Lock | 150 150 150 | Purple |
| Power off | 500 | Red fade to off |
| Studio unlock | 150 150 150 | Cyan |
| Clear current BT | 150 150 150 150 150 | Blue |
| Clear all BT | 250 150 250 150 250 150 250 | Red |
| Reset sensitivities | 150 150 150 150 150 | Rainbow |
| Vibration on | 250 150 250 150 250 | Green |
| Vibration off | 100 150 100 150 100 | Red |
| LED on | 250 150 250 150 250 | Cyan |
| LED off | 100 150 100 150 100 | Orange |
| USB connected | 250 | Green |
| USB disconnected | 150 150 150 | Blue |

Sensitivity reports: 250 ms per group of 5, then 100 ms per remaining level,
150 ms gaps; twist blue, pointer green. Profile report uses endpoint pattern.
Battery rounded down to 5%; 250 ms per 25%, then 100 ms per remaining 5%,
150 ms gaps. LED: >75 dark green, 50–75 light green, 25–49 orange, 0–24 red.
Below 5% still provide LED acknowledgment when enabled.

## Persistence and safety

Save pointer/twist levels, scroll mode and feedback preferences; coalesce changes
and avoid unchanged writes. Validate version/length/ranges and use safe defaults
on invalid storage. Factory reset restores all defaults and releases HID. Keep
upstream sensor mixer, rotation, acceleration, 1 ms synchronization and boot fixes.
No unbounded queues, busy waits, or heap allocations in critical input paths.
Feedback faults must not disable pointing. Preserve bootloader/UF2 recovery.

## Deliverables and acceptance

Two Actions/artifacts/UF2 stems: ankurs-customised-endgame-production and
ankurs-customised-endgame-debug. Identical functions; production no USB debugging,
debug USB diagnostics. Include SHA256SUMS.txt. Check pin conflicts, all inputs,
layers, guards, macros, sensitivity boundaries, remainder behavior, settings reset,
feedback patterns, endpoint transitions, Studio and lifecycle cleanup. Record
build results and test limitations. Hardware feel, latency, motor/LED behavior,
radio reliability and power testing require user confirmation; do not claim
hardware reliability solely from compilation.
