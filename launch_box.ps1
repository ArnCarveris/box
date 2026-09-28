#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Build (if needed) and run box_app with a scene, or the unit tests, from a menu.

.DESCRIPTION
    Runs under PowerShell 7 (pwsh) on Windows, Linux and macOS, and under Windows
    PowerShell 5.1. On Windows the MSVC environment is imported when cl.exe is not
    already on the path.

    Without arguments the script shows a menu, moved with Up/Down and activated with
    Enter or Space; Esc quits:

      Build      what is rebuilt from scratch before each run; Enter, Space or
                 Left/Right cycle through the modes (see -Build).
      GPU        the GPU box_app renders on: Default, High performance, Low power
                 and, on Windows, the adapters of this machine (see -Gpu).
      Scene      opens the scene selector: Up/Down/PgUp/PgDn/Home/End move, typing
                 filters, Backspace edits the filter, Enter picks, Esc goes back.
                 Until a scene is picked box_app runs its default scene.
      Run        builds and runs box_app with the selected scene.
      Tests      opens the same kind of selector over the test projects under test/,
                 with an entry that runs all of them (the default) and a 'failed'
                 entry that reruns only what failed last time (see -Test).
      Run tests  builds and runs the selected test projects (see -Test).

    When box_app or the tests finish the menu comes back with the same settings, and the
    next start of the script does too: the choices are saved in .launch_box/menu.json
    whenever the menu is left. -Build on the command line wins over the saved build mode.

    The scene list is cached in .launch_box/scenes.tsv and read again when a scene
    folder changes. F5, in the menu or the scene selector, rescans the folders anyway.

    flecs and bake3 are rebuilt from the repositories next to this one: ../flecs, which
    the flecs bundle in .bake/bundles is cloned from, and ../bake3.

    flecs is built with bake3, never CMake: before every build the launcher checks the
    flecs bundle and, when it is missing or out of date, builds it with bake3 from a
    project it generates in .launch_box and installs it where bake3 expects the
    bundle. box itself is always built by bake3.

    Scenes are collected from apps/box_app/etc/scenes and test/integration/{visual,
    command,bench}. Like tools/integration.py, a scene's <scene>.sources entries and its
    <scene>_content.flecs are loaded before the scene itself.

    bake3 is taken from ~/bake3/bake3 (bake3.exe on Windows) when it exists, otherwise
    from the path.

.PARAMETER Cfg
    Build configuration: debug (default) or release.

.PARAMETER Scene
    Run this scene directly, skipping the menu.

.PARAMETER NoMenu
    Run box_app with the default scene, skipping the menu.

.PARAMETER Build
    What to rebuild from scratch before running; in the menu this is the initial mode.
      None   no clean build; bake3 run builds whatever changed, as usual (default).
      Box    clean and build box and box_app.
      Flecs  Box + Flecs: also fast-forward the flecs bundle checkout from ../flecs
             (bake3 bundle update flecs) and build flecs again.
      Bake3  Box + Flecs + Bake3: first also build bake3 from ../bake3 and install it
             with 'bake setup', like its setup.sh.

.PARAMETER Test
    Run unit tests instead of box_app, skipping the menu: the name of a project under
    test/ (core, render, ...), 'all' or 'failed'. Each project runs with bake3 run
    test/<name> -- -j <Jobs>, after the clean builds -Build asks for (Box rebuilds box
    and the selected test projects). The exit code is non zero when anything fails.

    Every run keeps each project's --json report in .launch_box/tests-<Cfg>.
    'failed' reruns what those reports recorded as failed: each failed case on its own
    (bake3 run test/<name> -- Suite.case), and in full each project that left no
    report because it did not build or crashed. Cases that pass are cleared, so running
    'failed' again only repeats what still fails. A parameterized case reruns with its
    default parameters.

.PARAMETER Gpu
    GPU box_app renders on, passed to it as --gpu: default (the first adapter wgpu
    finds), high-performance, low-power, or part of an adapter name such as nvidia. In
    the menu this is the initial choice; box_app prints the adapter it picked. Tests
    are not affected.

.PARAMETER Jobs
    Parallel test processes per test project; defaults to 8.

.PARAMETER BoxArgs
    Extra arguments passed to box_app, for example --headless --frames 10.

.EXAMPLE
    ./launch_box.ps1
.EXAMPLE
    ./launch_box.ps1 -Cfg release
.EXAMPLE
    ./launch_box.ps1 -Scene apps/box_app/etc/scenes/town.flecs
.EXAMPLE
    ./launch_box.ps1 -NoMenu --headless --frames 10
.EXAMPLE
    ./launch_box.ps1 -Build Flecs -Scene apps/box_app/etc/scenes/town.flecs
.EXAMPLE
    ./launch_box.ps1 -Test all
.EXAMPLE
    ./launch_box.ps1 -Test core -Build Box
.EXAMPLE
    ./launch_box.ps1 -Test failed
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Cfg = 'debug',
    [string]$Scene,
    [switch]$NoMenu,
    [ValidateSet('None', 'Box', 'Flecs', 'Bake3')]
    [string]$Build = 'None',
    [string]$Gpu = 'default',
    [string]$Test,
    [ValidateRange(1, 256)]
    [int]$Jobs = 8,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$BoxArgs = @()
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$BakeSource = Join-Path (Split-Path $Root) 'bake3'
# Build modes in menu order; each one rebuilds everything the ones before it do.
$BuildModes = @('None', 'Box', 'Flecs', 'Bake3')
$BuildLabels = @{ None = 'None'; Box = 'Box'; Flecs = 'Box + Flecs'; Bake3 = 'Box + Flecs + Bake3' }
# GPU choices box_app knows by name, in menu order; adapter names follow them.
$GpuPresets = [ordered]@{ 'default' = 'Default'; 'high-performance' = 'High performance'; 'low-power' = 'Low power' }
# $IsWindows only exists from PowerShell 6; Windows PowerShell 5.1 is the Desktop edition.
$OnWindows = $IsWindows -or $PSVersionTable.PSEdition -eq 'Desktop'

# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------

function Find-Bake {
    $exe = if ($OnWindows) { 'bake3.exe' } else { 'bake3' }
    $local = [IO.Path]::Combine($HOME, 'bake3', $exe)
    if (Test-Path $local) { return $local }
    $cmd = Get-Command bake3 -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($cmd) { return $cmd.Source }
    throw "launch_box: bake3 not found at $local or on the path"
}

function Add-ToPath([string[]]$Dirs) {
    $env:PATH = (@($Dirs) + $env:PATH) -join [IO.Path]::PathSeparator
}

function Import-MsvcEnvironment {
    if (-not $OnWindows) { return }
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }

    $vsDir = $null
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vsDir = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (-not $vsDir) { $vsDir = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\18\Community' }

    $vcvars = Join-Path $vsDir 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "launch_box: vcvars64.bat not found under $vsDir" }

    # vcvars takes seconds, so what it changes is cached and replayed until Visual Studio,
    # its toolsets or the Windows SDKs change.
    $cache = Get-LauncherPath 'msvc-env.json'
    $key = Get-MsvcCacheKey $vsDir $vcvars
    $delta = $null
    if (Test-Path $cache) {
        $saved = Get-Content $cache -Raw | ConvertFrom-Json
        if ($saved.key -eq $key) { $delta = @($saved.vars) }
    }
    if (-not $delta) {
        $delta = @(Get-VcvarsDelta $vcvars)
        New-Item -ItemType Directory -Force (Split-Path $cache) | Out-Null
        @{ key = $key; vars = $delta } | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 $cache
    }
    foreach ($v in $delta) {
        $current = [Environment]::GetEnvironmentVariable($v.name)
        $value = if ($null -ne $v.prepend) { $v.prepend + $current } else { $v.value }
        Set-Item -Path "env:$($v.name)" -Value $value
    }

    $cmakeDir = Join-Path $vsDir 'Common7\IDE\CommonExtensions\Microsoft\CMake'
    if (Test-Path (Join-Path $cmakeDir 'CMake\bin\cmake.exe')) {
        Add-ToPath @((Join-Path $cmakeDir 'CMake\bin'), (Join-Path $cmakeDir 'Ninja'))
    }

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        if (Test-Path $cache) { Remove-Item -Force $cache }
        throw 'launch_box: could not set up the MSVC environment'
    }
}

# Identifies the Visual Studio setup the cached vcvars environment belongs to: vcvars64.bat
# and the installed MSVC toolsets and Windows SDKs.
function Get-MsvcCacheKey([string]$VsDir, [string]$Vcvars) {
    $dirs = @(
        (Join-Path $VsDir 'VC\Tools\MSVC'),
        (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include')
    )
    $versions = foreach ($d in $dirs) {
        if (Test-Path $d) { (Get-ChildItem $d -Directory | Sort-Object Name | ForEach-Object { $_.Name }) -join ',' }
    }
    "$Vcvars|$((Get-Item $Vcvars).LastWriteTimeUtc.Ticks)|$($versions -join '|')"
}

# Runs vcvars64.bat in cmd and returns what it changed: for each variable either the
# text it put in front of the old value (PATH, INCLUDE, LIB, ...) or its new value.
function Get-VcvarsDelta([string]$Vcvars) {
    $marker = '__launch_box_vcvars__'
    $before = @{}
    $after = @{}
    $seen = $false
    foreach ($line in cmd /c "set && echo $marker && `"$Vcvars`" >nul 2>&1 && set") {
        if ($line.Trim() -eq $marker) { $seen = $true; continue }
        if ($line -match '^([^=]+)=(.*)$') {
            if ($seen) { $after[$Matches[1]] = $Matches[2] } else { $before[$Matches[1]] = $Matches[2] }
        }
    }
    foreach ($name in $after.Keys) {
        $old = $before[$name]
        $new = $after[$name]
        if ($old -eq $new) { continue }
        if ($old -and $new.EndsWith($old)) {
            [ordered]@{ name = $name; prepend = $new.Substring(0, $new.Length - $old.Length) }
        } else {
            [ordered]@{ name = $name; value = $new }
        }
    }
}

function Set-LibclangPath {
    # wgpu-native's bindgen needs libclang when its bundle is (re)built. On Linux and
    # macOS bindgen finds it through llvm-config or the system library path.
    if (-not $OnWindows -or $env:LIBCLANG_PATH) { return }
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

# Path of $Name in .launch_box, the folder the launcher keeps its caches and choices in.
# It is not under .bake: bake3's clean (the Box build mode) deletes everything there but
# the bundles. The folder ignores itself for git, and bake3 skips it as a dot folder.
function Get-LauncherPath([string]$Name) {
    $dir = Join-Path $Root '.launch_box'
    if (-not (Test-Path (Join-Path $dir '.gitignore'))) {
        New-Item -ItemType Directory -Force $dir | Out-Null
        '*' | Write-Utf8File (Join-Path $dir '.gitignore')
    }
    Join-Path $dir $Name
}

# Writes the text piped in to $Path as UTF-8 without a byte order mark, which bake3's
# JSON parser rejects. Set-Content -Encoding utf8 writes one in Windows PowerShell 5.1.
function Write-Utf8File([string]$Path) {
    [IO.File]::WriteAllText($Path, (@($input) -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
}

$SceneGroups = @(
    @{ Name = 'app';     Dir = 'apps/box_app/etc/scenes' },
    @{ Name = 'visual';  Dir = 'test/integration/visual' },
    @{ Name = 'command'; Dir = 'test/integration/command' },
    @{ Name = 'bench';   Dir = 'test/integration/bench' }
)

function New-Scene([string]$Group, [string]$Rel) {
    [pscustomobject]@{
        Group = $Group
        Name  = [IO.Path]::GetFileNameWithoutExtension($Rel)
        Path  = [IO.Path]::GetFullPath((Join-Path $Root $Rel))
        Rel   = $Rel
    }
}

# Scans the scene folders for .flecs scenes, leaving out the <scene>_content files.
function Find-Scenes {
    foreach ($g in $SceneGroups) {
        $dir = Join-Path $Root $g.Dir
        if (-not (Test-Path $dir)) { continue }
        Get-ChildItem $dir -Filter *.flecs -File |
            Where-Object { $_.BaseName -notlike '*_content' } |
            Sort-Object Name |
            ForEach-Object { New-Scene $g.Name "$($g.Dir)/$($_.Name)" }
    }
}

# The scene list, from .launch_box/scenes.tsv while it is current. Scenes are only
# read from the top level of the scene folders, and adding, removing or renaming a file
# changes the modification time of its folder, so those times are the cache key. With
# -Refresh the folders are scanned and the cache rewritten whatever the key says (F5 in
# the menu), for changes the folder times miss.
function Get-Scenes([switch]$Refresh) {
    $cache = Get-LauncherPath 'scenes.tsv'
    $key = ($SceneGroups | ForEach-Object {
        $info = [IO.DirectoryInfo]::new((Join-Path $Root $_.Dir))
        if ($info.Exists) { "$($_.Dir)=$($info.LastWriteTimeUtc.Ticks)" } else { "$($_.Dir)=-" }
    }) -join ';'

    if (-not $Refresh -and (Test-Path $cache)) {
        $lines = [IO.File]::ReadAllLines($cache)
        if ($lines.Count -and $lines[0] -eq $key) {
            # Built inline: a New-Scene call per scene costs more than the folder scan.
            $sep = [IO.Path]::DirectorySeparatorChar
            for ($i = 1; $i -lt $lines.Count; $i++) {
                $parts = $lines[$i].Split("`t")
                [pscustomobject]@{
                    Group = $parts[0]
                    Name  = [IO.Path]::GetFileNameWithoutExtension($parts[1])
                    Path  = $Root + $sep + $parts[1].Replace('/', $sep)
                    Rel   = $parts[1]
                }
            }
            return
        }
    }

    $scenes = @(Find-Scenes)
    New-Item -ItemType Directory -Force (Split-Path $cache) | Out-Null
    [IO.File]::WriteAllLines($cache, [string[]](@($key) + ($scenes | ForEach-Object { "$($_.Group)`t$($_.Rel)" })))
    $scenes
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
# Tests
# ---------------------------------------------------------------------------

# Every test run keeps the --json report of each project here, per configuration, so the
# failed entry knows what failed last. A project that produced no report (it did not
# build, or crashed) gets {"broken": true} instead.
function Get-TestStateDir { Get-LauncherPath "tests-$Cfg" }

function Get-TestReportPath([string]$Name) { Join-Path (Get-TestStateDir) "$Name.json" }

# Failures recorded by the last runs of each project: an ordered map from project name
# to its failed Suite.case ids, or to $null when the whole project has to run again.
# Projects that no longer exist under test/ are skipped.
function Get-FailedTests {
    $failed = [ordered]@{}
    $dir = Get-TestStateDir
    if (-not (Test-Path $dir)) { return $failed }
    foreach ($file in Get-ChildItem $dir -Filter *.json -File | Sort-Object Name) {
        if (-not (Test-Path (Join-Path $Root "test/$($file.BaseName)/project.json"))) { continue }
        $report = Get-Content $file.FullName -Raw | ConvertFrom-Json
        if ($report.broken) { $failed[$file.BaseName] = $null; continue }
        $cases = @($report.tests |
            Where-Object { $_.status -in 'fail', 'error', 'timeout' } |
            ForEach-Object { "$($_.suite).$($_.case)" } |
            Select-Object -Unique)
        if ($cases.Count) { $failed[$file.BaseName] = $cases }
    }
    $failed
}

# Marks every run of Suite.case $Id in the saved report of project $Name as passed.
function Set-TestCasePassed([string]$Name, [string]$Id) {
    $path = Get-TestReportPath $Name
    $report = Get-Content $path -Raw | ConvertFrom-Json
    foreach ($t in $report.tests) {
        if ("$($t.suite).$($t.case)" -eq $Id) { $t.status = 'pass' }
    }
    $report | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 $path
}

# Test selector entries: 'all', 'failed', then every bake project directly under test/,
# in the shape the selector expects. Path holds the bake3 target of the entry.
function Get-TestEntries {
    $dir = Join-Path $Root 'test'
    $names = @()
    if (Test-Path $dir) {
        $names = @(Get-ChildItem $dir -Directory |
            Where-Object { Test-Path (Join-Path $_.FullName 'project.json') } |
            Sort-Object Name |
            ForEach-Object { $_.Name })
    }
    [pscustomobject]@{ Group = 'all'; Name = "every project ($($names.Count))"; Path = 'test'; Rel = 'test/*'; Projects = $names }

    $failed = Get-FailedTests
    $cases = 0
    $whole = 0
    foreach ($k in $failed.Keys) { if ($null -eq $failed[$k]) { $whole++ } else { $cases += $failed[$k].Count } }
    $parts = @()
    if ($cases) { $parts += "$cases case(s)" }
    if ($whole) { $parts += "$whole whole project(s)" }
    $summary = if ($parts) { ($parts -join ' + ') + " in $($failed.Count) project(s)" } else { 'none recorded' }
    # Box mode rebuilds the one failing project, or every project when several failed.
    $target = if ($failed.Count -eq 1) { "test/$(@($failed.Keys)[0])" } else { 'test' }
    [pscustomobject]@{ Group = 'failed'; Name = $summary; Path = $target; Rel = 'reruns the failures of the last runs'
        Projects = @($failed.Keys); Failed = $failed }

    foreach ($n in $names) {
        [pscustomobject]@{ Group = 'test'; Name = $n; Path = "test/$n"; Rel = "test/$n"; Projects = @($n) }
    }
}

# ---------------------------------------------------------------------------
# Menu
# ---------------------------------------------------------------------------

# Colors are optional: reading the current color is not reliable on Unix terminals, so
# the line resets to the terminal defaults instead of restoring the previous colors.
function Write-Line([string]$Text, [Nullable[ConsoleColor]]$Fg = $null,
        [Nullable[ConsoleColor]]$Bg = $null) {
    $width = [Console]::WindowWidth - 1
    if ($Text.Length -gt $width) { $Text = $Text.Substring(0, $width) }
    if ($null -ne $Fg) { [Console]::ForegroundColor = $Fg }
    if ($null -ne $Bg) { [Console]::BackgroundColor = $Bg }
    [Console]::Write($Text)
    if ($null -ne $Fg -or $null -ne $Bg) { [Console]::ResetColor() }
    [Console]::Write((' ' * ($width - $Text.Length)) + "`n")
}

function Read-MenuKey { [Console]::ReadKey($true) }

function Set-CursorVisible([bool]$Visible) {
    try { [Console]::CursorVisible = $Visible } catch { }
}

# Index of the entry of $List with the same Path and Group as $Entry, or -1.
function Find-EntryIndex($List, $Entry) {
    if ($Entry) {
        for ($i = 0; $i -lt $List.Count; $i++) {
            if ($List[$i].Path -eq $Entry.Path -and $List[$i].Group -eq $Entry.Group) { return $i }
        }
    }
    -1
}

# Filterable list selector for scenes and test projects, drawn inside the main menu.
# With $Refresh, F5 replaces the list with what that script block returns, keeping the
# highlighted entry when it is still there. Returns the picked entry, or $null on Esc.
function Show-PickList([string]$Title, $Scenes, $Selected, [scriptblock]$Refresh = $null) {
    $filter = ''
    $index = [Math]::Max(0, (Find-EntryIndex $Scenes $Selected))
    $offset = 0
    $keep = $null
    $note = ''
    $keys = if ($Refresh) { 'Enter pick, F5 refresh, Esc back' } else { 'Enter pick, Esc back' }

    Clear-Host
    try {
        while ($true) {
            $shown = @($Scenes | Where-Object {
                -not $filter -or "$($_.Group) $($_.Name)" -like "*$filter*"
            })
            if ($keep) { $index = [Math]::Max(0, (Find-EntryIndex $shown $keep)); $keep = $null }
            if ($index -ge $shown.Count) { $index = [Math]::Max(0, $shown.Count - 1) }

            $rows = [Math]::Max(3, [Console]::WindowHeight - 6)
            if ($index -lt $offset) { $offset = $index }
            if ($index -ge $offset + $rows) { $offset = $index - $rows + 1 }
            $offset = [Math]::Max(0, [Math]::Min($offset, [Math]::Max(0, $shown.Count - $rows)))

            [Console]::SetCursorPosition(0, 0)
            Write-Line "select $Title  -  Up/Down move, type to filter, $keys" Cyan
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
            if ($note) { $status = "$note  -  $status"; $note = '' }
            Write-Line ''
            Write-Line $status DarkGray

            $key = Read-MenuKey
            switch ($key.Key) {
                'F5' {
                    if ($Refresh) {
                        if ($shown.Count) { $keep = $shown[$index] }
                        $Scenes = @(& $Refresh)
                        $note = "refreshed: $($Scenes.Count) entries"
                    }
                }
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
        Clear-Host
    }
}

# Moves $State.Build $Step places through $BuildModes, wrapping around.
function Step-BuildMode($State, [int]$Step) {
    $i = [Array]::IndexOf($BuildModes, $State.Build)
    $State.Build = $BuildModes[($i + $Step + $BuildModes.Count) % $BuildModes.Count]
}

# Display adapters Windows reports, without its software renderers. Other systems have
# no quick way to list them, so there the menu offers the presets only.
function Find-GpuAdapters {
    if (-not $OnWindows) { return }
    try {
        Get-CimInstance Win32_VideoController -ErrorAction Stop |
            ForEach-Object { $_.Name } |
            Where-Object { $_ -and $_ -notmatch '^Microsoft Basic' } |
            Select-Object -Unique
    } catch {
    }
}

# The GPU choices in menu order: the presets, the adapters of this machine, and $Current
# when it names something else (a choice saved on another machine). The adapters are
# looked up once, the first time the choices are needed.
function Get-GpuChoices([string]$Current) {
    if ($null -eq $script:gpuAdapters) { $script:gpuAdapters = @(Find-GpuAdapters) }
    $choices = @($GpuPresets.Keys) + @($script:gpuAdapters | Where-Object { $_ -notin $GpuPresets.Keys })
    if ($Current -and $Current -notin $choices) { $choices += $Current }
    $choices
}

function Get-GpuLabel([string]$Value) {
    if (-not $Value) { return $GpuPresets['default'] }
    if ($GpuPresets.Contains($Value)) { return $GpuPresets[$Value] }
    $Value
}

# Moves $State.Gpu $Step places through the GPU choices, wrapping around.
function Step-GpuChoice($State, [int]$Step) {
    $choices = @(Get-GpuChoices $State.Gpu)
    $i = [Array]::IndexOf($choices, [string]$State.Gpu)
    if ($i -lt 0) { $i = 0 }
    $State.Gpu = $choices[($i + $Step + $choices.Count) % $choices.Count]
}

# Main menu rows, in order. A blank line is drawn before the Tests row.
$MenuBuild = 0; $MenuGpu = 1; $MenuScene = 2; $MenuRun = 3; $MenuTests = 4; $MenuRunTests = 5

# Main menu: Build mode selection, Scene and Tests selectors, and the Run and Run tests
# actions. Updates $State.Build, $State.Scene, $State.Tests and $State.Item in place and
# returns 'Run' or 'Test' for the chosen action, or $null for Esc.
function Show-MainMenu($State, $Scenes, $TestEntries) {
    # The getter is Windows only; elsewhere assume the cursor was visible.
    $cursorVisible = $true
    if ($OnWindows) { $cursorVisible = [Console]::CursorVisible }
    Set-CursorVisible $false
    Clear-Host
    $note = ''
    # After a refresh the selected scene is looked up in the new list; a scene that is
    # gone falls back to box_app's default scene, with a note saying so. Dot-sourced, so
    # it sets $note of this function.
    $resync = {
        if ($State.Scene) {
            $i = Find-EntryIndex $Scenes $State.Scene
            if ($i -ge 0) {
                $State.Scene = $Scenes[$i]
            } else {
                $note = "$($State.Scene.Rel) is gone; back to the default scene"
                $State.Scene = $null
            }
        }
    }
    try {
        while ($true) {
            $scene = if ($State.Scene) { '{0,-8} {1}' -f $State.Scene.Group, $State.Scene.Name } else { '(default)' }
            $tests = '{0,-8} {1}' -f $State.Tests.Group, $State.Tests.Name
            $items = @(
                "Build      < $($BuildLabels[$State.Build]) >",
                "GPU        < $(Get-GpuLabel $State.Gpu) >",
                "Scene      $scene",
                "Run",
                "Tests      $tests",
                "Run tests"
            )

            [Console]::SetCursorPosition(0, 0)
            Write-Line "box launcher ($Cfg)  -  Up/Down move, Enter/Space select, Left/Right change, F5 refresh scenes, Esc quit" Cyan
            Write-Line ''
            for ($i = 0; $i -lt $items.Count; $i++) {
                if ($i -eq $MenuTests) { Write-Line '' }
                if ($i -eq $State.Item) {
                    Write-Line ("> " + $items[$i]) Black Cyan
                } else {
                    Write-Line ("  " + $items[$i])
                }
            }
            Write-Line ''
            $status = if ($State.Item -ge $MenuTests) {
                "bake3 run $($State.Tests.Rel) -- -j $Jobs"
            } elseif ($State.Item -eq $MenuGpu) {
                "box_app --gpu $(if ($State.Gpu) { $State.Gpu } else { 'default' }); it prints the adapter it picks"
            } elseif ($State.Scene) {
                $State.Scene.Rel
            } else {
                'box_app runs its default scene'
            }
            if ($note) { $status = "$note  -  $status"; $note = '' }
            Write-Line $status DarkGray

            $key = Read-MenuKey
            switch ($key.Key) {
                'F5' {
                    $Scenes = @(Get-Scenes -Refresh)
                    $note = "refreshed: $($Scenes.Count) scenes"
                    . $resync
                }
                'UpArrow'   { if ($State.Item -gt 0) { $State.Item-- } }
                'DownArrow' { if ($State.Item -lt $items.Count - 1) { $State.Item++ } }
                'Home'      { $State.Item = 0 }
                'End'       { $State.Item = $items.Count - 1 }
                'Escape'    { return $null }
                'LeftArrow'  {
                    if ($State.Item -eq $MenuBuild) { Step-BuildMode $State -1 }
                    if ($State.Item -eq $MenuGpu) { Step-GpuChoice $State -1 }
                }
                'RightArrow' {
                    if ($State.Item -eq $MenuBuild) { Step-BuildMode $State 1 }
                    if ($State.Item -eq $MenuGpu) { Step-GpuChoice $State 1 }
                }
                { $_ -eq 'Enter' -or $_ -eq 'Spacebar' } {
                    switch ($State.Item) {
                        $MenuBuild { Step-BuildMode $State 1 }
                        $MenuGpu { Step-GpuChoice $State 1 }
                        $MenuScene {
                            $pick = Show-PickList 'scene' $Scenes $State.Scene { Get-Scenes -Refresh }
                            # The selector may have refreshed the list; the cache now has it.
                            $Scenes = @(Get-Scenes)
                            if ($pick) { $State.Scene = $pick; $State.Item = $MenuRun } else { . $resync }
                        }
                        $MenuRun { return 'Run' }
                        $MenuTests {
                            $pick = Show-PickList 'tests' $TestEntries $State.Tests
                            if ($pick) { $State.Tests = $pick; $State.Item = $MenuRunTests }
                        }
                        $MenuRunTests { return 'Test' }
                    }
                }
            }
        }
    } finally {
        Set-CursorVisible $cursorVisible
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

# Quotes arguments into one command line the way the C runtime (and .NET on Unix)
# splits it again: quotes around arguments with spaces or quotes, with the backslashes in
# front of a quote, or of the closing quote, doubled.
function ConvertTo-CommandLine([string[]]$Argv) {
    ($Argv | ForEach-Object {
        if ($_ -and $_ -notmatch '[\s"]') { $_ }
        else { '"' + (($_ -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"' }
    }) -join ' '
}

# cl.exe echoes the name of every file it compiles on a line of its own, after bake3 has
# already printed that file with its progress ('[ 40%] lod/error.c'); these are dropped.
$CompilerEcho = '^[^\s\\/\[\]:]+\.(c|cc|cpp|cxx)\s*$'

# Runs $Exe in $Dir and returns its exit code. By default the child writes straight to
# the console, since C programs buffer their output when it goes to a pipe. With -Filter
# its stdout is read line by line instead and printed without the compiler echo lines;
# that only suits programs that flush every line, such as bake3. stderr always goes to
# the console.
function Invoke-Native([string]$Exe, [string[]]$Argv, [string]$Dir = $Root, [switch]$Filter) {
    $psi = [Diagnostics.ProcessStartInfo]::new($Exe, (ConvertTo-CommandLine $Argv))
    $psi.UseShellExecute = $false
    $psi.WorkingDirectory = $Dir
    if ($Filter) {
        $psi.RedirectStandardOutput = $true
        $psi.StandardOutputEncoding = [Console]::OutputEncoding
    }
    $process = [Diagnostics.Process]::Start($psi)
    try {
        if ($Filter) {
            while ($null -ne ($line = $process.StandardOutput.ReadLine())) {
                if ($line -notmatch $CompilerEcho) { [Console]::Out.WriteLine($line) }
            }
        }
        $process.WaitForExit()
        $process.ExitCode
    } finally {
        $process.Dispose()
    }
}

# Runs bake3 with its output filtered, or straight on the console with -Console (for
# bake3 run of box_app, whose own output would be held back by a pipe).
function Invoke-Bake([string[]]$Argv, [switch]$Console) {
    Invoke-Native $Bake $Argv -Filter:(-not $Console)
}

# 0.4s, 12.3s, 2m 05s
function Format-Elapsed([TimeSpan]$Span) {
    if ($Span.TotalSeconds -lt 60) { return '{0:0.0}s' -f $Span.TotalSeconds }
    '{0}m {1:00}s' -f [int][Math]::Floor($Span.TotalMinutes), $Span.Seconds
}

# Prints how long $What took: '<What> <Verb> 12.3s' in green when $Code is 0, else
# '<What> failed after 12.3s (exit code N)' in red.
function Write-Elapsed([string]$What, [Diagnostics.Stopwatch]$Watch, [int]$Code = 0, [string]$Verb = 'took') {
    $time = Format-Elapsed $Watch.Elapsed
    if ($Code -eq 0) {
        Write-Host "launch_box: $What $Verb $time" -ForegroundColor Green
    } else {
        Write-Host "launch_box: $What failed after $time (exit code $Code)" -ForegroundColor Red
    }
}

# Runs bake3 with filtered output and prints how long $What took. Returns the exit code.
function Invoke-BakeStep([string]$What, [string[]]$Argv) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $code = Invoke-Bake $Argv
    Write-Elapsed $What $watch $code
    $code
}

# Builds bake3 in ../bake3 and installs it with 'bake setup', like its setup.sh. The
# Makefile does not drive MSVC, so on Windows cl compiles every source to its own object
# (sources in different folders share file names) and link produces build/bake.exe.
function Build-Bake3 {
    if (-not (Test-Path (Join-Path $BakeSource 'src'))) {
        throw "launch_box: bake3 sources not found at $BakeSource"
    }
    Write-Host "launch_box: building bake3 in $BakeSource" -ForegroundColor Cyan
    Push-Location $BakeSource
    try {
        if ($OnWindows) {
            if (Test-Path build/obj) { Remove-Item -Recurse -Force build/obj }
            New-Item -ItemType Directory -Force build/obj | Out-Null
            $common = @('/nologo', '/std:c11', '/W3', '/Zi', '/Od', '/MD', '/utf-8', '/FS',
                '/D_CRT_SECURE_NO_WARNINGS', '/Fdbuild\obj\bake.pdb')
            $units = @()
            foreach ($f in Get-ChildItem src -Recurse -Filter *.c -File) {
                $rel = (Resolve-Path -Relative $f.FullName) -replace '^\.[\\/]', ''
                $obj = ($rel -replace '[\\/]', '_') -replace '\.c$', '.obj'
                $units += , @('/Iinclude', '/Ideps', '/Isrc', '/c', $rel, "/Fobuild\obj\$obj")
            }
            $units += , @('/Ideps', '/c', 'deps\parson.c', '/Fobuild\obj\deps_parson.obj')
            $units += , @('/DFLECS_CUSTOM_BUILD', '/DFLECS_LOG', '/DFLECS_OS_API_IMPL', '/Ideps',
                '/c', 'deps\flecs.c', '/Fobuild\obj\deps_flecs.obj')
            foreach ($u in $units) {
                $out = & cl.exe @common @u
                $code = $LASTEXITCODE
                if ($code -ne 0) { $out | Out-Host; return $code }
            }
            $objs = @((Get-ChildItem build/obj -Filter *.obj).FullName)
            $code = Invoke-Native 'link.exe' (@('/nologo', '/DEBUG', '/OUT:build\bake.exe') + $objs +
                @('dbghelp.lib', 'ws2_32.lib')) $BakeSource
            if ($code -ne 0) { return $code }
            $exe = Join-Path $BakeSource 'build\bake.exe'
        } else {
            [void](Invoke-Native 'make' @('clean') $BakeSource)
            $code = Invoke-Native 'make' @('-j', '8', 'OPTIMIZE=0') $BakeSource
            if ($code -ne 0) { return $code }
            $exe = Join-Path $BakeSource 'build/bake'
            if ($IsMacOS) { [void](Invoke-Native 'codesign' @('-f', '-s', '-', $exe) $BakeSource) }
        }
        Invoke-Native $exe @('setup') $BakeSource
    } finally {
        Pop-Location
    }
}

# ---------------------------------------------------------------------------
# flecs
# ---------------------------------------------------------------------------
# box/project.json declares flecs as a bundle, and bake3 builds bundles with CMake. The
# launcher builds it with bake3 instead: it generates a package project from the bundle
# checkout in .launch_box/flecs-<Cfg> (bake3 skips dot folders when it discovers
# projects), builds it, lays the headers and library out like the CMake install, and
# writes the .bake_bundle_built marker with the fingerprint bake3 compares it to (see
# bake_bundle_fingerprint in bake3/src/bundle/bundle.c), so bake3 takes the bundle as
# built. Should bake3 ever compute a different fingerprint, it falls back to CMake.

# The arch-os-cfg folder name bake3 builds into, e.g. x64-Windows-debug.
function Get-BakeTriplet {
    $arch = switch ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()) {
        'X64' { 'x64' } 'Arm64' { 'arm64' } 'X86' { 'x86' } 'Arm' { 'arm' } default { 'unknown' }
    }
    $os = if ($OnWindows) { 'Windows' } elseif ($IsMacOS) { 'Darwin' } else { 'Linux' }
    "$arch-$os-$Cfg"
}

# The flecs bundle as box/project.json declares it, with the folders bake3 derives for
# it, spelled the way bake3 joins them ($null when there is no flecs bundle).
function Get-FlecsBundle {
    $bundle = (Get-Content (Join-Path $Root 'project.json') -Raw | ConvertFrom-Json).bundle.flecs
    if (-not $bundle) { return $null }
    $ref = if ($bundle.commit) { "commits/$($bundle.commit)" }
        elseif ($bundle.tag) { "tags/$($bundle.tag)" }
        elseif ($bundle.branch) { "branches/$($bundle.branch)" }
        else { 'default' }
    $sep = [IO.Path]::DirectorySeparatorChar
    $dir = $Root + $sep + '.bake' + $sep + 'bundles' + $sep + 'flecs' + $sep + $ref
    $src = $dir + $sep + 'src'
    [pscustomobject]@{
        Bundle   = $bundle
        Src      = $src
        BuildSrc = if ($bundle.subdir) { $src + $sep + $bundle.subdir } else { $src }
        Install  = $dir + $sep + 'install' + $sep + (Get-BakeTriplet)
    }
}

# Newest modification time under $Dir in nanoseconds since 1970, skipping folders whose
# name starts with a dot, like bake_os_tree_newest_mtime. Windows keeps file times in
# 100 ns steps, which .NET reads exactly; other systems keep nanoseconds, which GNU find
# prints, with .NET (100 ns) as the fallback.
function Get-TreeMtime([string]$Dir) {
    if (-not $OnWindows) {
        $times = & find $Dir -mindepth 1 -type d -name '.*' -prune -o -type f -printf '%T@\n' 2>$null
        if ($LASTEXITCODE -eq 0 -and $times) {
            $newest = [long]0
            foreach ($t in $times) {
                $parts = "$t".Split('.')
                $ns = [long]$parts[0] * 1000000000 + [long](($parts[1] + '000000000').Substring(0, 9))
                if ($ns -gt $newest) { $newest = $ns }
            }
            return $newest
        }
    }
    $script:treeNewest = [long]0
    $walk = {
        param([IO.DirectoryInfo]$DirInfo)
        foreach ($file in $DirInfo.EnumerateFiles()) {
            if ($file.LastWriteTimeUtc.Ticks -gt $script:treeNewest) { $script:treeNewest = $file.LastWriteTimeUtc.Ticks }
        }
        foreach ($sub in $DirInfo.EnumerateDirectories()) {
            if (-not $sub.Name.StartsWith('.')) { & $walk $sub }
        }
    }
    & $walk ([IO.DirectoryInfo]::new($Dir))
    if (-not $script:treeNewest) { return 0 }
    [Math]::Max([long]0, ($script:treeNewest - 621355968000000000) * 100)
}

# The text bake3 writes to, and compares with, the bundle's .bake_bundle_built marker.
function Get-FlecsFingerprint($Flecs) {
    $b = $Flecs.Bundle
    $lines = @("build_system=$($b.'build-system')")
    if ($b.'build-system' -eq 'cargo') {
        $lines += "cargo_profile=$(if ($Cfg -eq 'release') { 'release' } else { 'debug' })"
    }
    $lines += "profile=$($b.profile)", "subdir=$($b.subdir)", "library=$($b.library)",
        "header_only=$(if ($b.'header-only') { 1 } else { 0 })", "install_dir=$($Flecs.Install)"
    foreach ($a in @($b.'cmake-args')) { if ($a) { $lines += "cmake_arg=$a" } }
    foreach ($a in @($b.'cargo-args')) { if ($a) { $lines += "cargo_arg=$a" } }
    $lines += "source_mtime=$(Get-TreeMtime $Flecs.BuildSrc)"
    ($lines -join "`n") + "`n"
}

# Clones the bundle checkout like bake3 does on its first build.
function Initialize-FlecsCheckout($Flecs) {
    $repo = $Flecs.Bundle.repository
    if ($repo -notmatch '^[a-z]+://|^git@' -and -not [IO.Path]::IsPathRooted($repo)) {
        $repo = [IO.Path]::GetFullPath((Join-Path $Root $repo))
    }
    if (Test-Path $Flecs.Src) { Remove-Item -Recurse -Force $Flecs.Src }
    New-Item -ItemType Directory -Force (Split-Path $Flecs.Src) | Out-Null
    Write-Host "launch_box: cloning the flecs bundle from $repo" -ForegroundColor Cyan
    Invoke-Native 'git' @('clone', '--recurse-submodules', '--depth', '1', '--shallow-submodules',
        $repo, $Flecs.Src)
}

# Builds flecs with bake3 as a static library with the defines the bundle passes CMake
# in CMAKE_C_FLAGS, installs it where bake3 looks for the bundle and writes the marker.
function Build-FlecsBundle($Flecs, [string]$Fingerprint) {
    $project = Get-LauncherPath "flecs-$Cfg"
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Write-Host "launch_box: building flecs with bake3 ($Cfg)" -ForegroundColor Cyan
    if (Test-Path $project) { Remove-Item -Recurse -Force $project }
    New-Item -ItemType Directory -Force $project | Out-Null
    Copy-Item -Recurse (Join-Path $Flecs.BuildSrc 'src') (Join-Path $project 'src')

    $defines = @('flecs_STATIC')
    foreach ($a in @($Flecs.Bundle.'cmake-args')) {
        if ($a -match '^-DCMAKE_C_FLAGS=(.*)$') {
            $defines += [regex]::Matches($Matches[1], '-D(\S+)') | ForEach-Object { $_.Groups[1].Value }
        }
    }
    $name = if ($Flecs.Bundle.library) { $Flecs.Bundle.library } else { 'flecs' }
    [ordered]@{
        id       = $name
        type     = 'package'
        value    = @{ public = $false }
        'lang.c' = [ordered]@{
            'c-standard' = 'c99'
            defines      = $defines
            include      = @(ConvertTo-ForwardSlash (Join-Path $Flecs.BuildSrc 'include'))
        }
    } | ConvertTo-Json -Depth 5 | Write-Utf8File (Join-Path $project 'project.json')

    $code = Invoke-Bake @('rebuild', (ConvertTo-ForwardSlash $project), '--cfg', $Cfg)
    if ($code -ne 0) { return $code }

    $libFile = if ($OnWindows) { "$name.lib" } else { "lib$name.a" }
    $lib = Get-ChildItem (Join-Path $project '.bake') -Recurse -File -Filter $libFile |
        Where-Object { $_.DirectoryName -like "*-$Cfg*" } | Select-Object -First 1
    if (-not $lib) {
        Write-Host "launch_box: bake3 built no $libFile under $project" -ForegroundColor Red
        return 1
    }

    if (Test-Path $Flecs.Install) { Remove-Item -Recurse -Force $Flecs.Install }
    New-Item -ItemType Directory -Force (Join-Path $Flecs.Install 'lib') | Out-Null
    Copy-Item -Recurse (Join-Path $Flecs.BuildSrc 'include') (Join-Path $Flecs.Install 'include')
    Copy-Item $lib.FullName (Join-Path $Flecs.Install "lib/$libFile")
    [IO.File]::WriteAllText((Join-Path $Flecs.Install '.bake_bundle_built'), $Fingerprint)
    Write-Elapsed 'flecs build with bake3' $watch
    0
}

# Makes sure bake3 finds the flecs bundle built, building it with bake3 when $Force is
# set or when its marker is missing or out of date, so bake3 never builds it with CMake.
function Sync-FlecsBundle([bool]$Force) {
    $flecs = Get-FlecsBundle
    if (-not $flecs) { return 0 }
    if (-not (Test-Path (Join-Path $flecs.Src '.git'))) {
        $code = Initialize-FlecsCheckout $flecs
        if ($code -ne 0) { return $code }
    }
    $fingerprint = Get-FlecsFingerprint $flecs
    $marker = Join-Path $flecs.Install '.bake_bundle_built'
    if (-not $Force -and (Test-Path $marker) -and [IO.File]::ReadAllText($marker) -eq $fingerprint) {
        return 0
    }
    Build-FlecsBundle $flecs $fingerprint
}

# Fast-forwards the flecs bundle checkout from ../flecs and builds flecs again with bake3.
function Reset-FlecsBundle {
    $flecs = Get-FlecsBundle
    if (-not $flecs) { return 0 }
    if (Test-Path (Join-Path $flecs.Src '.git')) {
        Write-Host 'launch_box: updating the flecs bundle' -ForegroundColor Cyan
        $code = Invoke-Bake @('bundle', 'update', 'flecs')
        if ($code -ne 0) { return $code }
    }
    Sync-FlecsBundle $true
}

# Runs the clean builds $Mode asks for, from the outermost dependency in, ending with a
# rebuild of $Target. Rebuilding a project cleans and builds it and the box package it
# uses; a folder such as test rebuilds every project under it, box only once. flecs is
# built with bake3 whenever its bundle is out of date, whatever the mode; the other
# bundles (wgpu-native, glfw, ...) are only rebuilt by bake3 when their build is gone.
# Returns the exit code of the first step that fails, or 0.
function Invoke-CleanBuild([string]$Target, [string]$Mode) {
    Initialize-BuildEnvironment
    $level = [Array]::IndexOf($BuildModes, $Mode)
    if ($level -ge 3) {
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $code = Build-Bake3
        Write-Elapsed 'bake3 build and install' $watch $code
        if ($code -ne 0) { return $code }
        $script:Bake = Find-Bake
    }
    $code = if ($level -ge 2) { Reset-FlecsBundle } else { Sync-FlecsBundle $false }
    if ($code -ne 0) { Write-Host 'launch_box: flecs build failed' -ForegroundColor Red; return $code }
    if ($level -ge 1) {
        Write-Host "launch_box: rebuilding $Target ($Cfg)" -ForegroundColor Cyan
        $code = Invoke-BakeStep "rebuild of $Target" @('rebuild', $Target, '--cfg', $Cfg)
        if ($code -ne 0) { return $code }
    }
    0
}

# Builds box_app after the clean builds $Mode asks for, then runs it with $Scripts. The
# build output is filtered; box_app itself runs on the console.
function Invoke-Box([string[]]$Scripts, [string]$Mode, [string]$GpuChoice = $Gpu) {
    $code = Invoke-CleanBuild 'apps/box_app' $Mode
    if ($code -ne 0) { return $code }
    if ([Array]::IndexOf($BuildModes, $Mode) -lt 1) {
        $code = Invoke-BakeStep 'build of apps/box_app' @('build', 'apps/box_app', '--cfg', $Cfg)
        if ($code -ne 0) { return $code }
    }
    $watch = [Diagnostics.Stopwatch]::StartNew()
    # --gpu goes first, so a --gpu among the extra arguments of the command line wins.
    $gpuArgs = if ($GpuChoice) { @('--gpu', $GpuChoice) } else { @() }
    $code = Invoke-Bake (@('run', 'apps/box_app', '--cfg', $Cfg, '--') + $gpuArgs + $BoxArgs + $Scripts) -Console
    Write-Elapsed 'box_app' $watch $code 'ran for'
    $code
}

# The test program bake3 builds for test project $Name.
function Get-TestExe([string]$Name) {
    $project = Get-Content (Join-Path $Root "test/$Name/project.json") -Raw | ConvertFrom-Json
    $output = if ($project.value.output) { $project.value.output } else { $project.id }
    $ext = if ($OnWindows) { '.exe' } else { '' }
    Join-Path $Root "test/$Name/.bake/$(Get-BakeTriplet)/$output$ext"
}

# Runs test project $Name with $Argv. When $Built, the test program runs directly the way
# bake3 runs it (from the project folder); otherwise bake3 run builds this project on
# its own first. Returns the exit code.
function Invoke-TestProgram([string]$Name, [string[]]$Argv, [bool]$Built) {
    $exe = Get-TestExe $Name
    if ($Built -and (Test-Path $exe)) {
        return Invoke-Native $exe $Argv (Join-Path $Root "test/$Name")
    }
    Invoke-Bake (@('run', "test/$Name", '--cfg', $Cfg, '--') + $Argv)
}

# Runs test project $Name with a --json report kept for the failed entry, and returns
# the exit code. Without a report afterwards the project is recorded as broken.
function Invoke-TestProject([string]$Name, [bool]$Built) {
    $report = Get-TestReportPath $Name
    New-Item -ItemType Directory -Force (Split-Path $report) | Out-Null
    if (Test-Path $report) { Remove-Item -Force $report }
    Write-Host "launch_box: testing test/$Name" -ForegroundColor Cyan
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $code = Invoke-TestProgram $Name @('-j', "$Jobs", '--json', (ConvertTo-ForwardSlash $report)) $Built
    if (-not (Test-Path $report)) { '{"broken": true}' | Set-Content -Encoding utf8 $report }
    Write-Elapsed "test/$Name" $watch $code 'passed in'
    $code
}

# Prints the outcome of a test run: what still fails out of $Total, or that all passed.
function Write-TestSummary([string[]]$Failed, [int]$Total, [string]$What) {
    if ($Failed.Count) {
        Write-Host ("launch_box: {0} of {1} {2} failed: {3}" -f $Failed.Count, $Total, $What, ($Failed -join ', ')) -ForegroundColor Red
    } else {
        Write-Host "launch_box: all $Total $What passed" -ForegroundColor Green
    }
}

# Reruns the failures recorded by the last runs: every failed case on its own (the test
# program with Suite.case), and projects without a report in full. Cases that pass are
# marked passed in the saved report.
function Invoke-FailedTests($Entry, [bool]$Built) {
    $failed = $Entry.Failed
    $still = @()
    $total = 0
    $first = 0
    foreach ($name in @($failed.Keys)) {
        $cases = $failed[$name]
        if ($null -eq $cases) {
            $total++
            $code = Invoke-TestProject $name $Built
            if ($code -ne 0) { $still += "test/$name"; if (-not $first) { $first = $code } }
            continue
        }
        foreach ($id in $cases) {
            $total++
            Write-Host "launch_box: rerunning test/$name $id" -ForegroundColor Cyan
            $watch = [Diagnostics.Stopwatch]::StartNew()
            $code = Invoke-TestProgram $name @($id) $Built
            Write-Elapsed "$name $id" $watch $code 'passed in'
            if ($code -eq 0) {
                Set-TestCasePassed $name $id
            } else {
                $still += "$name $id"
                if (-not $first) { $first = $code }
            }
        }
    }

    Write-TestSummary $still $total 'rerun test(s)'
    $first
}

# Runs the tests of $Entry after the clean builds $Mode asks for, then prints which
# projects failed. Returns 0 when all passed, else the first failing exit code.
#
# All selected projects are built by one bake3 call, and their test programs then run
# directly: a bake3 run per project repeats project discovery and the up to date check
# every time. When that build fails, each project runs with its own bake3 run instead,
# so one project that does not build does not stop the others.
function Invoke-Tests($Entry, [string]$Mode) {
    if ($Entry.Group -eq 'failed' -and -not $Entry.Failed.Count) {
        Write-Host "launch_box: no failed tests recorded for $Cfg; run the tests first" -ForegroundColor Yellow
        return 0
    }

    $code = Invoke-CleanBuild $Entry.Path $Mode
    if ($code -ne 0) { return $code }
    $built = [Array]::IndexOf($BuildModes, $Mode) -ge 1
    if (-not $built) {
        $built = (Invoke-BakeStep "build of $($Entry.Path)" @('build', $Entry.Path, '--cfg', $Cfg)) -eq 0
        if (-not $built) {
            Write-Host 'launch_box: building and running each test project on its own' -ForegroundColor Yellow
        }
    }
    if ($Entry.Group -eq 'failed') { return Invoke-FailedTests $Entry $built }

    $failed = @()
    $first = 0
    foreach ($name in $Entry.Projects) {
        $code = Invoke-TestProject $name $built
        if ($code -ne 0) {
            $failed += $name
            if (-not $first) { $first = $code }
        }
    }

    Write-TestSummary $failed @($Entry.Projects).Count 'test project(s)'
    $first
}

# The test selector entry that -Test names: 'all', 'failed' or a project under test/.
function Find-TestEntry($TestEntries, [string]$Name) {
    if ($Name -in 'all', 'failed') {
        return $TestEntries | Where-Object { $_.Group -eq $Name } | Select-Object -First 1
    }
    $entry = $TestEntries | Where-Object { $_.Group -eq 'test' -and $_.Name -eq $Name } | Select-Object -First 1
    if (-not $entry) {
        $known = ($TestEntries | Where-Object { $_.Group -eq 'test' } | ForEach-Object { $_.Name }) -join ', '
        throw "launch_box: no test project '$Name' under test/; use all, failed or one of: $known"
    }
    $entry
}

# Prints the total time of a run and its exit code, and returns the code.
function Complete-Run([int]$Code, [Diagnostics.Stopwatch]$Watch) {
    $time = Format-Elapsed $Watch.Elapsed
    if ($Code -eq 0) {
        Write-Host "launch_box: finished in $time" -ForegroundColor Green
    } else {
        Write-Host "launch_box: finished with exit code $Code in $time" -ForegroundColor Red
    }
    $Code
}

# The menu choices are kept in .launch_box/menu.json between runs: the build mode,
# the GPU, the scene (by its path in the repository), the tests entry (all, failed or a
# project name) and the highlighted row.
function Get-MenuStatePath { Get-LauncherPath 'menu.json' }

# Saves the menu choices of $State. A failure only warns: it must not stop a run.
function Save-MenuState($State) {
    try {
        $path = Get-MenuStatePath
        New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
        [ordered]@{
            build = $State.Build
            gpu   = $State.Gpu
            scene = if ($State.Scene) { $State.Scene.Rel } else { $null }
            tests = if ($State.Tests) { [ordered]@{ group = $State.Tests.Group; name = $State.Tests.Name } } else { $null }
            item  = $State.Item
        } | ConvertTo-Json | Set-Content -Encoding utf8 $path
    } catch {
        Write-Warning "launch_box: could not save the menu choices: $($_.Exception.Message)"
    }
}

# Restores saved menu choices into $State. The scene and tests entry come back as stubs
# the menu loop looks up in the current lists, so ones that are gone fall back to the
# defaults there. $KeepBuild and $KeepGpu leave the build mode and the GPU alone (-Build
# and -Gpu given on the command line).
function Restore-MenuState($State, [bool]$KeepBuild, [bool]$KeepGpu = $false) {
    $path = Get-MenuStatePath
    if (-not (Test-Path $path)) { return }
    try {
        $saved = Get-Content $path -Raw | ConvertFrom-Json
    } catch {
        Write-Warning "launch_box: ignoring unreadable $path"
        return
    }
    if (-not $KeepBuild -and $saved.build -in $BuildModes) { $State.Build = $saved.build }
    if (-not $KeepGpu -and $saved.gpu -is [string] -and $saved.gpu) { $State.Gpu = $saved.gpu }
    if ($saved.scene) { $State.Scene = [pscustomobject]@{ Rel = [string]$saved.scene } }
    if ($saved.tests -and $saved.tests.group) {
        $State.Tests = [pscustomobject]@{ Group = [string]$saved.tests.group; Name = [string]$saved.tests.name }
    }
    if ($saved.item -is [ValueType] -and $saved.item -ge $MenuBuild -and $saved.item -le $MenuRunTests) {
        $State.Item = [int]$saved.item
    }
}

# Finds bake3 and sets up the compiler environment, once, before the first build: the
# menu needs neither, so it comes up without waiting for them.
$Bake = $null
function Initialize-BuildEnvironment {
    if ($script:Bake) { return }
    $script:Bake = Find-Bake
    Import-MsvcEnvironment
    Set-LibclangPath
}

$total = [Diagnostics.Stopwatch]::StartNew()

if ($Test) {
    exit (Complete-Run (Invoke-Tests (Find-TestEntry @(Get-TestEntries) $Test) $Build) $total)
}

if ($Scene) {
    exit (Complete-Run (Invoke-Box (Get-SceneScripts $Scene) $Build) $total)
}

$interactive = -not [Console]::IsInputRedirected -and $Host.Name -eq 'ConsoleHost'
if ($NoMenu) {
    exit (Complete-Run (Invoke-Box @() $Build) $total)
}

$scenes = @(Get-Scenes)
if (-not $scenes.Count) { throw "launch_box: no .flecs scenes found under $Root" }

if (-not $interactive) {
    $pick = Read-SceneNumber $scenes
    if (-not $pick) { exit 0 }
    $total.Restart()
    exit (Complete-Run (Invoke-Box (Get-SceneScripts $pick.Path) $Build) $total)
}

$state = [pscustomobject]@{ Build = $Build; Gpu = $Gpu; Scene = $null; Tests = $null; Item = $MenuScene }
Restore-MenuState $state $PSBoundParameters.ContainsKey('Build') $PSBoundParameters.ContainsKey('Gpu')
while ($true) {
    # Rebuilt every time: the failed entry changes with each test run, and F5 in the
    # menu may have refreshed the scenes (read back from the cache here).
    $scenes = @(Get-Scenes)
    if ($state.Scene) {
        $rel = $state.Scene.Rel
        $state.Scene = $scenes | Where-Object { $_.Rel -eq $rel } | Select-Object -First 1
    }
    $testEntries = @(Get-TestEntries)
    $group = if ($state.Tests) { $state.Tests.Group } else { 'all' }
    $state.Tests = $testEntries | Where-Object {
        $_.Group -eq $group -and ($group -ne 'test' -or $_.Name -eq $state.Tests.Name)
    } | Select-Object -First 1
    if (-not $state.Tests) { $state.Tests = $testEntries[0] }

    $action = Show-MainMenu $state $scenes $testEntries
    Save-MenuState $state
    if (-not $action) { break }
    $total.Restart()
    try {
        if ($action -eq 'Test') {
            $code = Invoke-Tests $state.Tests $state.Build
        } else {
            $scripts = @()
            $label = 'default scene'
            if ($state.Scene) {
                $scripts = Get-SceneScripts $state.Scene.Path
                $label = $state.Scene.Rel
            }
            Write-Host "launch_box: launching $label" -ForegroundColor Cyan
            $code = Invoke-Box $scripts $state.Build $state.Gpu
        }
    } catch {
        # Keep the menu alive: a missing bake3 or scene is reported, not fatal.
        Write-Host $_.Exception.Message -ForegroundColor Red
        $code = 1
    }
    [void](Complete-Run $code $total)
    Write-Host 'Press any key to return to the menu...' -ForegroundColor DarkGray
    [void][Console]::ReadKey($true)
}
