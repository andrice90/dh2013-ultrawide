# dh2013ws — Cabela's Dangerous Hunts 2013 widescreen mod

Ultra-wide resolutions, correct aspect ratio, unstretched UI and adjustable FOV for the Steam demo of
*Cabela's Dangerous Hunts 2013* (app 225580, FUN Labs CloakNT engine, Direct3D 9).

**Status (v1.1):** v1 ran in game at 3440x1440. That test showed white edges at high FOV and a
stretched UI with an oversized crosshair; v1.1 fixes those and passes the offline tests (`.\test.ps1`).
v1.1 has not been checked in game yet.

## What it does

| Area | Stock game | With the mod |
|---|---|---|
| Resolution list | Only widths 800–2560 and refresh 60/75/85/100/120/150/200 Hz; anything else (3440x1440) falls back to the smallest mode | Every mode the monitor offers (800x600 up to 4095 wide), any refresh ≥ `MinRefresh`; optional forced size in the ini |
| 3D view | Aspect rounded to 16:9 or 4:3: stretched on 21:9 | Real aspect; Hor+ (same vertical view as 4:3, more on the sides) |
| FOV | 60° (4:3 basis), 75° at 16:9 | Adjustable in the ini or live with Num+ / Num- / Num*; scopes keep their magnification |
| Wide views | Ambient/sky shading covers at most 126.9°: white edges beyond | Covers up to the 170° clamp |
| Menus and HUD | A 16:9 layout stretched to the full width | Laid out for the real screen shape: not stretched, corner elements in the corners |
| Crosshair | Same size at any FOV | Shrinks with the FOV setting so it covers the same spread |
| Scope overlay | Sized from the screen width (too big on 21:9) | Same size per screen height as at 16:9 |

At 3440x1440 the default view becomes about 92° horizontal (was 75° stretched across the screen).

## Install / remove

1. Copy `build\dinput8.dll` and `dh2013ws.ini` into the game folder (next to `dh_2013_demo.exe`),
   or run `.\build.ps1 -Install` (with the game closed: Windows locks the DLL while it runs).
2. Start the demo from Steam. Pick 3440x1440 in Options → Audio/Video, or set `Width`/`Height` in the ini.
3. `dh2013ws.log` in the game folder shows what the mod did.

Remove: delete `dinput8.dll` (and `dh2013ws.ini`, `dh2013ws.log`). No game file is modified.

## Settings (`dh2013ws.ini`)

- `[Display] AllModes` — open the resolution list. `MinRefresh` hides low refresh rates.
  `Width`/`Height`/`Refresh` — force a mode at startup (0 = the game's setting; `Refresh=0` = highest).
- `[Camera] FixAspect` — real aspect + Hor+ (0 = the game's stretched behaviour).
  `FOV` — normal view in degrees on a 4:3 basis, 0 = game default (60). `ZoomBlend` — zoom at which scopes and
  binoculars are back to their original magnification. `KeyFOVUp/Down/Reset`, `FOVStep` — live hotkeys
  (virtual-key codes); changes are written back to `FOV=`.
- `[UI] FixUI` — UI canvas with the real screen shape (0 = stretched 16:9 layout). `CrosshairFOV` — scale the
  crosshair with the FOV setting. `CrosshairMinScale` — smallest crosshair as a fraction of the game's own
  (default 1.0: never smaller, so it only grows for FOV < 60; 0.86 keeps rifle/shotgun arms apart).
- `[Proxy] Chain` — load another `dinput8.dll` instead of the system one.

## How it works

`dinput8.dll` is a proxy: `CloakNTEngine.dll` imports it, so the mod initialises after the engine is mapped
but before it runs, patches the engine in memory, and forwards DirectInput to the system DLL. It only touches
the demo build (PE timestamp 1353949478) and checks the bytes at every site first.

All game logic is in `CloakNTEngine.dll` (17,237 named C++ exports; the exe is a SteamStub-wrapped launcher).
Game data (GUI XML, Lua bytecode, shaders) is in `streams\*.crf` archives (LZO1X chunks; `re\crf.py` extracts
them). RVAs below are from base 0x10000000.

- **Mode list**: `Renderer::Init` (`0x256780`) enumerates D3D modes into a fixed 256-slot array at
  `Renderer+0x1214c` (count `+0x12148`) and keeps a mode only if its width, height and refresh are in tables
  at `0x6b13f8` / `0x6b1428` / `0x6b146c`. The test at `0x256885` is replaced by `AcceptMode`: 800x600 ≤ size
  ≤ 4095 (the engine sorts by `((w << 12 | h) << 8 | hz)`), refresh `MinRefresh`..255, no duplicates, and
  stops at 255 modes. `CheckDisplayMode` then accepts 3440x1440.
- **Forced resolution** (only with `Width`/`Height` set): detour of `Renderer::Init` replaces the requested
  w/h/refresh (`666x666@66` there means "no setting yet, use the desktop").
- **Aspect**: `Renderer::SetViewport` (`0x255140`) stores h/w but rounds it to 0.5625 or 0.75
  (`+0x1409c` 3D aspect, `+0x140a0` full-screen copy, `+0x140ac` "widescreen" = h/w < 0.666, `+0x140b0`
  GUI/text aspect). A detour writes the real h/w into `+0x1409c`/`+0x140a0` (FixAspect) and `+0x140b0` (FixUI)
  when it is below 0.666.
- **FOV**: the camera FOV (`Camera+0xf8`, radians) is horizontal on a 4:3 basis; the default player FOV is a
  global at `0x8280ac` (60°). Three places widen it for "widescreen" with a fixed 16/9 ÷ 4/3: the projection in
  `Camera::UpdateCachedData` (`0x31056d`–`0x3105cc`), the player's "real fov" in
  `EffectManager::advanceTimeFov` (`0x1eaefe`–`0x1eaf48`) and the view cone in `ViewTrigger::IsInActiveRange`
  (`0x395eee`–`0x395f33`). Each block now jumps to a stub that calls `RealFov`: real aspect instead of 16:9,
  times the user's FOV factor `k = tan(FOV/2) / tan(60°/2)`, faded out as the view zooms in (`ZoomBlend`),
  clamped to 170°. With default settings at 16:9 the result equals the game's own numbers.
  The grass extents in `Renderer::RenderSolid/RenderTextured/RenderNormalWii/RenderDefer` divide by 4/3 and the
  aspect; their `fdiv [4/3]` operands point at `(4/3) / k` instead. The first-person weapon is drawn with its
  own FOV through `Renderer::SetFOV`, which goes through the same camera code.
- **White edges**: the engine shades deferred (normal/distance G-buffers). `RenderShadeAmbient` (`0x22fb30`) and
  `RenderShadeAmbientDirectional` (`0x2303c0`) draw a view-space quad of ±7000 at depth 3500, which covers
  2·atan(2) = 126.9° in each direction. Past that, pixels stay unshaded (white). Their `movss [±7000]` operands
  point at ±50000 instead (172°). The depth stays 3500, so position reconstruction is unchanged.
- **UI**: the GUI is a 720-high virtual canvas whose width comes from `Gui::SetPhysicalViewport` (`0x25c5c0`),
  which also rounds to 16:9 (1280 wide). The layout XML anchors elements (`c,c`, `0%r,-132b`, percent sizes),
  so the stub at `0x25c60d` gives it the real h/w (1720 wide at 3440x1440) and everything keeps its proportions.
  Text width uses `Renderer+0x140b0`, set to the same value.
- **Crosshair**: `PlayerHud::Crosshair_AdvanceTime` sizes the crosshair box as `PlayerHud+0x308` × 2 × 42
  (`fmul` at `0x2dc44e`); `+0x308` is 1.0 and no script changes it, so the box is always 84 units. The arms
  sit on its edges (`gui\hud\hud_crosshair.xml`: rifle/shotgun 36 long, pistol 42, so the pistol arms meet in
  the middle). The operand points at `42 × max(1 / widening of the current view, CrosshairMinScale)`.
- **Scope overlay**: `wsModeHudBase::RenderHUD` sizes it as `width × 1024 / 1280` on wide screens (`fdiv` at
  `0x3bf4ff`); the divisor becomes `720 × width / height`, i.e. the same size per screen height as at 16:9.
- **Bullet-drop markers**: `wsModeAimWeapon::ChangeDropMarkers` divides by `3D aspect × scope FOV`; the stub at
  `0x3ba871` keeps the game's 0.5625 on wide screens, since the vertical view is the same as at 16:9.
- Frustum culling and clip planes come from the projection matrix, so they follow automatically.

## Known gaps (to check in game)

- Menu screens with fixed-size background art may leave empty space at the sides on 21:9 (`FixUI=0` brings
  back the stretched layout).
- The "focus" radial blur in `Renderer::RenderFLT` uses 16:9 constants (`3.555`/`2.666`); not changed.
- Bink videos and loading screens are unverified at 21:9.
- Cutscene cameras get the FOV setting too.
- With a non-widescreen mode (4:3, 5:4) the FOV setting only applies to the grass extents, not the view.

## Build and test

- `.\build.ps1` — MSVC (VS 2022 Build Tools, x86, static CRT) → `build\dinput8.dll`. `-Install` copies it.
- `.\test.ps1` — builds a test variant plus `tests\harness.exe` and runs it: the harness maps the real
  `CloakNTEngine.dll` from the game folder without running it (`DONT_RESOLVE_DLL_REFERENCES`), loads the mod,
  then executes each patched engine block (mode filter, viewport, GUI viewport, the three FOV blocks,
  drop markers, `Renderer::Init`) on fake objects and stack frames and checks results, stack balance and
  preserved registers. No game window.
- `re\*.py` — the Python (pefile + capstone) helpers used to find all of this: `exports.py` (full export list;
  pefile truncates it), `dis.py`, `xref.py` (displacement/immediate references), `callers.py`, `bytes.py`,
  `crf.py` (list/extract `.crf` archives), `shd.py` (find and disassemble shaders in an extracted `.shd` pack
  via `d3dcompiler_47.dll`).
