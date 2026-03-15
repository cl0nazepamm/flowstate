# 3ds Max Plugin Template

Minimal C++ GUP plugin template for 3ds Max 2026.

## Requirements

- Visual Studio 2022 Build Tools (v143 toolset)
- Windows SDK 10.0.19041.0+
- CMake 3.20+
- 3ds Max 2026 SDK (`C:\Program Files\Autodesk\3ds Max 2026 SDK\maxsdk`)

## Quick Start

1. Copy this folder and rename it
2. Edit `CMakeLists.txt` — change `PLUGIN_NAME`
3. Edit `src/dllmain.cpp` — change `MY_PLUGIN_CLASS_ID`, `MY_PLUGIN_NAME`
4. Rename `src/my_plugin.def` to match your plugin name, update `LIBRARY` line
5. Double-click `build.bat`
6. Restart 3ds Max

## Files

```
CMakeLists.txt      # Build config — change PLUGIN_NAME and PLUGIN_TYPE here
src/
  dllmain.cpp       # Your plugin code — start editing here
  my_plugin.def     # DLL exports — rename to match PLUGIN_NAME
build.bat           # Build + deploy (auto-elevates for admin copy)
clean.bat           # Wipe build folder
```

## Plugin Types

| Extension | Type | Description |
|-----------|------|-------------|
| `.gup`    | GUP  | Global Utility Plugin — auto-runs on startup |
| `.dlo`    | Object | Geometry object (Box, Sphere, etc.) |
| `.dlm`    | Modifier | Modifier plugin (Bend, Twist, etc.) |
| `.dlt`    | Texture | Texture map plugin |
| `.dlr`    | Renderer | Render plugin |
| `.dlu`    | Utility | Utility panel plugin |
| `.dle`    | Export | File export plugin |
| `.bmi`    | Bitmap | Bitmap I/O plugin |

## SDK Libs Reference

| Lib | What it gives you |
|-----|-------------------|
| `core.lib` | Core Max interfaces (`GetCOREInterface()`) |
| `maxutil.lib` | Utilities, `Class_ID`, string helpers |
| `gup.lib` | GUP base class |
| `Paramblk2.lib` | `ClassDesc2`, `ParamBlockDesc2` |
| `Geom.lib` | `Point3`, `Matrix3`, geometry math |
| `Maxscrpt.lib` | MAXScript (`mprintf`, `ExecuteMAXScriptScript`) |
| `mesh.lib` | `Mesh`, triangle mesh operations |
| `bmm.lib` | Bitmap Manager (render/viewport capture) |
