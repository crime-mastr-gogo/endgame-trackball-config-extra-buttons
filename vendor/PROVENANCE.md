# Feedback module provenance

`adaptive-feedback/` is based on efogdev/zmk-adaptive-feedback commit
`b0a9f02949c132d82e1740371f43a1aa11c12950`.
It is vendored so this branch can make narrow integration fixes without
changing an upstream repository or any existing user branch.

Custom changes route vibration through `src/feedback.c`, gate LED output,
serialize animation/event updates, ignore incompatible legacy persisted event
structures, and exclude the transparent standard-scroll layer from indications.
The adaptive-feedback shell editor is disabled in this build; compiled event
definitions and validated `ankur/v1` preferences are authoritative.

The `fbc_*` compatibility interface follows efogdev/zmk-feedback-common
`92251ce`. Its implementation is replaced by a copied-pattern, bounded,
single-owner driver with a dedicated work queue.

Devicetree bindings are exposed under the repository's root `dts/` and
`include/dt-bindings/` because Zephyr discovers these before module CMake runs.
If updating the vendored bindings, update those exposed copies too.
