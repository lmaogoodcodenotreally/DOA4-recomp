# Architecture

## Overview

DOA4-recomp is a native port built on **ReXGlue SDK** (with FidelityFX
enabled), vendored as a git submodule at `thirdparty/rexglue-sdk`
(upstream: <https://github.com/rexglue/rexglue-sdk>; fork branch
`doa4-native`; see `thirdparty/NOTICE.md`). The game-side entry point is
`src/main.cpp`; the app class is `src/doa4retail_app.h` (`rex::ReXApp`).
Hooks live in one file per concern under `src/` and are listed in
`CMakeLists.txt`. Codegen output (`generated/doa4/`) is produced locally
from the user's own dump and never shipped;
`doa4retail_manifest.toml` carries only function addresses/sizes.

See [hooks.md](hooks.md) for per-hook design notes and
[debug.md](debug.md) for the dev-only tooling.

## Build profiles

- **Dev build** (`win-amd64-relwithdebinfo`): keeps all project-side
  instrumentation — F5/F6 debug tab, state poller / `[state]` label,
  `DOA4_DUMP_IMAGE` hook, and the draw probe. `DOA4_LEAN` defaults to 0.
- **Lean play build** (`win-amd64-relwithdebinfo-lean`): `DOA4_LEAN=1`
  compiles out all project-side instrumentation so play sessions carry zero
  debug overhead. The SDK's own diagnostic probes stay in the DLLs but are
  made inert at runtime with `REX_DIAG=0` (set by the launcher for the play
  build). The preset also disables Tracy/perf counters, enables FidelityFX,
  and uses `-march=x86-64-v3` with `-O3` for RelWithDebInfo.

## State polling (v1 / v2)

The SDK polls a guest address once per frame (`REXCVAR perf_state_addr`)
for the current state id, installable game-agnostically from the app.

- **v1 fallback:** `0x82C31C24`, the MSEL screen-code global. RE: written by
  menu dispatcher `sub_8252C550` with the matched screen code; reset to -1
  by `sub_8253D960` when the menu system tears down. `>= 0` = menu screen,
  `-1` = outside the menus (gameplay/title/load).
- **v2 poll fn:** `OnPostSetup` registers `SetStatePollFn`
  (`PollGuestState`) which packs several globals into one `int64` — game
  state, stage ids, chars, screen code, record state — plus a label fn.
  The v1 address stays set as the fallback and is ignored while the poll
  fn is registered (SDK precedence), so removing the poll fn restores v1.
  See `debug.md` for the pack layout and the R30 finding that dropped
  record-state byte `0x83033B77` (churned 1–3×/s in every mode).

## Detector / packaging layout

- Release zip (via `tools/package_quiver.ps1`): the launcher and compiler
  (GUI + TUI), `extract-xiso.exe`, the `rexgluerd.exe` codegen tool and its
  runtime DLLs, the `src/` + `thirdparty/rexglue-sdk` sources the build needs,
  and a `doa4retail.toml` template. **No `doa4retail.exe` and no `game\`
  data** - the game binary is built on the player's machine by `compiler.exe`,
  because codegen needs their own dump as input.
- `compiler.exe` (Quiver first step): user picks their own USA Rev 1
  ISO/XISO; the tool extracts `game\` (`default.xex` + `*.afs`) beside the
  exe, runs codegen, then the CMake build. It never downloads game data.
  Every child process is streamed to `compiler.log`; progress comes from
  ninja's `[n/m]`. A re-run reuses an existing `game\` and `generated/`.
- `DOA4Launcher.exe`: writes `doa4retail.toml` (preserving foreign keys via
  TOML merge) and launches the game with
  `--game_data_root game --gpu_plugin xenos`; the play build also sets
  `REX_DIAG=0`. On first run it auto-detects `doa4retail.exe` and `game\`
  beside the launcher, or the repo `out/build/<preset>/` layout.

## F5/F6 keybinds

F5 (debug tab) and F6 (mark hitch) are free of SDK binds (F3 debug, F4
settings, F7 achievements, Backtick console) and show up rebindable in the
settings overlay. Kept app-side so the SDK stays game-agnostic. Details in
`debug.md`.
