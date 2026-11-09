# Package the Quiver release zip: exes + DLLs + launcher + compiler, NO game data.
# Run after a lean build (win-amd64-relwithdebinfo-lean) and launcher/compiler publish.
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$lean="$repo\out\build\win-amd64-relwithdebinfo-lean"
$dest="$repo\publish\DOA4-recomp-win-x64"
New-Item -ItemType Directory -Force -Path $dest | Out-Null
$bins=@(
  "$lean\doa4retail.exe",
  "$lean\rexruntimerd.dll",
  "$lean\rexgpu-xenosrd.dll",
  "$lean\amd_fidelityfx_dx12drel.dll",
  "$repo\launcher\publish\DOA4Launcher.exe",
  "$repo\compiler\publish\compiler.exe"
)
foreach($b in $bins){ if(-not (Test-Path $b)){ throw "missing $b" }; Copy-Item $b $dest -Force }
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
DOA4-recomp for Quiver
1. Run compiler.exe, pick your USA Rev 1 ISO/XISO (Dead Or Alive 4, US Rev 1).
   It extracts game\ (default.xex + *.afs) into this folder.
2. Run DOA4Launcher.exe to set display/graphics/input, then Launch.
No game data is included in this download.
'@ | Set-Content "$dest\README-INSTALL.txt"
Compress-Archive "$dest\*" "$repo\publish\DOA4-recomp-win-x64.zip" -Force
Write-Host "wrote $repo\publish\DOA4-recomp-win-x64.zip"
