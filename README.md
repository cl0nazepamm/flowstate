# flowstate

3ds Max C++ GUP plugin suite for fast parameter editing, shader lookup,
modifier-stack navigation, and viewport/modeling hotkeys.

![FlowState logo](images/flowstate_logo.png)

## Features

### Modifier Stack under mouse.

Floating parameter panel for the selected object or modifier stack. It opens at
the cursor, exposes editable parameters, supports favorite pins, and can take over
Editable Poly caddy-style command parameters such as chamfer, bridge, extrude,
bevel, inset, and outline.

Main trigger: `Mouse4`.

Highlights:

- Object, modifier, and selected modifier parameters in one floating panel.
- Multi-object writes where supported.
- Favorite pin strip with reorder and V/H slider slot assignment.
- Wheel, typed-value, and click-drag editing.
- Collapsible and hideable parameter groups.
- Editable Poly command-mode support through cached EPoly operation param blocks.

### Search
  
Material and texmap search palette for the scene and available shader classes.
It includes OSL category patching, pinned shader bricks, and texture preview.

Main triggers:

- `Shift+Mouse4`
- `Tab` when Material Editor is in focus.

### Config UI

Dark .NET WinForms macroscript dialog in
`macros/flowstate_config.ms`.

It controls:

- PowerParams and PowerShader module toggles.
- Sub-object toggles.
- SME `Tab` shader search.
- Theme mode.
- XButton1 action keymap.

The macroscript self-installs a startup loader so the macros survive Max
restarts.

### Modeling Tools

`FlowState.gup` also exports the bundled native classes/tools:

- `Precision Cut` modifier from `src/precisioncut/`.
- `Normalize Poly` plus function-published helper tools from
  `src/normalize_edges/`.
- `Smooth Bridge` and `F2 Extend` macros in `macros/flowstate_config.ms`.

## Controls

| Input | Action |
| --- | --- |
| `XButton2` | Toggle PowerParams |
| `Shift+XButton2` | Toggle PowerShader |
| `Tab` in Slate Material Editor | Toggle PowerShader when enabled |
| `Ctrl+XButton2` | Cycle auto-orbit mode |
| `Alt+Shift+XButton2` | Swap V/H slider assignments |
| `XButton1` combos | Run configurable drag/action keymap |
| Mouse wheel over PowerParams value | Scrub value |
| `Shift` while dragging/scrubbing | Coarse speed |
| `Alt` while dragging/scrubbing | Fine speed |
| Type then `Tab`/`Enter` | Apply typed value |
| `Esc` | Cancel panel operation and close |
| Click outside panel | Accept and close |

## Mouse5 Keymap

Mouse5 actions are mapped by combo index, not hardcoded modifier checks.

| Combo index | Modifiers | Default action |
| --- | --- | --- |
| `0` | none | Screen Grab |
| `1` | `Shift` | Time Slider |
| `2` | `Ctrl` | Param Slider |
| `3` | `Ctrl+Shift` | Opacity Slider |
| `4` | `Ctrl+Alt+Shift` | Clear Sliders |

The globals are:

- `g_kmGrab`
- `g_kmTime`
- `g_kmSlider`
- `g_kmOpacity`
- `g_kmClear`

They persist as `Key:<action>=<comboIdx>` entries in `FlowState.cfg`. Drag-time
speed modifiers are separate and remain hardcoded: `Shift` is coarse/faster,
`Alt` is fine/slower.
