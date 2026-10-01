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
      Scene      opens the scene selector: an entry for box_app's default scene,
                 then a tree of the scene groups (app, visual, command, bench) and
                 their scenes, * marking the selected one. Up/Down/PgUp/PgDn/Home/End
                 move, Right/Left (or Enter on a group) expand and collapse a group,
                 typing filters by words and shows every match expanded, Backspace
                 edits the filter, Enter picks, Esc goes back. Until a scene is
                 picked box_app runs its default scene.
      Run        builds and runs box_app with the selected scene.
      Tests      opens the tests selector: an entry that runs every project under
                 box's test/ (the default), a 'failed' entry that reruns only what
                 failed last time (see -Test), then a tree of the repositories with
                 tests (box, and flecs and bake3 next to it, see below), their test
                 projects, suites and cases. Each case shows the status of its last
                 run (pass, fail, error, timeout, empty, skip, broken or untested)
                 and each node the counts below it, colored red when anything
                 failed, green when all passed, gray when nothing ran.
                   Right/Left  expand and collapse a node; Left on a case, suite or
                               project goes to its parent.
                   Tab         checks or unchecks every case under the highlighted
                               row and moves down (Shift+Tab moves up); [x] marks a
                               node with all cases checked, [~] one with some.
                   Ctrl+A      checks or unchecks every shown case.
                   typing      filters: the tree shows, expanded, the cases that
                               contain each word of the filter in their status,
                               project or Suite.case, so 'fail render' leaves the
                               failing cases of render, and a first 'filter' entry
                               runs all of them (see -TestFilter).
                   Enter       runs the highlighted 'filter', 'all' or 'failed'
                               entry; on the tree, the checked cases when there are
                               any, else the highlighted project, suite or case.
                 Checks and expanded nodes stay while the launcher runs.
      Run tests  builds and runs the selected tests again.

    When box_app or the tests finish the menu comes back with the same settings, and the
    next start of the script does too: the choices are saved in .launch_box/menu.json
    whenever the menu is left. -Build on the command line wins over the saved build mode.

    The scene list is cached in .launch_box/scenes.tsv and read again when a scene
    folder changes. F5, in the menu or the scene selector, rescans the folders anyway.

    Tests come from three repositories:
      box    the bake test projects under test/, by their names (core, render, ...).
      flecs  the bake test projects under ../flecs/test, named flecs/<name>. They are
             built and run in the local bake environment of that repository
             (../flecs/.bake/local_env, bake3 --local-env), so the flecs package they
             build stays out of the environment box builds in.
      bake3  the Python unittest suite ../bake3/run_tests.py, named bake3/run_tests,
             with its classes as suites. It runs with python through a runner the
             launcher writes to .launch_box/run_unittest.py; its setUpClass builds
             bake3 with make, so it needs make and a C compiler on the path.
    A repository that is not there is left out.

    The test cases are read from the testsuites of each project.json, and from the
    source of run_tests.py, so listing them needs no build, and cached in
    .launch_box/tests.tsv until one of those files changes; F5 in the tests selector
    reads them again. The status of each case comes from the reports every test run
    keeps (see -Test).

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
    Run unit tests instead of box_app, skipping the menu: the name of a test project
    (core, render, flecs/query, bake3/run_tests, ...), 'all' (every project of box) or
    'failed'. Each bake project runs with bake3 run test/<name> -- -j <Jobs>, after
    the clean builds -Build asks for (Box rebuilds box, or the flecs test projects in
    their environment, and the selected test projects). The exit code is non zero
    when anything fails.

    Every run keeps each project's --json report in .launch_box/tests-<Cfg>.
    'failed' reruns what those reports recorded as failed: each failed case on its own
    (bake3 run test/<name> -- Suite.case), and in full each project that left no
    report because it did not build or crashed. Cases that pass are cleared, so running
    'failed' again only repeats what still fails. A parameterized case reruns with its
    default parameters.

    Running single cases or suites, from the selector or with -TestFilter, updates the
    statuses of those cases in the saved reports instead of replacing them.

.PARAMETER TestFilter
    Run the test cases that match this filter, skipping the menu: every word must occur
    in the status, project name or Suite.case id of a case, ignoring case, as in the
    tests selector ('render Lights', 'fail', 'core Args.fps'). A suite whose cases all
    match runs at once with -j <Jobs>; other cases run one by one.

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
.EXAMPLE
    ./launch_box.ps1 -TestFilter 'render Lights'
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
    [string]$TestFilter,
    [ValidateRange(1, 256)]
    [int]$Jobs = 8,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$BoxArgs = @()
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$BakeSource = Join-Path (Split-Path $Root) 'bake3'
$FlecsSource = Join-Path (Split-Path $Root) 'flecs'
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

# Every test run keeps the report of each project here, per configuration, so the failed
# entry and the case statuses know what happened last: tests-<Cfg>/<project>.json, where
# the project name of another repository makes a subfolder (flecs/core.json). A project
# that produced no report (it did not build, or crashed) gets {"broken": true} instead.
function Get-TestStateDir { Get-LauncherPath "tests-$Cfg" }

function Get-TestReportPath([string]$Name) { Join-Path (Get-TestStateDir) "$Name.json" }

# Statuses a report gives a case, from best to worst; a parameterized case shows the worst
# of its runs. The selector also labels cases 'broken' (their project left no report) and
# 'untested' (no report has them), and colors every status.
$StatusRank = @{ pass = 0; skip = 1; quarantined = 1; empty = 2; timeout = 3; error = 4; fail = 5 }
$StatusColors = @{
    pass = 'Green'; skip = 'Yellow'; quarantined = 'Yellow'; empty = 'Yellow'
    timeout = 'Red'; error = 'Red'; fail = 'Red'; broken = 'Red'; untested = 'DarkGray'
}
$FailedStatuses = @('fail', 'error', 'timeout')

# Repositories whose tests the launcher lists. box and flecs hold bake test projects under
# test/; flecs builds them in its own local bake environment (flecs/.bake/local_env), so
# the flecs package it registers stays out of the environment box builds in. bake3 holds
# a Python unittest module, whose classes are the suites and test methods the cases. The
# projects of box keep their bare names; the others carry the repository name in front.
$TestRepos = @(
    [pscustomobject]@{ Name = 'box'; Dir = $Root; Prefix = ''; Type = 'bake'; BakeArgs = @() }
    [pscustomobject]@{ Name = 'flecs'; Dir = $FlecsSource; Prefix = 'flecs/'; Type = 'bake'; BakeArgs = @('--local-env') }
    [pscustomobject]@{ Name = 'bake3'; Dir = $BakeSource; Prefix = 'bake3/'; Type = 'unittest'; BakeArgs = @() }
)

# Test project $Name: its repository, its name there (Local), its folder and the file that
# declares its cases (project.json, or the module of a unittest project).
function Get-TestProject([string]$Name) {
    $repo = $TestRepos | Where-Object { $_.Prefix -and $Name.StartsWith($_.Prefix) } | Select-Object -First 1
    if (-not $repo) { $repo = $TestRepos[0] }
    $local = $Name.Substring($repo.Prefix.Length)
    if ($repo.Type -eq 'unittest') {
        $dir = $repo.Dir
        $decl = Join-Path $dir "$local.py"
    } else {
        $dir = Join-Path $repo.Dir "test/$local"
        $decl = Join-Path $dir 'project.json'
    }
    [pscustomobject]@{ Name = $Name; Repo = $repo; Local = $local; Dir = $dir; Decl = $decl }
}

# Names of the test projects of every repository that is there: the bake projects
# directly under its test/ folder, sorted, or run_tests for bake3's run_tests.py.
function Get-TestProjectNames {
    foreach ($repo in $TestRepos) {
        if ($repo.Type -eq 'unittest') {
            if (Test-Path (Join-Path $repo.Dir 'run_tests.py')) { "$($repo.Prefix)run_tests" }
            continue
        }
        $dir = Join-Path $repo.Dir 'test'
        if (-not (Test-Path $dir)) { continue }
        Get-ChildItem $dir -Directory |
            Where-Object { Test-Path (Join-Path $_.FullName 'project.json') } |
            Sort-Object Name |
            ForEach-Object { "$($repo.Prefix)$($_.Name)" }
    }
}

# The cases of unittest module $Path, as Suite and Case: the test_ methods of each class
# that derives from a TestCase, read from the source without importing it.
function Read-UnittestCases([string]$Path) {
    $suite = $null
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        if ($line -match '^class\s+(\w+)\s*\((.*)\)\s*:') {
            $name = $Matches[1]
            $suite = if ($Matches[2] -match 'TestCase') { $name } else { $null }
        } elseif ($line -match '^\S') {
            if ($line -notmatch '^(#|@)') { $suite = $null }
        } elseif ($suite -and $line -match '^\s+def\s+(test\w*)\s*\(') {
            [pscustomobject]@{ Suite = $suite; Case = $Matches[1] }
        }
    }
}

# The cache key of the case list: a version of the way the cases are read, then every test
# project with the modification time of the file that declares its cases.
function Get-TestCasesKey {
    'v2;' + (@(Get-TestProjectNames) | ForEach-Object {
        "$_=$([IO.File]::GetLastWriteTimeUtc((Get-TestProject $_).Decl).Ticks)"
    }) -join ';'
}

# Every test case the test projects declare, as Project, Suite and Case, from
# .launch_box/tests.tsv while it is current. The cases come from the testsuites of each
# project.json and from the source of unittest modules, so no build is needed, and
# Get-TestCasesKey is the cache key. With -Refresh they are read again anyway.
function Get-TestCases([switch]$Refresh) {
    $cache = Get-LauncherPath 'tests.tsv'
    $key = Get-TestCasesKey

    if (-not $Refresh -and (Test-Path $cache)) {
        $lines = [IO.File]::ReadAllLines($cache)
        if ($lines.Count -and $lines[0] -eq $key) {
            for ($i = 1; $i -lt $lines.Count; $i++) {
                $parts = $lines[$i].Split("`t")
                [pscustomobject]@{ Project = $parts[0]; Suite = $parts[1]; Case = $parts[2] }
            }
            return
        }
    }

    $cases = @(foreach ($n in Get-TestProjectNames) {
        $p = Get-TestProject $n
        if ($p.Repo.Type -eq 'unittest') {
            foreach ($c in Read-UnittestCases $p.Decl) { [pscustomobject]@{ Project = $n; Suite = $c.Suite; Case = $c.Case } }
            continue
        }
        $project = Get-Content $p.Decl -Raw | ConvertFrom-Json
        foreach ($s in @($project.test.testsuites)) {
            if (-not $s) { continue }
            foreach ($c in @($s.testcases)) {
                if ($c) { [pscustomobject]@{ Project = $n; Suite = [string]$s.id; Case = [string]$c } }
            }
        }
    })
    [IO.File]::WriteAllLines($cache, [string[]](@($key) + ($cases | ForEach-Object { "$($_.Project)`t$($_.Suite)`t$($_.Case)" })))
    $cases
}

# The saved reports of the test projects that still exist, by project name in name
# order. A report that cannot be read counts as broken.
function Get-TestReports {
    $reports = [ordered]@{}
    $dir = Get-TestStateDir
    if (-not (Test-Path $dir)) { return $reports }
    $base = (Get-Item $dir).FullName.TrimEnd('\', '/').Length + 1
    $files = Get-ChildItem $dir -Recurse -Filter *.json -File | ForEach-Object {
        [pscustomobject]@{ Name = (ConvertTo-ForwardSlash $_.FullName.Substring($base)) -replace '\.json$', ''; File = $_.FullName }
    }
    foreach ($f in $files | Sort-Object Name) {
        if (-not (Test-Path (Get-TestProject $f.Name).Decl)) { continue }
        try {
            $reports[$f.Name] = Get-Content $f.File -Raw | ConvertFrom-Json
        } catch {
            $reports[$f.Name] = [pscustomobject]@{ broken = $true }
        }
    }
    $reports
}

# Failures recorded in $Reports: an ordered map from project name to its failed Suite.case
# ids, or to $null when the whole project has to run again.
function Get-FailedTests($Reports) {
    $failed = [ordered]@{}
    foreach ($name in $Reports.Keys) {
        $report = $Reports[$name]
        if ($report.broken) { $failed[$name] = $null; continue }
        $cases = @($report.tests |
            Where-Object { $_.status -in $FailedStatuses } |
            ForEach-Object { "$($_.suite).$($_.case)" } |
            Select-Object -Unique)
        if ($cases.Count) { $failed[$name] = $cases }
    }
    $failed
}

# Status of every case in $Reports, keyed 'project Suite.case', and 'broken' keyed by the
# name of each project whose last run left no report.
function Get-TestCaseStates($Reports) {
    $states = @{}
    foreach ($name in $Reports.Keys) {
        $report = $Reports[$name]
        if ($report.broken) { $states[$name] = 'broken'; continue }
        foreach ($t in @($report.tests)) {
            $key = "$name $($t.suite).$($t.case)"
            $status = [string]$t.status
            $old = $states[$key]
            if (-not $old -or $StatusRank[$status] -gt $StatusRank[$old]) { $states[$key] = $status }
        }
    }
    $states
}

# Replaces the runs of the cases in $Runs (report entries with suite, case and status) in
# the saved report of project $Name, starting a report when there is none. A report marked
# broken stays as it is: that project has to run in full again.
function Update-TestReport([string]$Name, $Runs) {
    $path = Get-TestReportPath $Name
    $report = $null
    if (Test-Path $path) {
        try { $report = Get-Content $path -Raw | ConvertFrom-Json } catch { }
    }
    if ($report -and $report.broken) { return }
    if (-not $report -or -not $report.PSObject.Properties['tests']) {
        $report = [pscustomobject]@{ project = $Name; tests = @() }
    }
    $ids = @{}
    foreach ($r in $Runs) { $ids["$($r.suite).$($r.case)"] = $true }
    $report.tests = @(@($report.tests | Where-Object { -not $ids["$($_.suite).$($_.case)"] }) + @($Runs))
    New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
    $report | ConvertTo-Json -Depth 5 | Write-Utf8File $path
}

# The command that runs test project $Name with $Argv, for the status lines.
function Get-TestCommand([string]$Name, [string[]]$Argv) {
    $p = Get-TestProject $Name
    $where = if ($p.Repo.Name -ne 'box') { "  (in ../$($p.Repo.Name))" } else { '' }
    if ($p.Repo.Type -eq 'unittest') { return "python $($p.Local).py $($Argv -join ' ')$where" }
    $local = if ($p.Repo.BakeArgs) { ' ' + ($p.Repo.BakeArgs -join ' ') } else { '' }
    "bake3 run test/$($p.Local)$local -- $($Argv -join ' ')$where"
}

# The entries of $List whose Text (or else group and name) contains every word of
# $Filter, ignoring case.
function Select-PickEntries($List, [string]$Filter) {
    $words = @($Filter.ToLowerInvariant() -split '\s+' | Where-Object { $_ })
    if (-not $words.Count) { return $List }
    foreach ($e in $List) {
        $text = $e.Text
        if (-not $text) { $text = "$($e.Group) $($e.Name)".ToLowerInvariant() }
        $match = $true
        foreach ($w in $words) { if (-not $text.Contains($w)) { $match = $false; break } }
        if ($match) { $e }
    }
}

# Tests entry that runs the case entries $Cases, or $null when there are none. Cases holds
# the selection: an ordered map from project name to its Suite.case ids, or to $null when
# every case of the project is selected (the project then runs in full). Unless given,
# Key lists the selection compactly for menu.json: 'project', 'project Suite' or 'project
# Suite.case' items separated by '|'.
function New-SelectionEntry($Cases, [string]$Kind = 'selection', [string]$Key = '', [string]$Name = '') {
    $counts = $script:caseCounts
    $groups = [ordered]@{}
    foreach ($c in $Cases) {
        if (-not $groups.Contains($c.Project)) { $groups[$c.Project] = [ordered]@{} }
        $suites = $groups[$c.Project]
        if (-not $suites.Contains($c.Suite)) { $suites[$c.Suite] = [Collections.Generic.List[object]]::new() }
        $suites[$c.Suite].Add($c)
    }
    if (-not $groups.Count) { return $null }

    $selection = [ordered]@{}
    $items = [Collections.Generic.List[string]]::new()
    $count = 0
    foreach ($p in $groups.Keys) {
        $ids = [Collections.Generic.List[string]]::new()
        $parts = [Collections.Generic.List[string]]::new()
        foreach ($s in $groups[$p].Keys) {
            $picked = $groups[$p][$s]
            if ($picked.Count -ge $counts["$p $s"]) { $parts.Add("$p $s") } else { foreach ($c in $picked) { $parts.Add($c.Key) } }
            foreach ($c in $picked) { $ids.Add($c.CaseId) }
        }
        $count += $ids.Count
        if ($ids.Count -ge $counts[$p]) { $selection[$p] = $null; $items.Add($p) } else { $selection[$p] = $ids; $items.AddRange($parts) }
    }

    $projects = @($selection.Keys)
    if (-not $Key) { $Key = $items -join '|' }
    if (-not $Name) { $Name = "$count checked case(s) in $($projects.Count) project(s)" }
    $shown = if ($projects.Count -le 4) { $projects -join ', ' } else { "$(($projects[0..3]) -join ', '), ..." }
    [pscustomobject]@{ Kind = $Kind; Key = $Key; Group = $(if ($Kind -eq 'filter') { 'filter' } else { 'selected' })
        Name = $Name; Rel = "runs $count case(s) of $shown"; Projects = $projects; Cases = $selection; Color = 'Yellow' }
}

# The case entries a selection Key of New-SelectionEntry names.
function Expand-SelectionKey([string]$Key) {
    $items = [Collections.Generic.HashSet[string]]::new([string[]]@($Key.Split('|')))
    foreach ($c in $script:caseEntries) {
        if ($items.Contains($c.Project) -or $items.Contains("$($c.Project) $($c.Suite)") -or $items.Contains($c.Key)) { $c }
    }
}

# Tests entry that runs every case filter $Filter leaves, or $null when fewer than $Min
# cases are left. $Cases, when given, are those cases already.
function New-FilterEntry([string]$Filter, [int]$Min = 2, $Cases = $null) {
    if ($null -eq $Cases) { $Cases = @(Select-PickEntries $script:caseEntries $Filter) }
    if (@($Cases).Count -lt [Math]::Max(1, $Min)) { return $null }
    New-SelectionEntry $Cases 'filter' $Filter "$(@($Cases).Count) case(s) matching '$Filter'"
}

# One entry per test case, labeled with its status, kept between calls: the entries are
# made again only when the case list changes, and their statuses (Group, Color, and Text,
# what the filter searches) are brought up to date from $States on every call. Also keeps
# the entries by Key and the number of cases of every project and 'project Suite'.
$caseEntries = @()
$caseEntriesKey = $null
$caseByKey = @{}
$caseCounts = @{}
function Update-CaseEntries($States, [switch]$Refresh) {
    $key = Get-TestCasesKey
    if ($Refresh -or $key -ne $script:caseEntriesKey) {
        $cases = @(Get-TestCases -Refresh:$Refresh)
        $width = (@($cases | ForEach-Object { $_.Project } | Select-Object -Unique) |
            Measure-Object -Property Length -Maximum).Maximum
        $byKey = @{}
        $counts = @{}
        $script:caseEntries = @(foreach ($c in $cases) {
            $id = "$($c.Suite).$($c.Case)"
            $entry = [pscustomobject]@{ Kind = 'case'; Key = "$($c.Project) $id"; Group = ''; Text = ''
                Name = $c.Project.PadRight($width) + ' ' + $id; Project = $c.Project; Suite = $c.Suite
                CaseId = $id; Color = $null }
            $byKey[$entry.Key] = $entry
            $counts[$c.Project]++
            $counts["$($c.Project) $($c.Suite)"]++
            $entry
        })
        $script:caseByKey = $byKey
        $script:caseCounts = $counts
        $script:caseEntriesKey = $key
    }
    foreach ($e in $script:caseEntries) {
        $status = $States[$e.Project]
        if (-not $status) { $status = $States[$e.Key] }
        if (-not $status) { $status = 'untested' }
        if ($e.Group -ne $status) {
            $e.Group = $status
            $e.Color = $StatusColors[$status]
            $e.Text = "$status $($e.Name)".ToLowerInvariant()
        }
    }
}

# Test selector entries: 'all' (every project of box), 'failed', then every test project
# of every repository, in the shape the selector expects; Kind and Key identify an entry
# across lists. The case entries are brought up to date on the side (Update-CaseEntries)
# and kept in $caseEntries. With -Refresh the cases are read from their files again.
function Get-TestEntries([switch]$Refresh) {
    $names = @(Get-TestProjectNames)
    $boxNames = @($names | Where-Object { (Get-TestProject $_).Repo.Name -eq 'box' })
    [pscustomobject]@{ Kind = 'all'; Key = 'all'; Group = 'all'; Name = "every box project ($($boxNames.Count))"
        Rel = "bake3 run test/<project> -- -j $Jobs, for every project of box"; Projects = $boxNames }

    $reports = Get-TestReports
    $failed = Get-FailedTests $reports
    $cases = 0
    $whole = 0
    foreach ($k in $failed.Keys) { if ($null -eq $failed[$k]) { $whole++ } else { $cases += $failed[$k].Count } }
    $parts = @()
    if ($cases) { $parts += "$cases case(s)" }
    if ($whole) { $parts += "$whole whole project(s)" }
    $summary = if ($parts) { ($parts -join ' + ') + " in $($failed.Count) project(s)" } else { 'none recorded' }
    [pscustomobject]@{ Kind = 'failed'; Key = 'failed'; Group = 'failed'; Name = $summary
        Rel = 'reruns the failures of the last runs'; Projects = @($failed.Keys); Cases = $failed }

    foreach ($n in $names) {
        [pscustomobject]@{ Kind = 'test'; Key = $n; Group = 'test'; Name = $n
            Rel = Get-TestCommand $n @('-j', "$Jobs"); Projects = @($n) }
    }

    Update-CaseEntries (Get-TestCaseStates $reports) -Refresh:$Refresh
}

# What entry $Entry runs, for the status lines.
function Get-TestEntryHint($Entry) {
    if ($Entry.Kind -eq 'case') { return Get-TestCommand $Entry.Project @($Entry.CaseId) }
    $Entry.Rel
}

# The projects entry $Entry runs tests of.
function Get-TestEntryProjects($Entry) {
    if ($Entry.Kind -eq 'case') { return @($Entry.Project) }
    @($Entry.Projects)
}

# The selection of test cases entry $Entry runs, as New-SelectionEntry describes it.
function Get-TestSelection($Entry) {
    if ($Entry.Kind -eq 'case') { return [ordered]@{ $Entry.Project = @($Entry.CaseId) } }
    $Entry.Cases
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

# Index of the entry of $List that stands for $Entry, or -1: the same Kind and Key for
# test entries (their status label changes between runs), else the same Path and Group.
function Find-EntryIndex($List, $Entry) {
    if ($Entry) {
        $byKey = [bool]$Entry.PSObject.Properties['Key']
        for ($i = 0; $i -lt $List.Count; $i++) {
            if ($byKey) {
                if ($List[$i].Kind -eq $Entry.Kind -and $List[$i].Key -eq $Entry.Key) { return $i }
            } elseif ($List[$i].Path -eq $Entry.Path -and $List[$i].Group -eq $Entry.Group) {
                return $i
            }
        }
    }
    -1
}

# Rows the scene tree shows: the default scene entry, then each scene group with its
# scenes when it is expanded. With $Visible, the scenes the filter leaves, only groups
# with such scenes show, all expanded. Every row has an Id, a Level, a Type (default,
# group or scene) and its Node: the scene, or for a group its Name, Dir and Scenes.
function Get-SceneTreeRows($Scenes, $Visible, $Expanded) {
    $rows = [Collections.Generic.List[object]]::new()
    $rows.Add([pscustomobject]@{ Id = 'default'; Level = 0; Type = 'default'; Node = $null })
    foreach ($g in $SceneGroups) {
        $all = @($Scenes | Where-Object { $_.Group -eq $g.Name })
        $shown = if ($null -ne $Visible) { @($all | Where-Object { $Visible.Contains($_.Rel) }) } else { $all }
        if (-not $shown.Count) { continue }
        $node = [pscustomobject]@{ Name = $g.Name; Dir = $g.Dir; Scenes = $all; Shown = $shown.Count }
        $rows.Add([pscustomobject]@{ Id = "group $($g.Name)"; Level = 0; Type = 'group'; Node = $node })
        if ($null -eq $Visible -and -not $Expanded.Contains($g.Name)) { continue }
        foreach ($s in $shown) {
            $rows.Add([pscustomobject]@{ Id = "scene $($s.Rel)"; Level = 1; Type = 'scene'; Node = $s })
        }
    }
    $rows
}

# Scene selector, drawn inside the main menu: an entry for box_app's default scene, then
# the scene groups (app, visual, command, bench) with their scenes; * marks $Selected.
# Left/Right collapse and expand a group, Enter on a group does either, typing filters
# (the tree then shows every match expanded), F5 rescans the scene folders. Expanded
# groups are kept in $Expanded. Returns the picked scene, $DefaultScene for the default
# scene, or $null on Esc.
$DefaultScene = [pscustomobject]@{ Default = $true }
function Show-SceneTree($Scenes, $Selected, $Expanded) {
    $filter = ''
    $offset = 0
    $index = 0
    $note = ''
    $dirty = $true
    $follow = $true
    $rows = $null
    $keepId = if ($Selected) { "scene $($Selected.Rel)" } else { 'default' }
    if ($Selected) { [void]$Expanded.Add($Selected.Group) } elseif (-not $Expanded.Count) { [void]$Expanded.Add('app') }

    Clear-Host
    try {
        while ($true) {
            if ($dirty) {
                # The highlighted row stays, except after a filter edit: then the first
                # match is highlighted.
                if ($follow -and $rows -and $index -lt $rows.Count) { $keepId = $rows[$index].Id }
                $visible = $null
                if ($filter.Trim()) {
                    $visible = [Collections.Generic.HashSet[string]]::new()
                    foreach ($s in Select-PickEntries $Scenes $filter) { [void]$visible.Add($s.Rel) }
                }
                $rows = Get-SceneTreeRows $Scenes $visible $Expanded
                if (-not $follow) {
                    $index = 0
                    for ($i = 0; $i -lt $rows.Count; $i++) { if ($rows[$i].Type -eq 'scene') { $index = $i; break } }
                } elseif ($keepId) {
                    for ($i = 0; $i -lt $rows.Count; $i++) { if ($rows[$i].Id -eq $keepId) { $index = $i; break } }
                }
                $keepId = $null
                $follow = $true
                $dirty = $false
            }
            if ($index -ge $rows.Count) { $index = [Math]::Max(0, $rows.Count - 1) }

            $height = [Math]::Max(3, [Console]::WindowHeight - 6)
            if ($index -lt $offset) { $offset = $index }
            if ($index -ge $offset + $height) { $offset = $index - $height + 1 }
            $offset = [Math]::Max(0, [Math]::Min($offset, [Math]::Max(0, $rows.Count - $height)))

            [Console]::SetCursorPosition(0, 0)
            Write-Line 'select scene  -  Up/Down move, Left/Right fold, type to filter, Enter pick, F5 refresh, Esc back' Cyan
            Write-Line ("filter: " + $filter + "_") Yellow
            Write-Line ''
            for ($r = 0; $r -lt $height; $r++) {
                $i = $offset + $r
                if ($i -ge $rows.Count) { Write-Line ''; continue }
                $row = $rows[$i]
                $label = switch ($row.Type) {
                    'default' { '{0} (default scene)' -f $(if (-not $Selected) { '*' } else { ' ' }) }
                    'group' {
                        $sign = if ($null -ne $visible -or $Expanded.Contains($row.Node.Name)) { '-' } else { '+' }
                        $count = if ($row.Node.Shown -ne $row.Node.Scenes.Count) { "$($row.Node.Shown) of $($row.Node.Scenes.Count)" } else { "$($row.Node.Scenes.Count)" }
                        '  {0} {1,-8} {2} scene(s)' -f $sign, $row.Node.Name, $count
                    }
                    'scene' { '{0}     {1}' -f $(if ($Selected -and $Selected.Rel -eq $row.Node.Rel) { '*' } else { ' ' }), $row.Node.Name }
                }
                if ($i -eq $index) {
                    Write-Line ("> " + $label) Black Cyan
                } elseif ($row.Type -eq 'group') {
                    Write-Line ("  " + $label) Yellow
                } else {
                    Write-Line ("  " + $label)
                }
            }
            $row = if ($rows.Count) { $rows[$index] } else { $null }
            $status = switch ($row.Type) {
                'default' { 'box_app runs its default scene' }
                'group'   { "$($row.Node.Dir)/" }
                'scene'   { $row.Node.Rel }
                default   { 'no match' }
            }
            if ($note) { $status = "$note  -  $status"; $note = '' }
            Write-Line ''
            Write-Line $status DarkGray

            $key = Read-MenuKey
            switch ($key.Key) {
                'F5' {
                    $Scenes = @(Get-Scenes -Refresh)
                    $note = "refreshed: $($Scenes.Count) scenes"
                    $dirty = $true
                }
                'UpArrow'   { if ($index -gt 0) { $index-- } }
                'DownArrow' { if ($index -lt $rows.Count - 1) { $index++ } }
                'PageUp'    { $index = [Math]::Max(0, $index - $height) }
                'PageDown'  { $index = [Math]::Max(0, [Math]::Min($rows.Count - 1, $index + $height)) }
                'Home'      { $index = 0 }
                'End'       { $index = [Math]::Max(0, $rows.Count - 1) }
                'Escape'    { return $null }
                'RightArrow' {
                    if ($row.Type -eq 'group') {
                        if ($null -eq $visible -and $Expanded.Add($row.Node.Name)) { $dirty = $true }
                        elseif ($index -lt $rows.Count - 1) { $index++ }
                    }
                }
                'LeftArrow' {
                    if ($row.Type -eq 'group' -and $null -eq $visible -and $Expanded.Remove($row.Node.Name)) {
                        $dirty = $true
                    } elseif ($row.Type -eq 'scene') {
                        for ($j = $index - 1; $j -ge 0; $j--) { if ($rows[$j].Type -eq 'group') { $index = $j; break } }
                    }
                }
                'Enter' {
                    switch ($row.Type) {
                        'default' { return $DefaultScene }
                        'scene'   { return $row.Node }
                        'group' {
                            if ($null -eq $visible) {
                                if (-not $Expanded.Remove($row.Node.Name)) { [void]$Expanded.Add($row.Node.Name) }
                                $dirty = $true
                            }
                        }
                    }
                }
                'Backspace' {
                    if ($filter.Length) { $filter = $filter.Substring(0, $filter.Length - 1); $dirty = $true; $follow = $false }
                }
                default {
                    if (-not [char]::IsControl($key.KeyChar)) { $filter += $key.KeyChar; $dirty = $true; $follow = $false }
                }
            }
        }
    } finally {
        Clear-Host
    }
}

# Color of a tree node from the statuses of its cases: red when any failed, green when
# all passed, dark gray when none ran, else the default.
function Get-StatusColor($Counts, [int]$Total) {
    foreach ($s in @('fail', 'error', 'timeout', 'broken')) { if ($Counts[$s]) { return 'Red' } }
    if ($Counts['pass'] -eq $Total) { return 'Green' }
    if ($Counts['untested'] -eq $Total) { return 'DarkGray' }
    $null
}

# The tests tree: a node per repository with test projects, holding a node per project
# (Entry is its project entry), holding a node per suite. Every node has Kind, Key (the
# key in $State.Expanded), Name, its child nodes in Children, every case entry under it
# in Cases, and a Summary and Color from the statuses of those cases.
function New-TestTree($Entries) {
    $repos = [ordered]@{}
    $projects = @{}
    foreach ($e in $Entries) {
        if ($e.Kind -ne 'test') { continue }
        $repo = (Get-TestProject $e.Key).Repo
        if (-not $repos.Contains($repo.Name)) {
            $repos[$repo.Name] = [pscustomobject]@{ Kind = 'repo'; Key = "@$($repo.Name)"; Name = $repo.Name
                Children = [ordered]@{}; Cases = [Collections.Generic.List[object]]::new(); Counts = @{} }
        }
        $node = [pscustomobject]@{ Kind = 'test'; Key = $e.Key; Name = $e.Key.Substring($repo.Prefix.Length)
            Entry = $e; Children = [ordered]@{}; Cases = [Collections.Generic.List[object]]::new(); Counts = @{}
            Parent = $repos[$repo.Name] }
        $repos[$repo.Name].Children[$e.Key] = $node
        $projects[$e.Key] = $node
    }
    foreach ($c in $script:caseEntries) {
        $p = $projects[$c.Project]
        if (-not $p) { continue }
        $s = $p.Children[$c.Suite]
        if (-not $s) {
            $s = [pscustomobject]@{ Kind = 'suite'; Key = "$($c.Project) $($c.Suite)"; Name = $c.Suite
                Children = $null; Cases = [Collections.Generic.List[object]]::new(); Counts = @{} }
            $p.Children[$c.Suite] = $s
        }
        foreach ($n in @($s, $p, $p.Parent)) { $n.Cases.Add($c); $n.Counts[$c.Group]++ }
    }
    foreach ($r in $repos.Values) {
        foreach ($n in @($r) + @($r.Children.Values) + @(foreach ($p in $r.Children.Values) { $p.Children.Values })) {
            $parts = @(foreach ($s in @('fail', 'error', 'timeout', 'broken', 'empty', 'skip', 'quarantined', 'untested')) {
                if ($n.Counts[$s]) { "$($n.Counts[$s]) $s" }
            })
            if (-not $parts) { $parts = @('all pass') }
            $n | Add-Member Summary ($parts -join ', ')
            $n | Add-Member Color (Get-StatusColor $n.Counts $n.Cases.Count)
        }
    }
    @($repos.Values | Where-Object { $_.Children.Count })
}

# Rows the tests tree shows: $Specials first (the filter entry, 'all' and 'failed'), then
# each repository, its projects when it is expanded, their suites when the project is and
# the cases of a suite when that is. With $Visible, the set of case keys the filter
# leaves, only nodes with such cases show, all expanded. Every row has an Id, a Level, a
# Type, its Node and the visible case entries under it.
function Get-TestTreeRows($Specials, $Tree, $Visible, $Expanded) {
    $rows = [Collections.Generic.List[object]]::new()
    foreach ($s in $Specials) {
        $rows.Add([pscustomobject]@{ Id = "$($s.Kind) $($s.Key)"; Level = 0; Type = 'special'; Node = $s; Cases = @() })
    }
    $add = {
        param($Node, [int]$Level)
        $cases = if ($Visible) { @(foreach ($c in $Node.Cases) { if ($Visible.Contains($c.Key)) { $c } }) } else { @($Node.Cases) }
        if (-not $cases.Count) { return }
        $rows.Add([pscustomobject]@{ Id = "$($Node.Kind) $($Node.Key)"; Level = $Level; Type = $Node.Kind; Node = $Node; Cases = $cases })
        if (-not $Visible -and -not $Expanded.Contains($Node.Key)) { return }
        if ($Node.Kind -eq 'suite') {
            foreach ($c in $cases) {
                $rows.Add([pscustomobject]@{ Id = "case $($c.Key)"; Level = $Level + 1; Type = 'case'; Node = $c; Cases = @($c) })
            }
            return
        }
        foreach ($child in $Node.Children.Values) { & $add $child ($Level + 1) }
    }
    foreach ($r in $Tree) { & $add $r 0 }
    $rows
}

# Checks or unchecks the case entries $Cases in $Checks: unchecks them when all are
# checked, else checks them all.
function Switch-TestChecks($Checks, $Cases) {
    $all = $true
    foreach ($c in $Cases) { if (-not $Checks.Contains($c.Key)) { $all = $false; break } }
    foreach ($c in $Cases) { if ($all) { [void]$Checks.Remove($c.Key) } else { [void]$Checks.Add($c.Key) } }
}

# The text of a tests tree row: a check box ([x] all of its cases checked, [~] some),
# the indentation of its level, + or - for a collapsed or expanded node, then the name
# and the statuses of its cases. $Width pads project names.
function Format-TestTreeRow($Row, $Checks, $Expanded, [bool]$Filtering, [int]$Width) {
    $node = $Row.Node
    if ($Row.Type -eq 'special') { return '    {0,-8} {1}' -f $node.Group, $node.Name }
    $cases = if ($Row.Type -eq 'case') { @($Row.Cases) } else { $node.Cases }
    $checked = 0
    if ($Checks.Count) { foreach ($c in $cases) { if ($Checks.Contains($c.Key)) { $checked++ } } }
    $box = if (-not $checked) { '[ ]' } elseif ($checked -eq $cases.Count) { '[x]' } else { '[~]' }
    $indent = '  ' * $Row.Level
    if ($Row.Type -eq 'case') {
        return '{0} {1}  {2,-8} {3}' -f $box, $indent, $node.Group, $node.CaseId.Split('.', 2)[1]
    }
    $sign = if ($Filtering -or $Expanded.Contains($node.Key)) { '-' } else { '+' }
    $count = if ($Filtering -and $Row.Cases.Count -ne $node.Cases.Count) { "$($Row.Cases.Count) of $($node.Cases.Count)" } else { "$($node.Cases.Count)" }
    $name = if ($Row.Type -eq 'test') { $node.Name.PadRight($Width) } else { $node.Name }
    '{0} {1}{2} {3}  {4} case(s), {5}' -f $box, $indent, $sign, $name, $count, $node.Summary
}

# Tests selector: the filter entry while a filter is typed, 'all' and 'failed', then a
# tree of the repositories, their test projects, suites and cases with the statuses of
# their last runs. Left/Right collapse and expand, Tab checks the cases under the
# highlighted row, Ctrl+A every shown case; typing filters (the tree then shows every
# match expanded). Enter runs the highlighted special entry, else the checked cases when
# there are any, else the highlighted node or case. Checks and expanded nodes are kept in
# $State.Checks and $State.Expanded. Returns the entry to run, or $null on Esc.
function Show-TestTree($State, $Entries) {
    $filter = ''
    $offset = 0
    $index = 0
    $note = ''
    $reload = $false
    $dirty = $true
    $follow = $true
    $keepId = $null
    $rows = $null
    $tree = @(New-TestTree $Entries)
    $width = ($tree | ForEach-Object { $_.Children.Values } | ForEach-Object { $_.Name.Length } | Measure-Object -Maximum).Maximum

    # Opens with box expanded the first time, and on the entry that ran last, with the
    # nodes above a case expanded.
    if (-not $State.Expanded.Count) { [void]$State.Expanded.Add('@box') }
    if ($State.Tests) {
        $keepId = "$($State.Tests.Kind) $($State.Tests.Key)"
        if ($State.Tests.Kind -eq 'case') {
            [void]$State.Expanded.Add("@$((Get-TestProject $State.Tests.Project).Repo.Name)")
            [void]$State.Expanded.Add($State.Tests.Project)
            [void]$State.Expanded.Add("$($State.Tests.Project) $($State.Tests.Suite)")
        }
    }

    Clear-Host
    try {
        while ($true) {
            if ($reload) {
                $Entries = @(Get-TestEntries -Refresh)
                $tree = @(New-TestTree $Entries)
                $known = [Collections.Generic.HashSet[string]]::new([string[]]@($script:caseByKey.Keys))
                $State.Checks.IntersectWith($known)
                $note = "refreshed: $($script:caseEntries.Count) cases"
                $reload = $false
                $dirty = $true
            }
            if ($dirty) {
                # The highlighted row stays, except after a filter edit: then the first
                # row, the entry that runs every match, is highlighted.
                if ($follow -and $rows -and $index -lt $rows.Count) { $keepId = $rows[$index].Id }
                if (-not $follow) { $index = 0 }
                $follow = $true
                $words = $filter.Trim()
                $visible = $null
                $specials = @($Entries | Where-Object { $_.Kind -in 'all', 'failed' })
                if ($words) {
                    $matched = @(Select-PickEntries $script:caseEntries $words)
                    $visible = [Collections.Generic.HashSet[string]]::new()
                    foreach ($c in $matched) { [void]$visible.Add($c.Key) }
                    $filterEntry = New-FilterEntry $words 1 $matched
                    if ($filterEntry) { $specials = @($filterEntry) + $specials }
                }
                $rows = Get-TestTreeRows $specials $tree $visible $State.Expanded
                if ($keepId) {
                    for ($i = 0; $i -lt $rows.Count; $i++) { if ($rows[$i].Id -eq $keepId) { $index = $i; break } }
                    $keepId = $null
                }
                $dirty = $false
            }
            if ($index -ge $rows.Count) { $index = [Math]::Max(0, $rows.Count - 1) }

            $height = [Math]::Max(3, [Console]::WindowHeight - 6)
            if ($index -lt $offset) { $offset = $index }
            if ($index -ge $offset + $height) { $offset = $index - $height + 1 }
            $offset = [Math]::Max(0, [Math]::Min($offset, [Math]::Max(0, $rows.Count - $height)))

            [Console]::SetCursorPosition(0, 0)
            Write-Line 'select tests  -  Left/Right fold, Tab check, Ctrl+A check shown, type to filter, Enter run, F5 refresh, Esc back' Cyan
            Write-Line ("filter: " + $filter + "_") Yellow
            Write-Line ''
            for ($r = 0; $r -lt $height; $r++) {
                $i = $offset + $r
                if ($i -ge $rows.Count) { Write-Line ''; continue }
                $label = Format-TestTreeRow $rows[$i] $State.Checks $State.Expanded ([bool]$visible) $width
                if ($i -eq $index) {
                    Write-Line ("> " + $label) Black Cyan
                } elseif ($rows[$i].Node.Color) {
                    Write-Line ("  " + $label) ([ConsoleColor]$rows[$i].Node.Color)
                } else {
                    Write-Line ("  " + $label)
                }
            }
            $row = if ($rows.Count) { $rows[$index] } else { $null }
            $status = if (-not $row) {
                'no match'
            } elseif ($row.Type -eq 'special') {
                Get-TestEntryHint $row.Node
            } elseif ($State.Checks.Count) {
                "Enter runs the $($State.Checks.Count) checked case(s)"
            } elseif ($row.Type -eq 'case') {
                Get-TestEntryHint $row.Node
            } else {
                "Enter runs $($row.Cases.Count) case(s) of $($row.Node.Key.TrimStart('@'))"
            }
            if ($note) { $status = "$note  -  $status"; $note = '' }
            Write-Line ''
            Write-Line $status DarkGray

            $key = Read-MenuKey
            if ($key.Key -eq 'A' -and ($key.Modifiers -band [ConsoleModifiers]::Control)) {
                Switch-TestChecks $State.Checks @(foreach ($x in $rows) { if ($x.Type -eq 'repo') { $x.Cases } })
                continue
            }
            switch ($key.Key) {
                'F5'        { $reload = $true }
                'UpArrow'   { if ($index -gt 0) { $index-- } }
                'DownArrow' { if ($index -lt $rows.Count - 1) { $index++ } }
                'PageUp'    { $index = [Math]::Max(0, $index - $height) }
                'PageDown'  { $index = [Math]::Max(0, [Math]::Min($rows.Count - 1, $index + $height)) }
                'Home'      { $index = 0 }
                'End'       { $index = [Math]::Max(0, $rows.Count - 1) }
                'Escape'    { return $null }
                'RightArrow' {
                    if ($row -and $row.Type -in 'repo', 'test', 'suite') {
                        if (-not $visible -and $State.Expanded.Add($row.Node.Key)) { $dirty = $true }
                        elseif ($index -lt $rows.Count - 1) { $index++ }
                    }
                }
                'LeftArrow' {
                    if ($row -and $row.Type -in 'repo', 'test', 'suite' -and -not $visible -and $State.Expanded.Remove($row.Node.Key)) {
                        $dirty = $true
                    } elseif ($row -and $row.Level -gt 0) {
                        for ($j = $index - 1; $j -ge 0; $j--) {
                            if ($rows[$j].Type -ne 'special' -and $rows[$j].Level -lt $row.Level) { $index = $j; break }
                        }
                    }
                }
                'Tab' {
                    if ($row -and $row.Type -ne 'special') { Switch-TestChecks $State.Checks $row.Cases }
                    if ($key.Modifiers -band [ConsoleModifiers]::Shift) {
                        if ($index -gt 0) { $index-- }
                    } elseif ($index -lt $rows.Count - 1) {
                        $index++
                    }
                }
                'Enter' {
                    if (-not $row) { break }
                    if ($row.Type -eq 'special') { return $row.Node }
                    if ($State.Checks.Count) {
                        return New-SelectionEntry @(foreach ($c in $script:caseEntries) { if ($State.Checks.Contains($c.Key)) { $c } })
                    }
                    if ($row.Type -eq 'case') { return $row.Node }
                    if ($row.Type -eq 'test' -and -not $visible) { return $row.Node.Entry }
                    return New-SelectionEntry $row.Cases 'selection' '' "$($row.Cases.Count) case(s) of $($row.Node.Key.TrimStart('@'))"
                }
                'Backspace' {
                    if ($filter.Length) { $filter = $filter.Substring(0, $filter.Length - 1); $dirty = $true; $follow = $false }
                }
                default {
                    if (-not [char]::IsControl($key.KeyChar)) { $filter += $key.KeyChar; $dirty = $true; $follow = $false }
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
# returns 'Run' or 'Test' for the chosen action, or $null for Esc. Picking an entry in
# the Tests selector runs it right away.
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
                Get-TestEntryHint $State.Tests
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
                            $pick = Show-SceneTree $Scenes $State.Scene $State.SceneExpanded
                            # The selector may have refreshed the list; the cache now has it.
                            $Scenes = @(Get-Scenes)
                            if ($pick -eq $DefaultScene) {
                                $State.Scene = $null
                                $State.Item = $MenuRun
                            } elseif ($pick) {
                                $State.Scene = $pick
                                $State.Item = $MenuRun
                            } else {
                                . $resync
                            }
                        }
                        $MenuRun { return 'Run' }
                        $MenuTests {
                            $pick = Show-TestTree $State $TestEntries
                            # The selector may have refreshed the cases; the cache now has them.
                            $TestEntries = @(Get-TestEntries)
                            if ($pick) { $State.Tests = $pick; $State.Item = $MenuRunTests; return 'Test' }
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
function Invoke-Bake([string[]]$Argv, [switch]$Console, [string]$Dir = $Root) {
    Invoke-Native $Bake $Argv $Dir -Filter:(-not $Console)
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

# Runs bake3 in $Dir with filtered output and prints how long $What took. Returns the
# exit code.
function Invoke-BakeStep([string]$What, [string[]]$Argv, [string]$Dir = $Root) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $code = Invoke-Bake $Argv -Dir $Dir
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

# A bake3 build of $Target in repository folder $Dir, with $BakeArgs added to the command
# and named $Label in the output.
function New-BuildStep([string]$Dir, [string]$Target, [string[]]$BakeArgs = @(), [string]$Label = $Target) {
    [pscustomobject]@{ Dir = $Dir; Target = $Target; Args = @($BakeArgs); Label = $Label }
}

# The bake3 builds the test projects $Projects need: one for those of box (the project, or
# every project under test/, which builds box once), and one per project of another
# repository, built in that repository with its arguments. Unittest projects need none.
function Get-TestBuildSteps([string[]]$Projects) {
    $box = @()
    $others = @()
    foreach ($name in $Projects) {
        $p = Get-TestProject $name
        if ($p.Repo.Type -ne 'bake') { continue }
        if ($p.Repo.Name -eq 'box') { $box += $p.Local; continue }
        $others += New-BuildStep $p.Repo.Dir "test/$($p.Local)" $p.Repo.BakeArgs "$($p.Repo.Name) test/$($p.Local)"
    }
    if ($box.Count) { New-BuildStep $Root $(if ($box.Count -eq 1) { "test/$($box[0])" } else { 'test' }) }
    $others
}

# Runs the clean builds $Mode asks for, from the outermost dependency in, ending with a
# rebuild of each of $Steps (see New-BuildStep). Rebuilding a project cleans and builds it
# and the packages it uses; a folder such as test rebuilds every project under it, box
# only once. The flecs bundle of box is built with bake3 whenever it is out of date,
# whatever the mode; the other bundles (wgpu-native, glfw, ...) are only rebuilt by bake3
# when their build is gone. Returns the exit code of the first step that fails, or 0.
function Invoke-CleanBuild($Steps, [string]$Mode) {
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
        foreach ($step in $Steps) {
            Write-Host "launch_box: rebuilding $($step.Label) ($Cfg)" -ForegroundColor Cyan
            $code = Invoke-BakeStep "rebuild of $($step.Label)" (@('rebuild', $step.Target, '--cfg', $Cfg) + $step.Args) $step.Dir
            if ($code -ne 0) { return $code }
        }
    }
    0
}

# Builds box_app after the clean builds $Mode asks for, then runs it with $Scripts. The
# build output is filtered; box_app itself runs on the console.
function Invoke-Box([string[]]$Scripts, [string]$Mode, [string]$GpuChoice = $Gpu) {
    $code = Invoke-CleanBuild @(New-BuildStep $Root 'apps/box_app') $Mode
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

# The test program bake3 builds for bake test project $Name: in the project folder, or in
# the build folder of the local environment of its repository when it builds in one.
function Get-TestExe([string]$Name) {
    $p = Get-TestProject $Name
    $project = Get-Content $p.Decl -Raw | ConvertFrom-Json
    $output = if ($project.value.output) { $project.value.output } else { $project.id }
    $ext = if ($OnWindows) { '.exe' } else { '' }
    if ($p.Repo.BakeArgs -contains '--local-env') {
        return Join-Path $p.Repo.Dir ".bake/local_env/build/$($project.id)/$(Get-BakeTriplet)/$output$ext"
    }
    Join-Path $p.Dir ".bake/$(Get-BakeTriplet)/$output$ext"
}

# Runs bake test project $Name with $Argv. When $Built, the test program runs directly
# the way bake3 runs it (from the project folder); otherwise bake3 run builds this
# project on its own first. Returns the exit code.
function Invoke-TestProgram([string]$Name, [string[]]$Argv, [bool]$Built) {
    $p = Get-TestProject $Name
    $exe = Get-TestExe $Name
    if ($Built -and (Test-Path $exe)) {
        return Invoke-Native $exe $Argv $p.Dir
    }
    Invoke-Bake (@('run', "test/$($p.Local)", '--cfg', $Cfg) + $p.Repo.BakeArgs + @('--') + $Argv) -Dir $p.Repo.Dir
}

# Runs unittest cases of a module and writes a report in the shape of the --json report of
# bake test programs. Written to .launch_box by launch_box.ps1 before each use. Usage:
# run_unittest.py <module dir> <module> <report> <project> [Class.test_method ...]
$UnittestRunner = @'
import importlib, json, os, sys, time, unittest

module_dir, module_name, report_path, project = sys.argv[1:5]
names = sys.argv[5:]
sys.path.insert(0, module_dir)
os.chdir(module_dir)
module = importlib.import_module(module_name)
loader = unittest.defaultTestLoader
suite = unittest.TestSuite([loader.loadTestsFromName(n, module) for n in names]) if names else loader.loadTestsFromModule(module)
tests = []


class Result(unittest.TextTestResult):
    def startTest(self, test):
        self._start = time.time()
        super().startTest(test)

    def _record(self, test, status):
        # Errors of setUpClass and the like arrive for a placeholder, not a test.
        if isinstance(test, unittest.TestCase):
            tests.append({"suite": type(test).__name__, "case": test._testMethodName, "status": status,
                          "elapsed": round(time.time() - getattr(self, "_start", time.time()), 6)})

    def addSuccess(self, test):
        super().addSuccess(test)
        self._record(test, "pass")

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self._record(test, "fail")

    def addError(self, test, err):
        super().addError(test, err)
        self._record(test, "error")

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self._record(test, "skip")

    def addExpectedFailure(self, test, err):
        super().addExpectedFailure(test, err)
        self._record(test, "pass")

    def addUnexpectedSuccess(self, test):
        super().addUnexpectedSuccess(test)
        self._record(test, "fail")


def flatten(s):
    for t in s:
        if isinstance(t, unittest.TestSuite):
            yield from flatten(t)
        else:
            yield t


# Listed before the run: a suite lets go of each test once it has run.
selected = [(type(t).__name__, t._testMethodName) for t in flatten(suite) if isinstance(t, unittest.TestCase)]
start = time.time()
result = unittest.TextTestRunner(verbosity=2, resultclass=Result).run(suite)
# Cases that never ran, because setUpClass or setUpModule failed, count as errors.
ran = {(t["suite"], t["case"]) for t in tests}
for suite_name, case in selected:
    if (suite_name, case) not in ran:
        tests.append({"suite": suite_name, "case": case, "status": "error", "elapsed": 0})
count = lambda status: sum(1 for t in tests if t["status"] == status)
with open(report_path, "w", encoding="utf-8") as f:
    json.dump({"project": project, "pass": count("pass"), "fail": count("fail") + count("error"),
               "empty": 0, "timeout": 0, "elapsed": round(time.time() - start, 6), "tests": tests}, f, indent=2)
sys.exit(0 if result.wasSuccessful() and not count("error") else 1)
'@

function Find-Python {
    foreach ($name in 'python3', 'python') {
        $cmd = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($cmd) { return $cmd.Source }
    }
    throw 'launch_box: python not found on the path'
}

# Runs unittest project $Name, all of it or the Class.test_method ids $Ids, with the
# runner above. Returns the exit code and the report entries of its runs; without a
# report every case of $Ids counts as an error.
function Invoke-Unittest([string]$Name, $Ids) {
    $p = Get-TestProject $Name
    $runner = Get-LauncherPath 'run_unittest.py'
    $UnittestRunner | Write-Utf8File $runner
    $part = Join-Path (Get-TestStateDir) "$Name.part"
    New-Item -ItemType Directory -Force (Split-Path $part) | Out-Null
    if (Test-Path $part) { Remove-Item -Force $part }
    $what = if ($Ids) { "$Name ($(@($Ids).Count) case(s))" } else { $Name }
    Write-Host "launch_box: testing $what" -ForegroundColor Cyan
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $code = Invoke-Native (Find-Python) (@('-u', $runner, $p.Repo.Dir, $p.Local, (ConvertTo-ForwardSlash $part), $Name) + @($Ids)) $p.Repo.Dir
    Write-Elapsed $what $watch $code 'passed in'
    $runs = $null
    if (Test-Path $part) {
        try { $runs = @((Get-Content $part -Raw | ConvertFrom-Json).tests) } catch { }
        Remove-Item -Force $part
    }
    if (-not $runs -and $Ids) { $runs = @(New-TestRuns $Ids 'error') }
    [pscustomobject]@{ Code = $code; Runs = $runs }
}

# Runs test project $Name in full with a report kept for the failed entry, and returns
# the exit code. Without a report afterwards the project is recorded as broken.
function Invoke-TestProject([string]$Name, [bool]$Built) {
    $report = Get-TestReportPath $Name
    New-Item -ItemType Directory -Force (Split-Path $report) | Out-Null
    if (Test-Path $report) { Remove-Item -Force $report }
    if ((Get-TestProject $Name).Repo.Type -eq 'unittest') {
        $result = Invoke-Unittest $Name $null
        if ($result.Runs) {
            [pscustomobject]@{ project = $Name; tests = $result.Runs } | ConvertTo-Json -Depth 5 | Write-Utf8File $report
        }
        $code = $result.Code
    } else {
        Write-Host "launch_box: testing $Name" -ForegroundColor Cyan
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $code = Invoke-TestProgram $Name @('-j', "$Jobs", '--json', (ConvertTo-ForwardSlash $report)) $Built
        Write-Elapsed $Name $watch $code 'passed in'
    }
    if (-not (Test-Path $report)) { '{"broken": true}' | Write-Utf8File $report }
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

# Report entries for Suite.case ids $Ids that all ended with $Status.
function New-TestRuns([string[]]$Ids, [string]$Status) {
    foreach ($id in $Ids) {
        $suite, $case = $id.Split('.', 2)
        [pscustomobject]@{ suite = $suite; case = $case; status = $Status }
    }
}

# Runs test case $Id (Suite.case) of project $Name on its own, records the outcome in
# the saved report and returns the exit code.
function Invoke-TestCase([string]$Name, [string]$Id, [bool]$Built) {
    Write-Host "launch_box: running $Name $Id" -ForegroundColor Cyan
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $code = Invoke-TestProgram $Name @($Id) $Built
    Write-Elapsed "$Name $Id" $watch $code 'passed in'
    Update-TestReport $Name @(New-TestRuns @($Id) $(if ($code -eq 0) { 'pass' } else { 'fail' }))
    $code
}

# Runs suite $Suite of project $Name with -j and a --json report, records its runs in the
# saved report and returns its exit code and the ids that did not pass. Without a report
# every case of $Ids counts as failed.
function Invoke-TestSuite([string]$Name, [string]$Suite, [string[]]$Ids, [bool]$Built) {
    $part = Join-Path (Get-TestStateDir) "$Name.$Suite.part"
    New-Item -ItemType Directory -Force (Split-Path $part) | Out-Null
    if (Test-Path $part) { Remove-Item -Force $part }
    Write-Host "launch_box: testing $Name $Suite" -ForegroundColor Cyan
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $code = Invoke-TestProgram $Name @($Suite, '-j', "$Jobs", '--json', (ConvertTo-ForwardSlash $part)) $Built
    Write-Elapsed "$Name $Suite" $watch $code 'passed in'
    $runs = $null
    if (Test-Path $part) {
        try { $runs = @((Get-Content $part -Raw | ConvertFrom-Json).tests) } catch { }
        Remove-Item -Force $part
    }
    if (-not $runs) { $runs = @(New-TestRuns $Ids 'fail') }
    Update-TestReport $Name $runs
    [pscustomobject]@{
        Code   = $code
        Failed = @($runs | Where-Object { $_.status -in $FailedStatuses } |
            ForEach-Object { "$($_.suite).$($_.case)" } | Select-Object -Unique)
    }
}

# Runs the test cases of $Selection, an ordered map from project name to Suite.case ids,
# or to $null for a project that runs in full (with a new report). A suite whose cases
# are all selected runs at once with -j; the other cases run one by one. Every outcome is
# recorded in the saved reports, so the failed entry and the case statuses follow it.
# Returns 0 when all passed, else the first failing exit code.
function Invoke-TestCases($Selection, [bool]$Built) {
    $declared = $script:caseCounts
    $still = @()
    $total = 0
    $first = 0
    foreach ($name in @($Selection.Keys)) {
        $ids = $Selection[$name]
        if ($null -eq $ids) {
            $total++
            $code = Invoke-TestProject $name $Built
            if ($code -ne 0) { $still += $name; if (-not $first) { $first = $code } }
            continue
        }
        if ((Get-TestProject $name).Repo.Type -eq 'unittest') {
            $total += @($ids).Count
            $result = Invoke-Unittest $name @($ids)
            Update-TestReport $name $result.Runs
            $still += @($result.Runs | Where-Object { $_.status -in $FailedStatuses } | ForEach-Object { "$name $($_.suite).$($_.case)" })
            if ($result.Code -ne 0 -and -not $first) { $first = $result.Code }
            continue
        }
        $suites = [ordered]@{}
        foreach ($id in $ids) {
            $suite = $id.Split('.')[0]
            if (-not $suites.Contains($suite)) { $suites[$suite] = [Collections.Generic.List[string]]::new() }
            $suites[$suite].Add($id)
        }
        foreach ($suite in @($suites.Keys)) {
            $suiteIds = @($suites[$suite])
            $total += $suiteIds.Count
            if ($suiteIds.Count -gt 1 -and $suiteIds.Count -ge $declared["$name $suite"]) {
                $result = Invoke-TestSuite $name $suite $suiteIds $Built
                $still += @($result.Failed | ForEach-Object { "$name $_" })
                if ($result.Code -ne 0 -and -not $first) { $first = $result.Code }
                continue
            }
            foreach ($id in $suiteIds) {
                $code = Invoke-TestCase $name $id $Built
                if ($code -ne 0) { $still += "$name $id"; if (-not $first) { $first = $code } }
            }
        }
    }

    if ($still.Count -and -not $first) { $first = 1 }
    Write-TestSummary $still $total 'test(s)'
    $first
}

# Runs the tests of $Entry after the clean builds $Mode asks for, then prints which
# projects failed. Returns 0 when all passed, else the first failing exit code.
#
# The selected projects are built first, those of box by one bake3 call, and their test
# programs then run directly: a bake3 run per project repeats project discovery and the
# up to date check every time. When a build fails, each project runs with its own bake3
# run instead, so one project that does not build does not stop the others.
function Invoke-Tests($Entry, [string]$Mode) {
    if ($Entry.Kind -eq 'failed' -and -not $Entry.Cases.Count) {
        Write-Host "launch_box: no failed tests recorded for $Cfg; run the tests first" -ForegroundColor Yellow
        return 0
    }

    $steps = @(Get-TestBuildSteps (Get-TestEntryProjects $Entry))
    $code = Invoke-CleanBuild $steps $Mode
    if ($code -ne 0) { return $code }
    $built = [Array]::IndexOf($BuildModes, $Mode) -ge 1
    if (-not $built) {
        $built = $true
        foreach ($step in $steps) {
            $code = Invoke-BakeStep "build of $($step.Label)" (@('build', $step.Target, '--cfg', $Cfg) + $step.Args) $step.Dir
            if ($code -ne 0) { $built = $false }
        }
        if (-not $built) {
            Write-Host 'launch_box: building and running each test project on its own' -ForegroundColor Yellow
        }
    }
    if ($Entry.Kind -in 'failed', 'case', 'filter', 'selection') { return Invoke-TestCases (Get-TestSelection $Entry) $built }

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

# The test selector entry that -Test names: 'all', 'failed' or a test project (core,
# flecs/query, bake3/run_tests, ...).
function Find-TestEntry($TestEntries, [string]$Name) {
    if ($Name -in 'all', 'failed') {
        return $TestEntries | Where-Object { $_.Kind -eq $Name } | Select-Object -First 1
    }
    $entry = $TestEntries | Where-Object { $_.Kind -eq 'test' -and $_.Key -eq $Name } | Select-Object -First 1
    if (-not $entry) {
        $known = ($TestEntries | Where-Object { $_.Kind -eq 'test' } | ForEach-Object { $_.Key }) -join ', '
        throw "launch_box: no test project '$Name'; use all, failed or one of: $known"
    }
    $entry
}

# The entry of $TestEntries that $Saved stands for (an entry of an earlier list, or a
# stub with Kind and Key restored from menu.json), or the 'all' entry when it is gone. A
# filter entry is built again over the current cases.
function Resolve-TestEntry($TestEntries, $Saved) {
    if ($Saved) {
        if ($Saved.Kind -eq 'filter') {
            $entry = New-FilterEntry $Saved.Key 1
            if ($entry) { return $entry }
        } elseif ($Saved.Kind -eq 'selection') {
            $entry = New-SelectionEntry @(Expand-SelectionKey $Saved.Key)
            if ($entry) { return $entry }
        } elseif ($Saved.Kind -eq 'case') {
            $entry = $script:caseByKey[[string]$Saved.Key]
            if ($entry) { return $entry }
        } else {
            $i = Find-EntryIndex $TestEntries $Saved
            if ($i -ge 0) { return $TestEntries[$i] }
        }
    }
    $TestEntries[0]
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
            tests = if ($State.Tests) { [ordered]@{ group = $State.Tests.Kind; name = $State.Tests.Key } } else { $null }
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
        $kind = [string]$saved.tests.group
        $key = if ($kind -in 'all', 'failed') { $kind } else { [string]$saved.tests.name }
        $State.Tests = [pscustomobject]@{ Kind = $kind; Key = $key }
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

if ($TestFilter) {
    [void](Get-TestEntries)
    $entry = New-FilterEntry $TestFilter 1
    if (-not $entry) { throw "launch_box: no test case matches '$TestFilter'" }
    exit (Complete-Run (Invoke-Tests $entry $Build) $total)
}

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

$state = [pscustomobject]@{ Build = $Build; Gpu = $Gpu; Scene = $null; Tests = $null; Item = $MenuScene
    Checks = [Collections.Generic.HashSet[string]]::new(); Expanded = [Collections.Generic.HashSet[string]]::new()
    SceneExpanded = [Collections.Generic.HashSet[string]]::new() }
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
    $state.Tests = Resolve-TestEntry $testEntries $state.Tests

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
