# FlowState TODO - from flowstate.md

## Tiling Properties (for LINK)
- Bitmaptexture: `.coords.U_Tiling` / `.coords.V_Tiling`
- ai_image: `.sscale` / `.tscale`
- RS_Texture: `.scale_x` / `.scale_y`
- UberBitmap (OSL_uberBitmap2b): `.scale` (single uniform)
- OSLMap: `.scale` (if available)

## Fixes Priority
1. LINK popup removal + UberBitmap/Arnold/RS support
2. Modifier search resize when panel too small
3. Pins rebuild on modifier add
4. Spline stack pinning
5. Panel drag (header text only, middle-mouse drag)
6. Name display robustness

## Features Priority
1. Shift+XButton1 = Time Slider, Ctrl+XButton1 = Opacity
2. XButton1 dynamic assignment system
3. Remove hardcoded modeling buttons, ModStack command system
4. PowerShader tab rework (remove ALL, toggle Shaders/Maps)
5. Dual favorites (file-local + persistent)
