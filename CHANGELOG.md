# Changelog

## 1.5.0 - 2026-08-04

### Added

- Added a Force reload list command to the header menu. It re-parses the class directory, OSL folders, and scene materials in place — no 3ds Max restart — so newly loaded plugins, edited shader metadata, and reorganized OSL folders show up immediately. (Max itself still registers brand-new .osl files only at startup.)

### Changed

- Categories now mirror the Slate material browser. Classes that publish `IMaterialBrowserEntryInfo` (the browser's own grouping source) show exactly that group; legacy stock maps that predate the interface have their 1990s category tokens translated to the browser's current groups (General, Scanline) instead of showing raw COMP/COLMOD/2D/ENV strings.
- OSL shader categories are matched by the shader name declared inside each .osl file, not the filename — about 15% of the stock library declares a different name (ColorCorrect.osl registers as UberColorCorrect). Subfolders of the OSL directories become categories, unmatched OSL shaders fall back to "OSL", and the generic OSL Map host is listed under OSL instead of "Autodesk".

### Fixed

- Native maps can no longer be miscategorized as OSL. Only classes actually registered by the OSL plugin (OSL_ internal names) receive folder-scan categories, so the standard Composite map no longer collides with Composite.osl.
- iToo entries no longer display as "Itoo Software". iToo's ClassDesc returns the company name from both name virtuals after its DLL loads; names are now vetted against category strings and fall back to the class directory's registration-time names, restoring Forest Color, RailClone Color, Forest Material, and Forest Edge.
- The hidden Slate slot placeholders (NoMaterial / NoTexture, both displayed as "None") are excluded from the list.
- Stock Autodesk materials no longer show empty or junk categories. Their live ClassDesc reports an empty or meaningless token (Blend/Composite: empty, Architectural: "Material", Physical: "Physical"), so category resolution now falls back to the class directory and files them the way the browser does: General for the compound and modern materials, Scanline for Standard (Legacy), Raytrace, Architectural, and Advanced Lighting Override.
- V-Ray materials and maps are grouped under a V-Ray category. Chaos publishes no browser grouping and tags everything "standard".

### Compatibility

- Verified Release builds against the 3ds Max 2024, 2025, 2026, and 2027 SDKs.

## 1.4.1 - 2026-08-01

### Added

- Added a Persistent search toggle to the header menu. The palette reopens with the last query intact and restores it after a 3ds Max restart, with the text pre-selected so typing still replaces it in one keystroke.
- Added a Keep panel open toggle to the header menu. The palette no longer closes when it loses focus or after assigning a material or map, so you can drop several shaders in a row.

### Changed

- The palette hands 3ds Max its keyboard shortcuts back whenever Max takes focus, and reclaims them on reactivation, so Keep panel open never swallows viewport hotkeys.
- Both toggles persist through `PSPersistSearch`, `PSKeepOpen`, and `PSSearch` in `FlowState.cfg`. The query is flushed on close rather than on every keystroke.

### Fixed

- Tab now closes the shader palette while the palette itself has focus, instead of only toggling from the Slate Material Editor. Keep panel open made this obvious, since the palette never gives focus back on its own. Renaming a brick still leaves Tab inert so a half-typed name is not lost.

### Compatibility

- Verified Release builds against the 3ds Max 2024, 2025, 2026, and 2027 SDKs.

## 1.4.0 - 2026-07-31

### Added

- Added a Favorites filter that composes with the Materials, Maps, and Scene filters.
- Added automatic palette compaction for small lists
- Added a compact header menu
- Added persistent brick reordering with middle-button drag.
- Added debounce to bitmap preview (eliminates any possible lag when previewing high resolution bitmaps)

### Changed

- Right-click favorites are sorted alphabetically and grouped at the start of the normal results list.
- Brick favorites remain visible in the brick strip and keep their diamond indicator, but no longer duplicate themselves in the Favorites filter or at the start of the results list.
- Pinning or unpinning an item now preserves the current selection and scroll position.
- Brick controls now use left-click to activate, left-drag to create/drop, middle-click to remove, middle-drag to reorder, and right-click to rename.

### Fixed

- Eliminated duplicate favorites such as repeated Composite maps by using stable class and scene identities, deduplicating all favorite views, and migrating legacy saved entries.
- Fixed compact lists briefly scrolling or snapping when keyboard navigation reached the last item.
- Fixed the scrollbar reappearing on the final row of a list that did not actually overflow.
- Fixed the results list jumping back to the start after pinning an item.
- Fixed a 3ds Max crash caused by releasing a brick drag over FlowState's own controls. FlowState windows and the preview popup are never probed as Max drag-and-drop targets.
- Made brick capture cancellation and button recreation safe during reorder, removal, palette deactivation, and interrupted mouse gestures.

### Compatibility

- Verified Release builds against the 3ds Max 2024, 2025, 2026, and 2027 SDKs.
