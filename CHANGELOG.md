# Changelog

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
