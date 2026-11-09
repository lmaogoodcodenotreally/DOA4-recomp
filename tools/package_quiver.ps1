# Package the Quiver release zip: exes + DLLs + Rust launcher/compiler (GUI+TUI), NO game data.
# Run after a lean build (win-amd64-relwithdebinfo-lean) and cargo build --release.
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$lean="$repo\out\build\win-amd64-relwithdebinfo-lean"
$dest="$repo\publish\DOA4-recomp-win-x64"
New-Item -ItemType Directory -Force -Path $dest | Out-Null
$cbt="$repo\rust\compiler\target\release"
$lbt="$repo\rust\launcher\target\release"
$bins=@(
  "$lean\doa4retail.exe",
  "$lean\rexruntimerd.dll",
  "$lean\rexgpu-xenosrd.dll",
  "$lean\amd_fidelityfx_dx12drel.dll",
  "$lbt\DOA4Launcher.exe",
  "$lbt\DOA4Launcher-tui.exe",
  "$cbt\compiler.exe",
  "$cbt\compiler-tui.exe",
  "$repo\tools\extract-xiso.exe"
)
# Optional codegen tool (rexgluerd.exe) from the dev lean build, if present
if (Test-Path "$lean\sdk-bin\rexgluerd.exe") { $bins += "$lean\sdk-bin\rexgluerd.exe" }
foreach($b in $bins){ if(-not (Test-Path $b)){ throw "missing $b" }; Copy-Item $b $dest -Force }
# Source tree needed for the recompile step (codegen + cmake build).
# No game data, no generated/ (produced by codegen from the user's own dump).
foreach($f in @("doa4retail_manifest.toml","CMakeLists.txt","CMakePresets.json")) { Copy-Item "$repo\$f" $dest -Force }
Copy-Item "$repo\src" $dest -Recurse -Force
Copy-Item "$repo\thirdparty" $dest -Recurse -Force
Copy-Item "$repo\.gitmodules" $dest -Force
# saved_manifest.txt is needed by compiler for codegen fallback; keep .txt so rexglue auto-discovery (which globs *_manifest.toml) ignores it.
Copy-Item "$repo\doa4retail_manifest.toml" "$dest\saved_manifest.txt" -Force
# Ensure only .txt ships, not stale .toml
Remove-Item "$dest\saved_manifest.toml" -Force -ErrorAction SilentlyContinue
@'
game_data_root = "game"
gpu_plugin = "xenos"
fullscreen = true
mnk_mode = true
input_backend = "sdl"
present_effect = "cas"
scaled_point_filter_linear = true
aspect_ratio = -1
skip_intro = true
present_letterbox = false
'@ | Set-Content "$dest\doa4retail.toml"
@'
DOA4-recomp for Quiver (Rust launcher/compiler, no .NET)
1. Run compiler.exe (GUI) or compiler-tui.exe (TUI in terminal), pick your USA Rev 1 ISO/XISO.
   It checks/installs 7-Zip, CMake, Ninja, LLVM if missing (Yes/No), extracts game\ and recompiles.
2. Run DOA4Launcher.exe (GUI) or DOA4Launcher-tui.exe (TUI) to set display/graphics/input, then Launch.
No game data is included in this download.
'@ | Set-Content "$dest\README-INSTALL.txt"
Compress-Archive "$dest\*" "$repo\publish\DOA4-recomp-win-x64.zip" -Force
Write-Host "wrote $repo\publish\DOA4-recomp-win-x64.zip"
