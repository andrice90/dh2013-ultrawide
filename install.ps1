<#
.SYNOPSIS
    Installs or removes the dh2013ws widescreen mod for Cabela's Dangerous Hunts 2013 (full game or demo).

.DESCRIPTION
    Finds every Steam copy of the game (full game, app 218860, and the free demo, app 225580) unless -GameDir
    is given, and copies dinput8.dll (plus dh2013ws.ini if there is none yet) next to the game's exe.
    A dinput8.dll from another mod is kept as dinput8.chain.dll and loaded through [Proxy] Chain.

.PARAMETER GameDir
    The game folder (the one with CloakNTEngine.dll). Default: every copy found through Steam.
.PARAMETER Uninstall
    Removes the mod (and restores a chained dinput8.dll).
.PARAMETER KeepConfig
    With -Uninstall: keep dh2013ws.ini.
.PARAMETER ResetConfig
    Replace an existing dh2013ws.ini with the default one.
.PARAMETER Locate
    Only print the game folder(s), one per line.

.EXAMPLE
    .\install.ps1
.EXAMPLE
    .\install.ps1 -GameDir "E:\Games\Cabela's Dangerous Hunts 2013"
.EXAMPLE
    .\install.ps1 -Uninstall
#>
param(
    [string]$GameDir,
    [switch]$Uninstall,
    [switch]$KeepConfig,
    [switch]$ResetConfig,
    [switch]$Locate
)
$ErrorActionPreference = 'Stop'

$AppIds = @('218860', '225580')       # full game, demo
$DemoEngineStamp = 1353949478         # CloakNTEngine.dll of the Steam demo: every patch verified on it
$ChainName = 'dinput8.chain.dll'

function Say([string]$text, [string]$color = 'Gray') { Write-Host $text -ForegroundColor $color }
function Fail([string]$text) { Say "ERROR: $text" Red; exit 1 }

function Test-GameDir([string]$dir) { return $dir -and (Test-Path (Join-Path $dir 'CloakNTEngine.dll')) }

function Get-SteamLibraries {
    $steam = $null
    foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam') {
        $props = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
        if ($props -and $props.SteamPath) { $steam = $props.SteamPath; break }
        if ($props -and $props.InstallPath) { $steam = $props.InstallPath; break }
    }
    if (-not $steam) { return @() }
    $libs = @($steam)
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) {
        foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
            $libs += $m.Groups[1].Value -replace '\\\\', '\'
        }
    }
    return $libs | Select-Object -Unique
}

function Find-GameDirs {
    $found = @()
    foreach ($lib in Get-SteamLibraries) {
        foreach ($id in $AppIds) {
            $acf = Join-Path $lib "steamapps\appmanifest_$id.acf"
            if (-not (Test-Path $acf)) { continue }
            $m = [regex]::Match((Get-Content $acf -Raw), '"installdir"\s+"([^"]+)"')
            if (-not $m.Success) { continue }
            $dir = Join-Path $lib ("steamapps\common\" + $m.Groups[1].Value)
            if (Test-GameDir $dir) { $found += (Resolve-Path $dir).Path }
        }
    }
    # Retail / non-Steam installs record their folder in the registry.
    foreach ($key in Get-ChildItem 'HKLM:\SOFTWARE\WOW6432Node\Activision' -ErrorAction SilentlyContinue) {
        if ($key.PSChildName -notmatch 'dangerous hunts 2013') { continue }
        $path = (Get-ItemProperty $key.PSPath -ErrorAction SilentlyContinue).installpath
        if (Test-GameDir $path) { $found += (Resolve-Path $path).Path }
    }
    return $found | Select-Object -Unique
}

function Get-PeTimestamp([string]$path) {
    $fs = [IO.File]::OpenRead($path)
    try {
        $br = New-Object IO.BinaryReader($fs)
        $fs.Position = 0x3c
        $fs.Position = $br.ReadInt32() + 8
        return $br.ReadUInt32()
    } finally { $fs.Dispose() }
}

function Test-OurDll([string]$path) {
    return [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($path)).Contains('dh2013ws.ini')
}

function Test-GameRunning([string]$dir) {
    foreach ($p in Get-Process -ErrorAction SilentlyContinue) {
        $exe = $null
        try { $exe = $p.Path } catch { }
        if ($exe -and $exe.StartsWith($dir.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}

function Set-Chain([string]$iniPath, [string]$value) {
    $ini = Get-Content $iniPath -Raw
    if ($ini -match '(?m)^Chain=') {
        $ini = $ini -replace '(?m)^Chain=[^\r\n]*', "Chain=$value"
    } else {
        $ini = $ini.TrimEnd() + "`r`n`r`n[Proxy]`r`nChain=$value`r`n"
    }
    Set-Content -Path $iniPath -Value $ini -Encoding Ascii -NoNewline
}

function Uninstall-From([string]$dir) {
    $dll = Join-Path $dir 'dinput8.dll'
    $chain = Join-Path $dir $ChainName
    if ((Test-Path $dll) -and (Test-OurDll $dll)) {
        Remove-Item $dll
        Say '  removed dinput8.dll'
    } elseif (Test-Path $dll) {
        Say '  dinput8.dll belongs to another mod; left alone' Yellow
    }
    if ((Test-Path $chain) -and -not (Test-Path $dll)) {
        Rename-Item $chain 'dinput8.dll'
        Say "  restored the other mod's dinput8.dll"
    }
    foreach ($f in 'dh2013ws.log', 'dinput8.dll.old') {
        $p = Join-Path $dir $f
        if (Test-Path $p) { Remove-Item $p }
    }
    $ini = Join-Path $dir 'dh2013ws.ini'
    if ((Test-Path $ini) -and -not $KeepConfig) { Remove-Item $ini; Say '  removed dh2013ws.ini' }
}

function Install-To([string]$dir, [string]$srcDll, [string]$srcIni) {
    $dll = Join-Path $dir 'dinput8.dll'
    $ini = Join-Path $dir 'dh2013ws.ini'
    $chain = Join-Path $dir $ChainName

    if ((Get-PeTimestamp (Join-Path $dir 'CloakNTEngine.dll')) -eq $DemoEngineStamp) {
        Say '  engine: Steam demo build (every patch verified)'
    } else {
        Say '  engine: not the demo build. The mod finds its patch points by pattern and skips any it does not' Yellow
        Say '  recognise; after the first start, dh2013ws.log lists what was patched.' Yellow
    }

    $chained = $false
    if ((Test-Path $dll) -and -not (Test-OurDll $dll)) {
        if (Test-Path $chain) { Fail "Both dinput8.dll and $ChainName from other mods exist in $dir; sort them out first." }
        Rename-Item $dll $ChainName
        $chained = $true
        Say "  another mod's dinput8.dll was kept as $ChainName and will be loaded by this mod" Yellow
    }
    $old = Join-Path $dir 'dinput8.dll.old'
    if (Test-Path $old) { Remove-Item $old -ErrorAction SilentlyContinue }

    Copy-Item $srcDll $dll -Force
    Unblock-File $dll -ErrorAction SilentlyContinue
    Say '  installed dinput8.dll'
    if ($ResetConfig -or -not (Test-Path $ini)) {
        Copy-Item $srcIni $ini -Force
        Say '  installed dh2013ws.ini (default settings)'
    } else {
        Say '  kept your dh2013ws.ini (-ResetConfig restores the defaults)'
    }
    if ($chained) { Set-Chain $ini $ChainName }
}

# ---- which game folders ---------------------------------------------------------------------------
if ($GameDir) {
    if (-not (Test-GameDir $GameDir)) { Fail "CloakNTEngine.dll not found in $GameDir; point -GameDir at the game's folder." }
    $dirs = @((Resolve-Path $GameDir).Path)
} else {
    $dirs = @(Find-GameDirs)
    if (-not $dirs) { Fail "Could not find the game through Steam. Run again with -GameDir `"<game folder>`"." }
}
if ($Locate) { $dirs | ForEach-Object { Write-Output $_ }; exit 0 }

foreach ($dir in $dirs) {
    if (Test-GameRunning $dir) { Fail "The game in $dir is running. Close it and run this again (Windows locks dinput8.dll while it runs)." }
}

if ($Uninstall) {
    foreach ($dir in $dirs) { Say $dir; Uninstall-From $dir }
    Say 'The widescreen mod is uninstalled.' Green
    exit 0
}

$here = $PSScriptRoot
$srcDll = @((Join-Path $here 'dinput8.dll'), (Join-Path $here 'build\dinput8.dll')) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $srcDll) { Fail 'dinput8.dll not found next to this script (or in build\). Download a release zip or run build.ps1 first.' }
$srcIni = Join-Path $here 'dh2013ws.ini'
if (-not (Test-Path $srcIni)) { Fail 'dh2013ws.ini not found next to this script.' }

foreach ($dir in $dirs) { Say $dir; Install-To $dir $srcDll $srcIni }
Say ''
Say 'Done. Start the game and pick your resolution in Options > Audio/Video.' Green
Say 'Field of view: numpad + / - / * in game, or FOV= in dh2013ws.ini. dh2013ws.log shows what the mod did.'
