# Cabela's Dangerous Hunts 2013 Widescreen Fix + Adjustable FOV

Ultrawide and widescreen support for **Cabela's Dangerous Hunts 2013** on PC: any resolution your monitor
supports, the correct aspect ratio, an adjustable field of view and an unstretched HUD.

| | Stock game | With the fix |
|---|---|---|
| Resolutions | Only widths up to 2560 and refresh rates 60–200 Hz; 3440x1440 is not offered | Every mode your monitor supports (up to 4095 wide, e.g. 3440x1440, 5120x1440 is too wide), any refresh rate |
| 3D view | Rounded to 16:9 or 4:3, so 21:9 is stretched | Real aspect ratio, Hor+ (more on the sides, same vertical view) |
| Field of view | Fixed (60° on a 4:3 basis) | Adjustable, live with numpad + / − / \*; scopes keep their magnification |
| Wide views | White unshaded edges beyond about 127° | Shaded up to the 170° limit |
| Menus and HUD | A 16:9 layout stretched across the screen | Laid out for your screen: nothing stretched, corner items in the corners |
| Crosshair, scope overlay | Sized for 16:9 | Correct size on any screen and FOV |

At 3440x1440 the default view becomes 92° wide instead of a stretched 75°.

**Status:** used on the free Steam demo at 3440x1440. The full game runs the same engine, and the fix finds
its patch points by pattern rather than fixed addresses, so it is expected to work there too, but it has not
been tried on the full game yet. If you play the full game, please [open an issue](../../issues) with your
`dh2013ws.log`, whether it works or not.

## Install

1. Download `dh2013ws-<version>.zip` from [Releases](../../releases) and extract it anywhere.
2. Double-click **`install.bat`**. It finds the game through Steam (the full game, app 218860, and the demo,
   app 225580; it installs into every copy it finds). For a copy outside Steam, run it from a command prompt:
   `install.bat -GameDir "D:\Games\Cabela's Dangerous Hunts 2013"`.
3. Start the game and pick your resolution in **Options → Audio/Video**.

The game must be closed while installing. Manual install: copy `dinput8.dll` and `dh2013ws.ini` into the game
folder (the one with `CloakNTEngine.dll`).

**Uninstall:** double-click `uninstall.bat`, or delete `dinput8.dll`, `dh2013ws.ini` and `dh2013ws.log` from
the game folder. The fix never changes the game's own files.

If the game folder already has a `dinput8.dll` from another mod, the installer keeps it as
`dinput8.chain.dll` and the fix loads it, so both work. Uninstalling puts it back.

## Settings

`dh2013ws.ini` in the game folder (read when the game starts):

| Setting | Default | |
|---|---|---|
| `[Display] AllModes` | 1 | List every display mode the monitor offers |
| `MinRefresh` | 50 | Hide modes below this refresh rate |
| `Width`, `Height`, `Refresh` | 0 | Force a resolution at startup (0 = use the game's setting; `Refresh=0` = highest available) |
| `[Camera] FixAspect` | 1 | Real aspect ratio and Hor+ view (0 = the game's stretched view) |
| `FOV` | 0 | Normal view in degrees, measured on a 4:3 screen (0 = game default, 60). Wider screens add to it |
| `ZoomBlend` | 2.0 | Zoom level from which scopes and binoculars use their original magnification |
| `KeyFOVUp`, `KeyFOVDown`, `KeyFOVReset`, `FOVStep` | numpad + − \*, 5 | FOV hotkeys (Windows virtual-key codes); changes are saved to `FOV=` |
| `[UI] FixUI` | 1 | Menus and HUD in the real screen shape (0 = stretched 16:9 layout) |
| `CrosshairFOV` | 1 | Crosshair shrinks as the FOV setting widens the view |
| `CrosshairMinScale` | 1.0 | Smallest crosshair, as a fraction of the game's own (1.0 = never smaller; 0.86 keeps rifle and shotgun arms apart) |
| `[Debug] Log` | 1 | Write `dh2013ws.log` |
| `[Proxy] Chain` | | Another `dinput8.dll` to load (set by the installer when needed) |

FOV examples at 21:9: `FOV=60` → 92° horizontal, `70` → 103°, `80` → 113°.

## Troubleshooting

- **Nothing changes and there is no `dh2013ws.log`:** Windows did not load the DLL. Check that `dinput8.dll` is
  in the folder with `CloakNTEngine.dll`. Antivirus or **Smart App Control** (Windows 11) can block unsigned
  DLLs; the fix is not code-signed. Smart App Control cannot whitelist single files, so it would have to be
  turned off, which is your call.
- **The log says `NOT patched` for something:** your copy of the engine differs where that patch goes. The
  other patches still work. Please open an issue with the log.
- **A menu has empty space at the sides:** some menu art is drawn at a fixed 16:9 size. `FixUI=0` brings back
  the stretched menus (and HUD).
- **Resolution list is huge:** raise `MinRefresh`, e.g. to 100.

## Known limitations

- Pre-rendered videos and loading screens are shown as the game draws them.
- Cutscene cameras get the FOV setting too.
- The FOV setting needs a widescreen resolution; on 4:3 or 5:4 the view keeps the game's FOV.
- The "focus" zoom-blur effect still uses 16:9 maths.

## Building from source

Needs Windows and Visual Studio 2019/2022 (or the Build Tools) with *Desktop development with C++*.

```powershell
.\build.ps1             # build\dinput8.dll
.\test.ps1              # offline tests against your installed game (see below)
.\package.ps1           # dist\dh2013ws-<version>.zip
.\build.ps1 -Install    # build and run install.ps1
```

`test.ps1` maps the game's `CloakNTEngine.dll` without starting it, loads a test build of the fix, and runs
every patched piece of engine code on fake objects: about 55 checks per installed copy, once at the engine's
own load address and once relocated. On the demo it also checks that every patch point found by pattern is
exactly the one verified by hand. Pushing a tag like `v1.2.0` makes GitHub Actions build the zip and publish
a release (`.github/workflows/build.yml`).

How it works, engine internals and the reverse-engineering tools: [docs/engine-notes.md](docs/engine-notes.md).

## Disclaimer

Unofficial fan-made fix, not affiliated with or endorsed by Activision, Cabela's or the developers. No game
files are included or modified; it patches the running game in memory. MIT licensed (see `LICENSE`).
