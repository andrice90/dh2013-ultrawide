# Engine notes

How the fix works, what it changes in Cauldron's CloakNT engine, and the tools used to find it.

## Loading

The game exe is a small launcher; everything runs in `CloakNTEngine.dll` (Direct3D 9, 32-bit). The engine
imports `dinput8.dll`, so a `dinput8.dll` in the game folder is loaded before the engine runs any code. The fix
is such a proxy: from its `DllMain` it patches the mapped (not yet initialised) engine in memory, then forwards
the DirectInput exports to the system DLL (or to `[Proxy] Chain`).

## Finding patch points without fixed addresses

The engine exports about 17,000 C++ names (all of `Renderer`, `Camera`, `Gui`, `PlayerHud`, ...). Every patch
point is located at startup (`src/patches.cpp`):

1. **Function** by its exported (mangled) name, e.g. `?SetViewport@Renderer@@QAEXIIII@Z`.
2. **Site** by a byte pattern with wildcards inside that function, bounded by the next exported address, and
   required to match exactly once.
3. **Constants** (`4/3`, `0.5625`, `1280.0`, `42.0`, `±7000`) by the value their operand points to, not by
   address.
4. **Struct offsets** from one-instruction getters: `Renderer::GetGameAspectRatio`, `GetGameWideScreen`,
   `GetAspectRatio`, `Camera::GetFov`, and `BeginRenderingFullScreen` for the full-screen aspect. Patterns
   that contain offsets must agree with them.

A build where a pattern does not match loses only that patch; `dh2013ws.log` lists each one. On the Steam
demo (`CloakNTEngine.dll` timestamp 1353949478) the test harness checks that every resolved site equals the
address verified by hand (table below).

## What the engine does and what the fix changes

Demo addresses are relative to the image base 0x10000000.

| Area | Engine | Fix |
|---|---|---|
| Display modes | `Renderer::Init` (`0x256780`) enumerates D3D modes into a fixed 256-slot array (`Renderer+0x1214c`, count `+0x12148`) and keeps a mode only if width, height and refresh are in tables at `0x6b13f8` / `0x6b1428` / `0x6b146c` (widths up to 2560). Otherwise `CheckDisplayMode` falls back to the smallest mode. | The three-table test (`0x256885`) jumps to `AcceptMode`: 800x600 up to 4095 wide (the sort key is `((w << 12 \| h) << 8 \| hz)`), refresh `MinRefresh`..255, no duplicates, at most 255 modes. |
| Forced resolution | `Renderer::Init(adapter, ?, w, h, hz, ?, ?)`; `666x666@66` means "no setting yet, use the desktop". | Detour replaces w/h/hz when `Width`/`Height` are set. |
| Aspect ratio | `Renderer::SetViewport` (`0x255140`) computes h/w, then stores 0.5625 if h/w < 0.666 else 0.75: `+0x1409c` (3D), `+0x140a0` (full screen), `+0x140b0` (UI text and UI 3D); `+0x140ac` = h/w < 0.666 ("widescreen"). | Detour writes the real h/w to the 3D fields (FixAspect) and the UI field (FixUI). |
| FOV | Camera FOV `Camera+0xf8`, radians, horizontal on a 4:3 basis; default player FOV is a global (`0x8280ac`, 60°). Three blocks widen it for widescreen by a fixed `16/9 ÷ 4/3`: the projection in `Camera::UpdateCachedData` (`0x31056d`–`0x3105cc`, then `mtProjection(fov, aspect)`), the player's "real fov" in `EffectManager::advanceTimeFov` (`0x1eaefe`–`0x1eaf48`), the view cone in `ViewTrigger::IsInActiveRange` (`0x395eee`–`0x395f33`). | Each block jumps to a stub computing `2·atan(tan(fov/2) · (w/h)/(4/3) · k)`, where `k = tan(FOV/2)/tan(60°/2)` fades to 1 as the view zooms in (`ZoomBlend`), clamped to 170°. With default settings at 16:9 this equals the game's own result. The first-person weapon is drawn with its own FOV through `Renderer::SetFOV`, which goes through the same camera code. |
| Grass extents | `RenderSolid/RenderTextured/RenderNormalWii/RenderDefer` compute how far grass is drawn as `tan(fov/2) / (4/3) / aspect`. | The `fdiv [4/3]` operands point at `(4/3)/k`. |
| White edges | Deferred renderer (normal and distance G-buffers). `RenderShadeAmbient` (`0x22fb30`) and `RenderShadeAmbientDirectional` (`0x2303c0`) shade through a view-space quad of ±7000 at depth 3500, which covers 2·atan(2) = 126.9°; beyond it pixels stay unshaded (white). | The `movss [±7000]` operands point at ±50000 (172°). Depth stays 3500, so position reconstruction is unchanged. |
| UI canvas | The GUI is 720 virtual units high; `Gui::SetPhysicalViewport` (`0x25c5c0`) picks the width from the same rounding (1280 for 16:9). The layout XML anchors elements (`c,c`, `0%r,-132b`, percentages). | The stub at `0x25c60d` gives it the real h/w (1720 wide at 3440x1440). |
| Crosshair | `PlayerHud::Crosshair_AdvanceTime` sizes the crosshair box as `PlayerHud+0x308` (always 1.0) × 2 × 42 (`fmul` at `0x2dc44e`); arms sit on the box edges (`gui\hud\hud_crosshair.xml`: rifle and shotgun arms 36, pistol 42, so pistol arms meet in the middle). | Operand points at `42 · max(1/k_view, CrosshairMinScale)`. |
| Scope overlay | `wsModeHudBase::RenderHUD` sizes it as `width × 1024 / 1280` on wide screens (`fdiv` at `0x3bf4ff`). | Divisor becomes `720 · w / h`: same size per screen height as at 16:9. |
| Bullet-drop markers | `wsModeAimWeapon::ChangeDropMarkers` divides by `3D aspect × scope FOV` (`0x3ba871`). | Keeps 0.5625 on wide screens (the vertical view is the same as at 16:9). |
| Not changed | `Renderer::RenderFLT` "focus" radial blur uses 16:9 constants (`3.555`, `2.666`); `Renderer::GetSplitDualWidth` (split screen). | |

Frustum culling and clip planes come from the projection matrix, so they follow the new projection.

## Game data

`streams\*.crf` are archives: `!CPF` header, 0x124-byte entries (flags, archive id, date, ?, size, data offset,
chunk count, name hash, name), data in chunks of `u32 header` (bit 31 = LZO1X compressed) + bytes. GUI layouts
are XML (`gui.crf`), scripts are compiled Lua (`scripts.crf`), shaders are D3D9 bytecode packs (`shaders.crf`).

## Reverse-engineering tools (`re/`)

Python 3 with `pefile`, `capstone` and `numpy`. The scripts find the game through Steam or `DH2013_GAME_DIR`.

| Script | |
|---|---|
| `exports.py` | Writes all export names to `re/exports.txt` (pefile alone stops at 8192). Run first. |
| `dis.py VA[:count]` | Disassembly with export names and float constants. |
| `xref.py HEX` | Instructions using a value as immediate or displacement (struct offsets, constant addresses). |
| `callers.py VA` | Direct calls and jumps to an address. |
| `bytes.py VA:LEN`, `rd.py TYPE:VA` | Raw bytes as C initialisers; typed values. |
| `crf.py ARCHIVE [filter] [outdir]` | List or extract `.crf` archives. |
| `shd.py FILE.shd NAME [max]` | Disassemble shaders in an extracted pack that use a constant/sampler name (needs `d3dcompiler_47.dll`). |
| `strs.py FILE REGEX`, `strrange.py FILE START END` | Strings in any file. |

Extracted game data is for your own analysis; keep it out of the repository (`re/extracted/` is ignored).
