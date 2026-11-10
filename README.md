# DOA4-recomp

A native PC port of **Dead or Alive 4** (Xbox 360), built by static
recompilation with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).
Work in progress, and heavily AI-assisted.

**No game code or data is in this repository, and none is downloaded.** You
supply your own Xbox 360 disc dump; the included compiler turns it into a
playable build on your machine. The decompiled output (`generated/doa4/`) and
your extracted dump stay on your disk and are git-ignored.

---

## Play

You need:

- **Dead or Alive 4 (USA, Europe) Revision 1** — your own disc dump, as an
  `.iso`/XISO. `default.xex` must be 16,465,920 bytes; other regions and
  revisions will not work, because the function addresses in
  `doa4retail_manifest.toml` are specific to this build.
- **64-bit Windows 10 or 11**, a D3D12-capable GPU, and ~15 GB free (the dump,
  the generated code and the build).
- A CPU with **AVX2** (Intel Haswell / AMD Excavator, 2013 or newer). The build
  targets `x86-64-v3`, and the SDK uses SSSE3+ intrinsics unconditionally.
- Build tools: **7-Zip, CMake, Ninja, LLVM/clang** and the **Visual Studio
  Build Tools** ("Desktop development with C++" — clang compiles against the
  MSVC headers and libraries). The compiler checks for all of these and offers
  to install the first four with `winget`; the Build Tools you install
  yourself, since it is a multi-gigabyte install.

Then:

1. Download `DOA4-recomp-win-x64.zip` from
   [Releases](https://github.com/lmaogoodcodenotreally/DOA4-recomp/releases)
   and unzip it anywhere, with two caveats about the folder's path. **It must
   not contain a `^`** — cmd.exe treats it as an escape character and the build
   fails at the link step, so the compiler refuses such a path up front. And if
   it contains **non-ASCII characters** (an accent, say, as in
   `C:\Users\José\…`), FidelityFX's shader compiler cannot handle it: the
   compiler then builds without FidelityFX, so the game runs but the CAS and
   FSR output-scaling options are unavailable. Spaces, apostrophes and
   parentheses are all fine.
2. Run **`compiler.exe`** (or `compiler-tui.exe` for a terminal), drag your ISO
   in, and press Build. It extracts the disc into `game\`, translates the game
   code, and compiles it. This takes a while — the progress bar tracks real
   compiler progress, and everything is logged to `compiler.log`.
3. Run **`DOA4Launcher.exe`** (or `DOA4Launcher-tui.exe`), choose your
   settings, press Launch.

Re-running the compiler reuses an already-extracted `game\` folder and the
existing generated code, so a rebuild is much faster than the first run.

This is also packaged for [Quiver
Launcher](https://github.com/tgeorgiadis/quiver-launcher), which downloads the
release zip, runs `compiler.exe` and then the game.

### Settings

The launcher writes `launcher.json` (its own state) and `doa4retail.toml` (the
game's cvars, merging your choices and leaving any other keys alone).

| Tab | Setting | Notes |
| --- | --- | --- |
| Display | Fullscreen, monitor, window size | `window_width`/`window_height` of 0 means "use the current video mode" |
| Display | Aspect ratio | True Hor+ widescreen, up to 32:9 (`src/doa4_widescreen.cpp`) |
| Display | Keep 16:9 (letterbox) | Off by default, so wide displays fill |
| Display | Frame interpolation | Experimental, ~120 fps; the game logic stays at 60 Hz |
| Display | Screen sync | V-Sync, or off for variable refresh / tearing |
| Graphics | Internal resolution | 1x–4x |
| Graphics | Anti-aliasing, native 2x MSAA, texture filtering | |
| Graphics | Output scaling | Bilinear, or FidelityFX CAS / FSR 1–3 |
| Input | Controller backend | SDL or XInput |
| Input | Keyboard as controller, legacy DirectInput pads | |
| Input | Skip intro | On by default (`src/doa4_skip_intro.cpp`) |

### When something goes wrong

- **The build failed.** Read `compiler.log` next to the exe — the real compiler
  error is at the end. The in-app error box shows the same lines.
- **"This folder's path contains a `^`".** Move the folder. CMake wraps build
  steps in `cmd /C`, where `^` is the escape character, so the link step fails.
- **"Visual Studio Build Tools" stays missing.** Install "Desktop development
  with C++" from <https://visualstudio.microsoft.com/downloads/>; the Build
  Tools edition is enough.
- **CMake cannot find Ninja.** Ninja often ships inside Visual Studio rather
  than on `PATH`; the compiler finds it there and extends `PATH` itself. If you
  run `cmake` by hand, add Ninja's directory yourself.
- **The game does not start.** Check `logs\` next to the game exe. The launcher
  will not let you press Launch until it can see both the exe and `game\`.
- **CAS/FSR do nothing.** They need FidelityFX. The release preset enables it,
  but the compiler switches it off when the install path has a non-ASCII
  character (see above) - `compiler.log` says so when it does. Move the folder
  to an all-ASCII path and build again.

---

## Build from source

```bat
git clone --recurse-submodules https://github.com/lmaogoodcodenotreally/DOA4-recomp.git
cd DOA4-recomp
```

The build needs generated code, which comes from **your own** dump, so run
codegen before configuring CMake:

```bat
:: stage your dump where the manifest expects it
mkdir assets\game
copy <your>\default.xex assets\game\doa4.xex

:: build the SDK's codegen tool, then run it
cmake -S thirdparty\rexglue-sdk -B out\sdk -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DREXGLUE_BUILD_TESTS=OFF ^
      "-DCMAKE_C_FLAGS=-march=x86-64-v3" "-DCMAKE_CXX_FLAGS=-march=x86-64-v3"
cmake --build out\sdk --target rexglue
out\sdk\rexgluerd.exe codegen ./doa4retail_manifest.toml

:: now the game itself
cmake --preset win-amd64-relwithdebinfo-lean
cmake --build --preset win-amd64-relwithdebinfo-lean
```

Or simply run `compiler.exe`, which does all of the above.

Presets:

- `win-amd64-relwithdebinfo-lean` — the **play** build. `DOA4_LEAN=1` compiles
  out the project's debug tooling; Tracy and perf counters off; FidelityFX on.
- `win-amd64-relwithdebinfo` — the **dev** build, with the F5/F6 debug tab, the
  state poller and the draw probe. See [docs/debug.md](docs/debug.md).

The launcher and compiler are plain Rust, with no game dependency:

```bat
cargo build --release --manifest-path rust\compiler\Cargo.toml
cargo build --release --manifest-path rust\launcher\Cargo.toml
cargo test --manifest-path rust\compiler\Cargo.toml
cargo test --manifest-path rust\launcher\Cargo.toml
```

Package a release zip (needs a built `rexgluerd.exe`):

```bat
powershell -File tools\package_quiver.ps1 -SdkBin out\sdk
```

The zip also vendors the FidelityFX SDK, so a player's build does not have to
`git clone` ~700 MB during configure. The script picks up the tree CMake's
FetchContent already populated (`out\sdk\_depsidelityfx-src`); pass
`-FfxSource <dir>` to point it elsewhere, or `-FfxSource ""` to ship without it
and let each build clone. Our `CMakeLists.txt` sets
`FETCHCONTENT_SOURCE_DIR_FIDELITYFX` automatically when the vendored copy is
present.

`REXSDK_DIR` defaults to the vendored submodule; pass
`-DREXSDK_DIR=<path>` to build against an SDK checkout elsewhere.

Docs: [architecture](docs/architecture.md) · [hooks](docs/hooks.md) ·
[debug](docs/debug.md) · [contributing](CONTRIBUTING.md)

---

## Legal

Fan project, not affiliated with or endorsed by the rights holders of Dead or
Alive 4, Team Ninja, Koei Tecmo or Microsoft. No game assets, no decompiled
game code and no copyrighted text are distributed here — only the original
hooks, launcher and compiler written for this project, plus function addresses
and sizes in `doa4retail_manifest.toml`. You must dump your own copy;
`compiler.exe` only reads the dump you give it and never downloads anything.

Our code is BSD-3-Clause ([LICENSE](LICENSE)). Third-party components keep
their own licenses — see [thirdparty/NOTICE.md](thirdparty/NOTICE.md).
