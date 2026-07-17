# Smoke tests

- `config_editor_smoke.py` parses the editor without installing its startup loader, opens it, and verifies that the master side-button switch disables and restores the XButton1 mapping controls.
- `powerparams_multishape_smoke.ms` verifies the PB1 fallback contract for multi-selected shapes without resetting the scene.
- `loop_subdivision_smoke.ms` verifies that the merged modifier evaluates common primitives, Editable Mesh, and Editable Poly inputs.
- `powerparams_multishape_smoke.ms` verifies legacy/PB1 shape discovery and same-class multi-edit fanout for multiple Rectangle objects.
- Run it only in a disposable 3ds Max session: the script resets the current scene without prompting.
- Install only `FlowState.gup` before running it. Remove or disable any legacy `clone_loop.dlm` first because it has the same Class ID.

The constructor probes intentionally remain `CloneLoop()` and `Clone_Loop()`; those are compatibility-facing internal names even though the modifier is displayed as **Loop Subdivision**.
