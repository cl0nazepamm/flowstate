# Walker

A floating parameter panel for 3ds Max 2026. Spawns at your cursor, lets you edit object and modifier parameters without the command panel.

## What it does

Press a mouse side button and a dark panel appears with editable parameters for the selected object. Scroll wheel to adjust values, type to override, close when done. Settings persist across sessions.

For Editable Poly, Walker detects your last operation (Connect, Bridge, Extrude, etc.) and shows only those params with live preview through the EPoly preview system.

## Controls

| Input | Action |
|-------|--------|
| **XButton2** (back mouse) | Toggle Walker panel |
| **XButton1** (front mouse) over Max spinner | Add that parameter to Walker |
| **XButton1** over Walker param | Pin/unpin (★ marker) |
| **Scroll wheel** | Adjust value |
| **Shift+scroll** | 10x coarser |
| **Ctrl+scroll** | 10x finer |
| **Type + Tab/Enter** | Edit value (auto-applies on blur) |
| **Click group header** | Collapse/expand group |
| **Ctrl+click param** | Hide parameter |
| **Shift+click group header** | Unhide all in that group |
| **Right-click panel** | Unhide all hidden params |
| **Escape** | Cancel and close (reverts EPoly preview) |
| **Click outside** | Accept and close |
| **Drag header** | Move panel |

Also bindable via **Customize > Keyboard** — search for "Walker" category.

## How parameters are collected

1. **EPoly operation detection** (one-shot) — if you just did a Connect, Bridge, Extrude, Bevel, Chamfer, Inset, or Outline, Walker shows only those operation params. Uses the EPoly preview system for live feedback. Detected once, forgotten after close.

2. **Modifiers** — Bend, Taper, Twist, Shell, TurboSmooth, etc. All scalar params (float, int, bool) from their param blocks.

3. **Base objects** — Box, Sphere, Cylinder, etc. Length, width, height, segments, and so on.

4. **Pinned params** — any parameter you added via XButton1 on a spinner in the command panel. Always shown, persists across sessions.

Editable Poly base objects are skipped in generic collection (their param block has 100+ internal settings). Use XButton1 on spinners in the command panel to add specific EPoly params.

## Persistence

Settings saved to `Walker.cfg` in Max's plugcfg directory:
- Collapsed group states
- Pinned parameters
- Hidden parameters

Survives Max restarts.

## Building

Requires:
- 3ds Max 2026 SDK
- Visual Studio 2022 Build Tools
- CMake

```
build.bat
```

Builds the plugin and deploys `Walker.gup` to the 3ds Max plugins folder. Auto-elevates to admin for the copy.

## Architecture

Single-file C++ GUP plugin (`src/dllmain.cpp`). No dialog resources, no UI frameworks.

- **UI** — Win32 popup window, GDI double-buffered painting, subclassed EDIT controls with dark theme
- **Param detection** — `IParamBlock2` enumeration across modifier stack, `EPoly::getParamBlock()` for operation params
- **Spinner grab** — `WindowFromPoint` + `IParamMap2::GetHWnd()` dialog matching + `ParamDef::ctrl_IDs` to identify which param a spinner belongs to
- **EPoly preview** — `epfn_preview_begin` / `invalidate` / `accept` / `cancel` via FPInterface dispatch
- **Hotkey** — Max ActionTable system + `WH_MOUSE_LL` low-level hook for mouse side buttons
- **Config** — Line-based file with `C:` (collapsed), `P:` (pinned), `H:` (hidden) prefixed keys
