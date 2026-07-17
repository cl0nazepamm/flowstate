# flowstate

![flowstate](images/flowstate_logo.png)

flowstate 1.3 is a collection of 3ds Max tools for faster parameter editing, shader creation, modifier access, modeling, and mouse-driven viewport workflows.

## Tools

- **Stack** — Opens a floating editor for object, modifier, multi-selection, and Editable Poly command parameters with typing, scrubbing, favorites, and V/H slider slots.
- **Timeline Slider** - Timeline scrubbing anywhere just by dragging. 
- **Automatic Orbit** - Switch between orbit modes. Also registers them to hotkey editor.
- **Opacity Slider** - Change object opacity smoothly by dragging on viewport
- **Shader Search** — Searches materials, texmaps, scene shaders, and OSL categories with pins, bricks, previews, drag/drop, assignment, and SHLL preview generation.
- **Macro Search** — Searches macros and modifiers, noticably faster than standard 3dsMax search
- **Normalize Poly** — Shiva Tools's vertex cleaner but works in modifier level.
- **Smooth Bridge** — Bridge with proper continuity
- **F2 Extend** — Blender style sideways bridge/extend or monorail.
- **Loop Subdivision** - Subdivision surface modifier for triangular meshes
- **Config UI** — Controls modules, sub-object buttons, Slate Tab search, theme, the XButton1 action map, and a master opt-out that releases both mouse side buttons.
- **FlowState.cfg** — Stores all module flags, key mappings, collapsed/hidden parameters, favorites, shader pins, and brick layouts in `<plugcfg>`.


## Shortcuts

| Input | Action |
| --- | --- |
| `XButton2` | Toggle PowerParams |
| `Shift+XButton2` | Toggle PowerShader |
| `Tab` in Slate Material Editor | Toggle PowerShader when enabled |
| `Ctrl+XButton2` | Cycle auto-orbit mode |
| `Alt+Shift+XButton2` | Swap V/H slider assignments |
| `XButton1` combinations | Run the configurable action map |
| Mouse wheel over a value | Scrub the value |
| `Shift` while dragging | Use 10x coarse speed |
| `Alt` while dragging | Use 0.1x fine speed |
| `Enter` or `Tab` while typing | Apply the value |
| `Esc` | Cancel the current operation and close |

## XButton1 defaults

| Modifiers | Action |
| --- | --- |
| None | Screen Grab |
| `Shift` | Time Slider |
| `Ctrl` | Param Slider |
| `Ctrl+Shift` | Opacity Slider |
| `Ctrl+Alt+Shift` | Clear Sliders |
| `Alt` | UV Grab |

Every XButton1 action can be reassigned, mapped to `Alt+Shift` or `Ctrl+Alt`, or turned off in the Config UI.

## Install

1. Copy the matching `FlowState.gup` build to `3ds Max <version>\plugins`.
2. Run `flowstate_config.ms` once to install the user macro and startup loader.
3. Restart 3ds Max after first installation or after replacing the GUP.

Release packages require both `FlowState.gup` and `flowstate_config.ms`.

All native tools and modifiers are exported from `FlowState.gup`. The build does not produce companion `.dlm`, `.dlo`, or secondary `.gup` files. Remove legacy `normalize_poly.dlm` and `clone_loop.dlm` copies when upgrading so Max does not load duplicate classes.

## Build

Requires Visual Studio 2022, CMake 3.20+, and the SDK matching the target 3ds Max version.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DMAX_VERSION=2026
cmake --build build --config Release
```

Set `MAX_VERSION=2027` or override `MAXSDK_PATH` for another supported SDK; the Release output places the GUP and config script together.

### Native source layout

| Area | Sources |
| --- | --- |
| Core module and exports | `src/flowstate.cpp`, `src/FlowState.def` |
| Floating editors | `src/powershader.*`, `src/modstack.*` |
| Modeling commands | `src/normalize_edges/f2_extend_tool.cpp`, `smooth_bridge_tool.cpp` |
| Normalize Poly modifier | `src/normalize_edges/normalize_poly.*` |
| Loop Subdivision modifier | `src/modifiers/loop_subdivision/` |
| Native smoke tests | `tests/` |

The CMake project groups those areas explicitly while linking them into the single `FlowState` target.

## Uninstall

Remove `FlowState.gup`, the installed `flowstate_config.ms` copies/startup loader, and optionally `<plugcfg>\FlowState.cfg`. Legacy standalone `normalize_poly.dlm` and `clone_loop.dlm` files are not part of the collection and can also be removed.

## License

GPL-3.0 with an Autodesk 3ds Max SDK linking exception; see [LICENSE](LICENSE) and [LICENSE-EXCEPTION](LICENSE-EXCEPTION).

Copyright (C) 2026 clone
