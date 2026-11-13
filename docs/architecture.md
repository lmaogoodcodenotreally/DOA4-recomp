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

- Release zip (via `tools/package_release.ps1`): the launcher and compiler
  (GUI + TUI), `extract-xiso.exe`, the `rexgluerd.exe` codegen tool and its
  runtime DLLs, the `src/` + `thirdparty/rexglue-sdk` sources the build needs,
  and a `doa4retail.toml` template. **No `doa4retail.exe` and no `game\`
  data** - the game binary is built on the player's machine by `compiler.exe`,
  because codegen needs their own dump as input.
- `compiler.exe` (the player's first step): user picks their own USA Rev 1
  ISO/XISO; the tool extracts `game\` (`default.xex` + `*.afs`) beside the
  exe, runs codegen, then the CMake build. It never downloads game data.
  Every child process is streamed to `compiler.log`; progress comes from
  ninja's `[n/m]`. A re-run reuses an existing `game\` and `generated/`.
- `DOA4Launcher.exe`: writes `doa4retail.toml` (preserving foreign keys via
  TOML merge) and launches the game with
  `--game_data_root game --gpu_plugin xenos`; the play build also sets
  `REX_DIAG=0`. On first run it auto-detects `doa4retail.exe`,
  `dev\doa4retail.exe` (developer build) and `game\` beside the launcher, or
  the repo `out/build/<preset>/` layout. The Paths tab's Re-detect only fills
  paths that are empty or missing on disk, never a working one.

## F5/F6/F8/F9/F11/Home/End keybinds

F5 (debug tab, `bind_doa4_debug`), F6 (mark hitch, `bind_doa4_mark`), F8 (free camera, `bind_doa4_freecam`), F9
(DS event replay, `bind_doa4_ds_event`), F11 (ST flipbook tuner,
`bind_doa4_st_tuner`), Home (stage overlay, `bind_doa4_overlay`) and End (PRO
light beams, `bind_doa4_pro_beams`) are free
of SDK binds (F3 debug, F4 settings, F7 achievements, Backtick console)
and of the keyboard-as-controller defaults, and show up rebindable in the
settings overlay. The launcher's Debug tab writes these `bind_doa4_*` keys
(and the `doa4_all_vs_stages` and `doa4_stage_info` switches), and only when the developer build is selected: the play build never registers
them and would log "unknown cvar". Kept app-side so the SDK stays game-agnostic. Details in
`debug.md`.

In the launcher every bind is a button: click it, press the new key (Escape
cancels), and the arrow next to it resets that one bind. Only keys the game's
bind parser knows are accepted (`KEY_NAMES` in `rust/launcher/src/lib.rs`,
mirroring `keybinds.cpp`; egui cannot tell numpad keys apart, so those are
only reachable through the console launcher). A bind that clashes with
another bind or a runtime default (F3, F4, F7, Backtick) is flagged.

## Ambient occlusion (SDK, `src/graphics/d3d12/render_target_cache_ssao.cpp`)

Launcher option "Ambient occlusion" sets `ssao_strength` (0 off, 0.6, 1);
`ssao_radius` (default 10, in near-plane units) and `ssao_debug` (AO term
alone) are hand-set cvars. DOA4's character shadows are already blurred
projective maps, so AO in creases was the cheaper visible gain. Only with a
render scale above 1x (it works on the scaled resolve buffer).

- **Finding the scene.** Each main-pass tile resolves raw depth, then colour
  (8888), from the same EDRAM surface into scene-sized textures (pitch
  ≥ 1024x576). `SsaoNoteResolve` keys on `RB_SURFACE_INFO` + dest pitch;
  `RB_COLOR_INFO`/`RB_DEPTH_INFO` are repointed per copy and depth dest
  format is rewritten to k_24_8, so neither can be matched. The lowest base
  of each kind is the texture origin.
- **When.** `SsaoFlushIfLeavingSurface` runs at the next draw or resolve on a
  different surface, i.e. after both tiles and before the bloom / DOF passes
  read the scene.
- **Passes.** `ssao_ao_cs` reads the tiled depth straight from the scaled
  buffer (layout from `texture_load_32bpb_scaled_cs`), 8-sample SAO with a
  4x4 rotation, into an R16G16F (ao, log2 z) texture. `ssao_apply_cs` does a
  4x4 depth-aware blur and multiplies RGB in place, keeping alpha.
- **Gotcha.** `PushTransitionBarrier` is a no-op for UAV -> UAV, so the
  resolve copy's UAV writes raced the apply pass (copy landed last, AO
  invisible). The apply pass pushes an explicit UAV barrier.
- **Cost.** Attract bench at 3x2: busy 9.24-9.31 ms on vs 9.24 off,
  out-of-time 0.1-0.2% both - within noise.

## Character shadows (SDK, `src/graphics/d3d12/render_target_cache_shadow.cpp`)

Launcher option "Character shadows" sets `shadow_quality` (0 original, 1 = 4x,
2 = 8x the guest shadow texture); advanced `shadow_softness` (default 0.5,
1 = about the game's own blur). Off by default; the Quality preset picks 8x.
Host render target path only.

- **The game's pass** (per frame, traced): into a 256x256 8888 + D24S8
  surface (`RB_SURFACE_INFO` 0x04000140), a full-screen textureless triangle
  (the lit background) and ~30 textureless caster draws; resolve to A; two
  full-screen blur passes sampling A, each resolved back to A; a copy of the
  surface to B (with a colour clear). The stage receivers fetch B (bilinear,
  border clamp) and `mad_sat` it into the lighting. No depth compare, so no
  self-shadowing; the same surface then draws a small reflection scene.
- **Mirroring.** After each guest draw, `ShadowMirrorDraw` re-issues
  textureless draws into small single-sample 8888 + depth surfaces on a
  private target at the shadow scale. Draws that sample A on the caster
  surface mark the blur; `ShadowNoteResolve` follows A -> (blurs into A) ->
  B and, at B, blurs the private copy (separable Gaussian, `shadow_blur_cs`)
  and registers it with `D3D12TextureCache::SetHiResOverride`, so fetches of
  B's address and size sample the high-resolution mask (bindless and
  bindful). Any other resolve to B drops the stand-in.
- **Gotcha.** The host viewport is the whole 8192-pixel guest range times the
  draw scale, with the guest mapping done in the vertex shader (ndc
  scale/offset). Scaling it again exceeds `D3D12_VIEWPORT_BOUNDS_MAX` and the
  draws vanish, so the mirrored draw gets a viewport from
  `GetHostViewportInfo` at the shadow scale and one-use system constants with
  that NDC transformation.
- **Cost.** Play settings (3x2, 2x MSAA, CAS, interpolation), interleaved
  with the committed build: busy 12.22 ms at 8x vs 12.34 / 12.34 off,
  out-of-time 9.5% vs 7.1-9.5% - within noise (attract content swings runs
  far more than this).
