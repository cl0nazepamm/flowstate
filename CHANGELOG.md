# Changelog

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
