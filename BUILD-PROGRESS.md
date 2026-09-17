# Ankur customised Endgame build progress

Status: hardware, policy and first full Zephyr integration implemented; compiler
iteration in progress.
No new final UF2 is ready to flash. Automatic builds of intermediate checkpoints
are development checks, not finished firmware.

## Baseline

Verified branch tip: 099d79d8bd7800169dc5b1b86ee6f17aea4d2d75 (upstream rare boot fix).
Work only on ankurs-customised-endgame-firmware. Preserve main, other variants, existing dongle firmware and radio configuration. Do not reuse abandoned custom code as the baseline.

## Authoritative inputs

REQUIREMENTS.md records firmware inputs.docx plus subsequent confirmed conversation
decisions; these supersede the older v3 release notes.

## Checkpoints

- [x] Verify remote baseline and isolate clean checkout from abandoned dirty checkout.
- [x] Save complete requirements and acceptance criteria.
- [x] Implement 15 active-low buttons, pull-ups, debounce and Studio layout.
- [x] Remove IO1/IO2 RGB probe and IO3 output initialization conflicts.
- [x] Define and host-test sensitivity, persisted-value validation and feedback policy.
- [x] Implement five layers, protected actions, macro, drag lock and sensitivity controls.
- [x] Implement the first asynchronous feedback, persistence, scroll-mode and lifecycle integration.
- [ ] Obtain passing builds from the exact-name production and debug Actions (workflows added).
- [ ] Inspect compile results, run automated tests and produce SHA256SUMS.txt.
- [ ] User hardware acceptance testing.

## Resume procedure

Read this file and requirements first. Inspect branch HEAD and working-tree changes before editing. Inspect existing Actions results before rerunning builds. Commit and push coherent stages; record failures and pending tests honestly. Temporary workspace files are not durable checkpoints.

## Latest verification and next work

Six hardware contract tests pass. Policy C tests pass with address/undefined-behavior
sanitizers; local LeakSanitizer needed disabling because the executor runs under
ptrace (the policy has no allocations). CI keeps the standard sanitizer run.
The first checkpoint's build failed at keymap line 25 because empty
DECLARE_ENCODERS expansions left stray semicolons. Commit a9ac8e4 removes those
invocations and adds a regression check. Commit 40c8dba then removed obsolete
encoder sensor behaviors that referenced the no-longer-defined follower behavior.
The contract job is green through 2485463. Its first integrated build reached
devicetree and Kconfig successfully, then stopped because Zephyr 3.5 does not
provide `zephyr_link_options`; the ESB module demonstrates the compatible
`zephyr_link_libraries` form used by the follow-up. The next checkpoint also adds
the exact-name production/debug workflows and packages a checksum beside each UF2.
The first exact-name runs progressed into compilation. Production exposed the
upstream UART log backend being enabled without a console when only Studio USB RPC
is selected; production now explicitly disables that backend. Debug exposed an
unused adaptive-feedback header relying on configuration symbols from the disabled
legacy feedback engine; the unused include is removed.
The second exact-name runs reached compilation again. Production additionally
needed the legacy UART console disabled (Studio RPC still retains USB CDC ACM).
Debug exposed a bug in the pinned EC11-ish module: it compiles its source even
when the encoder driver is disabled, but hides its log-level symbol. The board
keeps both encoder drivers disabled and supplies only that missing compile-time
constant; no encoder device nodes or GPIO ownership are restored.
The third runs advanced further. Production showed that the UART console itself
must also be disabled when the production build has no logging overlay. Debug
found a namespace collision between the custom settings callback and Zephyr's
global `settings_load()` API; the callback now has a module-specific name.
The fourth runs exposed three integration details: production's serial shell was
re-enabling the console, a full Studio RPC header was included before its generated
nanopb header existed, and devicetree omitted an upstream processor referenced only
from C. Production disables only the serial shell backend (the ESB relay remains),
the settings-reset hook uses the pinned minimal iterable-section ABI, and the custom
mode node now carries explicit phandle dependencies for every dynamic processor.
The fifth runs revealed two inherited configuration assumptions. The board selects
a console even in production, so a production-only snippet points that otherwise
silent console at the existing Studio CDC device while keeping logging and printk
off. Disabling the old adaptive feedback also removed the implicit `LED_STRIP`
selection; the custom feedback engine now selects it explicitly so the stock
WS2812 driver is instantiated.

Next: iterate on compiler diagnostics until both exact-name workflows pass, then
complete automated review and record the remaining physical test matrix.
Use clean-endgame checkout, not the abandoned modified-config worktree. The
firmware-clean worktree holds the earlier hardware stage only. Dependencies inspected:
ZMK 50d5707901f3dd89f967cf4fee90735758fd9766, bistable 652ae06, ESB
89b695a9aca6dc5a7daf4488140ef46e19fc266c. Preserve these pinned versions.
ESB routing selects the final BLE profile and needs explicit wired-USB handling;
ZMK smooth-scroll conversion must be checked to avoid double scaling. Studio
factory reset supports ZMK_RPC_SUBSYSTEM_SETTINGS_RESET for custom preferences.

## Required deliverables

Actions/artifacts named ankurs-customised-endgame-production and ankurs-customised-endgame-debug; corresponding .uf2 files with identical functionality except USB debugging. Neither a successful compile nor automated tests replace physical validation.
