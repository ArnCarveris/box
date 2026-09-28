<#
.SYNOPSIS
    Build (if needed) and run box_app on Windows with MSVC, picking a scene from a menu.

.DESCRIPTION
    Without arguments the script scans the repository for .flecs scenes and shows a
    menu: Up/Down/PgUp/PgDn/Home/End move, typing filters, Backspace edits the filter,
    Enter launches box_app with the selected scene, Esc quits. When box_app exits the
    menu comes back with the same scene selected.

    Scenes are collected from apps/box_app/etc/scenes and test/integration/{visual,
    command,bench}. Like tools/integration.py, a scene's <scene>.sources entries and its
    <scene>_content.flecs are loaded before the scene itself.

.PARAMETER Cfg
    Build configuration: debug (default) or release.

.PARAMETER Scene
    Run this scene directly, skipping the menu.

.PARAMETER NoMenu
    Run box_app with the default scene, skipping the menu.

.PARAMETER BoxArgs
    Extra arguments passed to box_app, for example --headless --frames 10.

.EXAMPLE
    .\run_box.ps1
.EXAMPLE
    .\run_box.ps1 -Cfg release
.EXAMPLE
    .\run_box.ps1 -Scene apps/box_app/etc/scenes/town.flecs
.EXAMPLE
    .\run_box.ps1 -NoMenu --headless --frames 10
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Cfg = 'debug',
    [string]$Scene,
    [switch]$NoMenu,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$BoxArgs = @()
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$AppDir = Join-Path $Root 'apps\box_app'
$Bake = Join-Path $env:USERPROFILE 'bake3\bake3.exe'

# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------

function Import-MsvcEnvironment {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }

    $vsDir = $null
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vsDir = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (-not $vsDir) { $vsDir = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\18\Community' }

    $vcvars = Join-Path $vsDir 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "run_box: vcvars64.bat not found under $vsDir" }

    # Run vcvars in cmd and copy the resulting environment into this session.
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
    }

    $cmakeDir = Join-Path $vsDir 'Common7\IDE\CommonExtensions\Microsoft\CMake'
    if (Test-Path (Join-Path $cmakeDir 'CMake\bin\cmake.exe')) {
        $env:PATH = "$cmakeDir\CMake\bin;$cmakeDir\Ninja;$env:PATH"
    }

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw 'run_box: could not set up the MSVC environment'
    }
}

function Set-LibclangPath {
    # wgpu-native's bindgen needs libclang when its bundle is (re)built.
    if ($env:LIBCLANG_PATH) { return }
    $candidates = @(
        (Join-Path $env:ProgramFiles 'LLVM\bin'),
        (Join-Path $env:ProgramFiles 'Unity\Hub\Editor\6000.4.0f1\Editor\Data\PlaybackEngines\WebGLSupport\BuildTools\Emscripten\llvm')
    )
    foreach ($dir in $candidates) {
        if (Test-Path (Join-Path $dir 'libclang.dll')) { $env:LIBCLANG_PATH = $dir; return }
    }
}

# ---------------------------------------------------------------------------
# Scenes
# ---------------------------------------------------------------------------

function ConvertTo-ForwardSlash([string]$Path) { $Path -replace '\\', '/' }

function Get-Scenes {
    $groups = @(
        @{ Name = 'app';     Dir = 'apps\box_app\etc\scenes' },
        @{ Name = 'visual';  Dir = 'test\integration\visual' },
        @{ Name = 'command'; Dir = 'test\integration\command' },
        @{ Name = 'bench';   Dir = 'test\integration\bench' }
    )
    foreach ($g in $groups) {
        $dir = Join-Path $Root $g.Dir
        if (-not (Test-Path $dir)) { continue }
        Get-ChildItem $dir -Filter *.flecs -File |
            Where-Object { $_.BaseName -notlike '*_content' } |
            Sort-Object Name |
            ForEach-Object {
                [pscustomobject]@{
                    Group = $g.Name
                    Name  = $_.BaseName
                    Path  = $_.FullName
                    Rel   = ConvertTo-ForwardSlash $_.FullName.Substring($Root.Length + 1)
                }
            }
    }
}

# Scripts box_app loads for a scene: its .sources entries, its _content file, then itself.
function Get-SceneScripts([string]$Path) {
    $full = (Resolve-Path $Path).Path
    $base = [IO.Path]::Combine([IO.Path]::GetDirectoryName($full), [IO.Path]::GetFileNameWithoutExtension($full))
    $scripts = @()

    $sources = "$base.sources"
    if (Test-Path $sources) {
        $dir = Split-Path $sources
        foreach ($line in Get-Content $sources) {
            $line = $line.Trim()
            if ($line) { $scripts += [IO.Path]::GetFullPath((Join-Path $dir $line)) }
        }
    }

    $content = "${base}_content.flecs"
    if (Test-Path $content) { $scripts += $content }

    $scripts += $full
    $scripts | ForEach-Object { ConvertTo-ForwardSlash $_ }
}

# ---------------------------------------------------------------------------
# Menu
# ---------------------------------------------------------------------------

function Write-Line([string]$Text, [ConsoleColor]$Fg = [Console]::ForegroundColor,
        [ConsoleColor]$Bg = [Console]::BackgroundColor) {
    $width = [Console]::WindowWidth - 1
    if ($Text.Length -gt $width) { $Text = $Text.Substring(0, $width) }
    $oldFg = [Console]::ForegroundColor
    $oldBg = [Console]::BackgroundColor
    [Console]::ForegroundColor = $Fg
    [Console]::BackgroundColor = $Bg
    [Console]::Write($Text)
    [Console]::ForegroundColor = $oldFg
    [Console]::BackgroundColor = $oldBg
    [Console]::Write((' ' * ($width - $Text.Length)) + "`n")
}

function Show-SceneMenu($Scenes, [string]$Selected) {
    $filter = ''
    $index = 0
    $offset = 0
    if ($Selected) {
        for ($i = 0; $i -lt $Scenes.Count; $i++) {
            if ($Scenes[$i].Path -eq $Selected) { $index = $i; break }
        }
    }

    $cursorVisible = [Console]::CursorVisible
    [Console]::CursorVisible = $false
    Clear-Host
    try {
        while ($true) {
            $shown = @($Scenes | Where-Object {
                -not $filter -or "$($_.Group) $($_.Name)" -like "*$filter*"
            })
            if ($index -ge $shown.Count) { $index = [Math]::Max(0, $shown.Count - 1) }

            $rows = [Math]::Max(3, [Console]::WindowHeight - 6)
            if ($index -lt $offset) { $offset = $index }
            if ($index -ge $offset + $rows) { $offset = $index - $rows + 1 }
            $offset = [Math]::Max(0, [Math]::Min($offset, [Math]::Max(0, $shown.Count - $rows)))

            [Console]::SetCursorPosition(0, 0)
            Write-Line "box scenes ($Cfg)  -  Up/Down move, type to filter, Enter launch, Esc quit" Cyan
            Write-Line ("filter: " + $filter + "_") Yellow
            Write-Line ''
            for ($r = 0; $r -lt $rows; $r++) {
                $i = $offset + $r
                if ($i -ge $shown.Count) { Write-Line ''; continue }
                $s = $shown[$i]
                $label = '{0,-8} {1}' -f $s.Group, $s.Name
                if ($i -eq $index) {
                    Write-Line ("> " + $label) Black Cyan
                } else {
                    Write-Line ("  " + $label)
                }
            }
            $status = if ($shown.Count) { "$($index + 1)/$($shown.Count)  $($shown[$index].Rel)" } else { 'no match' }
            Write-Line ''
            Write-Line $status DarkGray

            $key = [Console]::ReadKey($true)
            switch ($key.Key) {
                'UpArrow'   { if ($index -gt 0) { $index-- } }
                'DownArrow' { if ($index -lt $shown.Count - 1) { $index++ } }
                'PageUp'    { $index = [Math]::Max(0, $index - $rows) }
                'PageDown'  { $index = [Math]::Max(0, [Math]::Min($shown.Count - 1, $index + $rows)) }
                'Home'      { $index = 0 }
                'End'       { $index = [Math]::Max(0, $shown.Count - 1) }
                'Escape'    { return $null }
                'Enter'     { if ($shown.Count) { return $shown[$index] } }
                'Backspace' {
                    if ($filter.Length) { $filter = $filter.Substring(0, $filter.Length - 1); $index = 0 }
                }
                default {
                    if (-not [char]::IsControl($key.KeyChar)) { $filter += $key.KeyChar; $index = 0 }
                }
            }
        }
    } finally {
        [Console]::CursorVisible = $cursorVisible
        Clear-Host
    }
}

# Fallback for hosts without an interactive console (ISE, redirected input).
function Read-SceneNumber($Scenes) {
    for ($i = 0; $i -lt $Scenes.Count; $i++) {
        Write-Host ('{0,4}  {1,-8} {2}' -f ($i + 1), $Scenes[$i].Group, $Scenes[$i].Name)
    }
    $answer = Read-Host 'Scene number (empty to quit)'
    if (-not $answer) { return $null }
    $Scenes[[int]$answer - 1]
}

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------

function Invoke-Box([string[]]$Scripts) {
    $argv = @('run', 'apps/box_app', '--cfg', $Cfg, '--') + $BoxArgs + $Scripts
    Push-Location $Root
    try {
        & $Bake @argv | Out-Host
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    $code
}

if (-not (Test-Path $Bake)) { throw "run_box: bake3 not found at $Bake" }
Import-MsvcEnvironment
Set-LibclangPath

if ($Scene) {
    exit (Invoke-Box (Get-SceneScripts $Scene))
}

$interactive = -not [Console]::IsInputRedirected -and $Host.Name -eq 'ConsoleHost'
if ($NoMenu) {
    exit (Invoke-Box @())
}

$scenes = @(Get-Scenes)
if (-not $scenes.Count) { throw "run_box: no .flecs scenes found under $Root" }

$last = $null
while ($true) {
    $pick = if ($interactive) { Show-SceneMenu $scenes $last } else { Read-SceneNumber $scenes }
    if (-not $pick) { break }
    $last = $pick.Path
    Write-Host "run_box: launching $($pick.Rel)" -ForegroundColor Cyan
    $code = Invoke-Box (Get-SceneScripts $pick.Path)
    if ($code -ne 0) {
        Write-Host "run_box: box_app exited with code $code" -ForegroundColor Red
    }
    if (-not $interactive) { exit $code }
    Write-Host 'Press any key to return to the menu...' -ForegroundColor DarkGray
    [void][Console]::ReadKey($true)
}
