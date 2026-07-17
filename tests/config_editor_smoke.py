"""3ds Max batch smoke test for the FlowState configuration editor.

The startup-loader call is stripped before evaluation so this test never
installs or rewrites user scripts.
"""

from pathlib import Path

from pymxs import runtime as rt


def main() -> None:
    source_path = Path(__file__).resolve().parents[1] / "macros" / "flowstate_config.ms"
    source = source_path.read_text(encoding="utf-8").replace("\r\n", "\n")
    source, marker, trailing = source.rpartition("\nFlowState_InstallStartupLoader()")
    if not marker or trailing.strip():
        raise RuntimeError("Could not isolate the config script's startup-loader call")

    rt.execute(source + "\n")

    form = None
    try:
        rt.macros.run("CloneTools", "flowstate_Config")
        form = rt.flowstate_ConfigForm
        side_keys = rt.flowstate_chkSideKeys
        buttons = rt.flowstate_kmBtns
        labels = rt.flowstate_kmLabels

        side_keys.Checked = False
        if any(bool(control.Enabled) for control in buttons):
            raise RuntimeError("XButton1 mapping buttons remained enabled")
        if any(bool(control.Enabled) for control in labels):
            raise RuntimeError("XButton1 mapping labels remained enabled")

        side_keys.Checked = True
        if not all(bool(control.Enabled) for control in buttons):
            raise RuntimeError("XButton1 mapping buttons did not re-enable")
        if not all(bool(control.Enabled) for control in labels):
            raise RuntimeError("XButton1 mapping labels did not re-enable")

        print("FLOWSTATE_CONFIG_EDITOR_SMOKE_OK")
    finally:
        if form is not None:
            form.Close()


main()
