# Build the release zip: Rust launcher + compiler (GUI and TUI), extract-xiso,
# the rexglue codegen tool, and the source tree the compiler needs to build the
# game on the player's machine.
#
# Ships NO game executable and NO game data - compiler.exe produces those from
# the player's own disc dump.
#
# Run after building the front-ends and the SDK (see "Build from source" in
# README.md for the exact commands):
#   cargo build --release --manifest-path rust\compiler\Cargo.toml
#   cargo build --release --manifest-path rust\launcher\Cargo.toml
#   cmake -S thirdparty\rexglue-sdk -B out\sdk ... && cmake --build out\sdk
# then:  tools\package_release.ps1 -SdkBin out\sdk\sdk-bin
param(
    # Where rexgluerd.exe and the runtime DLLs were built. Defaults to this
    # repo's own lean build; point it elsewhere to reuse another build tree.
    [string]$SdkBin,
    # The populated FidelityFX SDK tree to vendor, so the player's build does
    # not git-clone ~700 MB. Defaults to the one FetchContent left in the SDK
    # build tree. Pass "" to ship without it (the build then clones).
    [string]$FfxSource,
    [switch]$SkipZip
)
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$lean = Join-Path $repo 'out\build\win-amd64-relwithdebinfo-lean'
$dest = Join-Path $repo 'publish\DOA4-recomp-win-x64'

if (-not $SdkBin) { $SdkBin = Join-Path $lean 'sdk-bin' }

function Find-First([string[]]$Candidates) {
    foreach ($c in $Candidates) { if (Test-Path -LiteralPath $c) { return $c } }
    return $null
}

# --- the codegen tool, required ------------------------------------------------
$rexglue = Find-First @((Join-Path $SdkBin 'rexgluerd.exe'), (Join-Path $SdkBin 'rexglue.exe'))
if (-not $rexglue) {
    throw "rexgluerd.exe not found under '$SdkBin'. Build it first:`n" +
          "  cmake --preset win-amd64-relwithdebinfo-lean`n" +
          "  cmake --build --preset win-amd64-relwithdebinfo-lean`n" +
          "or pass -SdkBin <dir> pointing at an existing build."
}

# --- front-ends, required ------------------------------------------------------
$bins = @(
    (Join-Path $repo 'rust\launcher\target\release\DOA4Launcher.exe'),
    (Join-Path $repo 'rust\launcher\target\release\DOA4Launcher-tui.exe'),
    (Join-Path $repo 'rust\compiler\target\release\compiler.exe'),
    (Join-Path $repo 'rust\compiler\target\release\compiler-tui.exe'),
    (Join-Path $repo 'tools\extract-xiso.exe'),
    (Join-Path $repo 'tools\extract-xiso-LICENSE.txt'),
    $rexglue
)
foreach ($b in $bins) {
    if (-not (Test-Path -LiteralPath $b)) { throw "missing $b (did cargo build --release run?)" }
}

if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dest | Out-Null
foreach ($b in $bins) { Copy-Item -LiteralPath $b -Destination $dest -Force }

# --- runtime DLLs rexgluerd.exe imports ---------------------------------------
foreach ($d in @('rexruntimerd.dll', 'rexgpu-xenosrd.dll', 'amd_fidelityfx_dx12drel.dll', 'TracyClientrd.dll')) {
    # FidelityFX lands in ffx-bin, a sibling of the SDK's own output dir.
    $src = Find-First @(
        (Join-Path $SdkBin $d),
        (Join-Path (Split-Path -Parent $SdkBin) "ffx-bin\$d"),
        (Join-Path $lean $d),
        (Join-Path $lean "ffx-bin\$d"))
    if ($src) { Copy-Item -LiteralPath $src -Destination $dest -Force }
    elseif ($d -eq 'rexruntimerd.dll') { throw "rexruntimerd.dll not found near '$SdkBin'; rexgluerd.exe cannot start without it." }
}

# --- source tree the compiler builds on the player's machine ------------------
foreach ($f in @('doa4retail_manifest.toml', 'CMakeLists.txt', 'CMakePresets.json')) {
    Copy-Item -LiteralPath (Join-Path $repo $f) -Destination $dest -Force
}
Copy-Item -LiteralPath (Join-Path $repo 'src') -Destination $dest -Recurse -Force

$sdk = Join-Path $repo 'thirdparty\rexglue-sdk'
if (-not (Test-Path -LiteralPath (Join-Path $sdk 'CMakeLists.txt'))) {
    throw "thirdparty\rexglue-sdk is empty. Run: git submodule update --init --recursive"
}
# robocopy returns 1 for "files copied", which is success.
# /XF .git as well: a submodule's gitlink is a file, not a directory, so /XD
# misses it and it would ship as a dangling "gitdir:" pointer.
# The SDK's own bin\ is FidelityFX build output (gitignored there) and would
# ship as stale libs. Excluded by full path, because /XD matches bare names at
# any depth and some vendored dependencies have their own bin\ with real files.
robocopy $sdk (Join-Path $dest 'thirdparty\rexglue-sdk') /E /XD .git obj out (Join-Path $sdk 'bin') /XF .git /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }
$global:LASTEXITCODE = 0

# --- vendored FidelityFX SDK (optional) ---------------------------------------
# Only ffx-api (built) and sdk/include (headers) are used; docs, framework and
# samples are ~244 MB of dead weight.
if ($null -eq $PSBoundParameters['FfxSource']) {
    $FfxSource = Find-First @(
        (Join-Path $repo 'out\sdk\_deps\fidelityfx-src'),
        (Join-Path $lean '_deps\fidelityfx-src'))
}
if ($FfxSource -and (Test-Path -LiteralPath (Join-Path $FfxSource 'ffx-api\CMakeLists.txt'))) {
    $ffxDest = Join-Path $dest 'thirdparty\fidelityfx'
    robocopy $FfxSource $ffxDest /E /XD .git docs framework samples PrebuiltSignedDLL /NFL /NDL /NJH /NJS | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy of FidelityFX failed with exit code $LASTEXITCODE" }
    $global:LASTEXITCODE = 0
    Write-Host ("vendored FidelityFX from {0}" -f $FfxSource)
} else {
    Write-Warning ("No FidelityFX source vendored" +
        " - every player's first build will git-clone it (~700 MB)." +
        " Pass -FfxSource <dir> with a populated fidelityfx-src tree.")
}

# --- default settings, overwritten by the launcher on first Launch ------------
@'
game_data_root = "game"
gpu_plugin = "xenos"
fullscreen = true
mnk_mode = true
input_backend = "sdl"
present_effect = "cas"
scaled_point_filter_linear = true
aspect_ratio = -1.000000
skip_intro = true
present_letterbox = false
'@ | Set-Content -LiteralPath (Join-Path $dest 'doa4retail.toml') -Encoding utf8

@'
Dead or Alive 4 - native PC port (ReXGlue recompilation)

You need your own Xbox 360 disc dump: Dead or Alive 4 (USA, Europe), Revision 1.
No game code or data is included here.

1. Run compiler.exe (or compiler-tui.exe in a terminal) and give it your ISO.
   It checks for 7-Zip, CMake, Ninja and LLVM, offers to install the missing
   ones, extracts the disc into game\ and compiles the game. This takes a
   while; the progress bar is real. Details go to compiler.log.

2. Run DOA4Launcher.exe (or DOA4Launcher-tui.exe) to pick display, graphics
   and input settings, then press Launch.

About the folder you unzip into: its path must not contain a "^" (cmd.exe
treats it as an escape character and the build fails), and if it contains
non-ASCII characters such as accents, FidelityFX cannot be built, so CAS and
FSR will be unavailable. Spaces, apostrophes and parentheses are fine.
'@ | Set-Content -LiteralPath (Join-Path $dest 'README-INSTALL.txt') -Encoding utf8

if ($SkipZip) { Write-Host "staged $dest"; return }

$zip = Join-Path $repo 'publish\DOA4-recomp-win-x64.zip'
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
$seven = Find-First @('C:\Program Files\7-Zip\7z.exe', 'C:\Program Files (x86)\7-Zip\7z.exe')
if ($seven) { & $seven a -tzip $zip "$dest\*" | Out-Null }
else { Compress-Archive -Path "$dest\*" -DestinationPath $zip -Force }
Write-Host ("wrote {0} ({1:N0} MB)" -f $zip, ((Get-Item -LiteralPath $zip).Length / 1MB))
