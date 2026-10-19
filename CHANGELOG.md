# Changelog

## [1.2.0] - 2026-10-18

### Added
- Full game support (Steam app 218860): patch points are now found by exported function name, byte pattern
  and constant values, with struct offsets read from the engine, instead of fixed demo addresses. Any build
  where the code matches gets the patch; `dh2013ws.log` lists each one. Verified on the demo; not yet tried
  on the full game.
- `install.bat` / `uninstall.bat` (`install.ps1`): find every Steam copy (full game and demo), keep another
  mod's `dinput8.dll` as `dinput8.chain.dll`, keep existing settings, uninstall cleanly.
- `CrosshairMinScale` (default 1.0): the crosshair never gets smaller than the game's own, so its arms no
  longer overlap at high FOV.
- Release zip (`package.ps1`) and GitHub Actions build.

### Changed
- The mod no longer refuses engine builds other than the demo.

## [1.1.0] - 2026-10-18

### Fixed
- White, unshaded screen edges at wide FOV (the deferred ambient pass only covered 126.9°).
- Menus and HUD stretched on screens wider than 16:9 (`FixUI`).
- Crosshair size ignored the FOV setting (`CrosshairFOV`).
- Scope overlay too large on wide screens.
- Bullet-drop markers kept the game's 16:9 maths with the new aspect ratio.

## [1.0.0] - 2026-10-18

### Added
- Every display mode the monitor supports (the game listed widths up to 2560 only), optional forced
  resolution.
- Real aspect ratio for the 3D view and Hor+ field of view.
- FOV setting with live hotkeys; scopes and binoculars keep their magnification.
