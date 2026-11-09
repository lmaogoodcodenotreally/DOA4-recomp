# DOA4-recomp

Work in progress native (ReXGlue / Recomp) port of Dead Or Alive 4 (USA, Rev 1), packaged for
[Quiver Launcher](https://github.com/tgeorgiadis/quiver-launcher): Quiver
downloads the GitHub release zip, runs `compiler.exe`, then the game.

**No game data ships here.** This repo contains only our own code (hooks,
launcher, compiler). Decompiled `generated/doa4/` and the `assets/game/`
dump are git-ignored and must never be published.

Docs: [architecture](docs/architecture.md) ·
[hooks](docs/hooks.md) · [debug](docs/debug.md)

## For players (Quiver)

1. Install from Quiver (or unzip the release zip anywhere).
2. Run `compiler.exe` (GUI) or `compiler-tui.exe` (TUI) and pick your own
   USA Rev 1 ISO/XISO. It checks/installs 7-Zip/CMake/Ninja/LLVM if missing
   (Yes/No), extracts `game\` (`default.xex` + `*.afs`) and recompiles
   (rexglue codegen + cmake build), beside the exe.
3. Run `DOA4Launcher.exe` (GUI) or `DOA4Launcher-tui.exe` (TUI) to set
   display/graphics/input, then **Launch**.

### Extra features (launcher `Settings.cs`)

- Widescreen / true Hor+ (`doa4_widescreen.cpp`, `aspect_ratio` + `present_letterbox`)
- Skip intro/disclaimer (`doa4_skip_intro.cpp`, `skip_intro` on by default)
- Attract fade fix (`doa4_attract_fade.cpp`)
- Frame interpolation (experimental, ~120 fps, `frame_interp` — game still 60 Hz)
- Screen sync / variable refresh & tearing (`d3d12_allow_variable_refresh_rate_and_tearing`)
- FidelityFX CAS/FSR output scaling (`present_effect`, needs FFX build: `REXGLUE_ENABLE_FIDELITYFX=ON`)
- Lean play build strips the F5/F6 debug tab, state poller and draw probe (`DOA4_LEAN=1`, dev only).

## For developers

Toolkit: **ReXGlue SDK** (vendored as a git submodule, see
`thirdparty/NOTICE.md` for upstream credit), with FidelityFX enabled
(`REXGLUE_ENABLE_FIDELITYFX=ON`). The build defaults `REXSDK_DIR` to
`thirdparty/rexglue-sdk` when present; clone with submodules:

```bat
git clone --recurse-submodules https://github.com/lmaogoodcodenotreally/DOA4-recomp.git
cmake --preset win-amd64-relwithdebinfo-lean
cmake --build --preset win-amd64-relwithdebinfo-lean
```

An explicit `-DREXSDK_DIR=<path-to-rexglue-sdk>` still overrides the
vendored copy.

Then run the SDK codegen against **your** `assets/game/doa4.xex` (USA Rev 1,
`default.xex` 16,465,920 bytes) with `doa4retail_manifest.toml` to regenerate
`generated/doa4/` locally — that output stays ignored.

```bat
cargo build --release --manifest-path rust\compiler\Cargo.toml
cargo build --release --manifest-path rust\launcher\Cargo.toml
powershell tools\package_quiver.ps1
```

`package_quiver.ps1` writes `publish\DOA4-recomp-win-x64.zip` (exes + DLLs,
no `game\`). Attach that zip to a GitHub release; Quiver tracks releases.

## Legal

This is a fan recomp project. Dead Or Alive 4 is a trademark of its rights
holder; this repo is not affiliated with or endorsed by them. No game assets,
decompiled code, or copyrighted text are included — only the original hooks,
launcher, and compiler written for this project, plus function-address
metadata in `doa4retail_manifest.toml`. You must dump your own USA Rev 1
copy to play; `compiler.exe` extracts your dump, never downloads it.

## Quiver readiness

- Release asset: `DOA4-recomp-win-x64.zip` (sha256 recorded by Quiver catalog)
- First step: `compiler.exe` (ISO/XISO → `game\`)
- Game entry: `DOA4Launcher.exe` (writes `doa4retail.toml`, launches with
  `--game_data_root game --gpu_plugin xenos`)
- This repo must stay **private until you flip it public** for Quiver catalog
  submission (`gh repo edit --visibility public`). Private repos are not
  listed in the Quiver catalog.
