# DOA4-recomp

**Dead or Alive 4 running natively on PC.** Not an emulator: the Xbox 360 game
code is statically recompiled into a real Windows program with the
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), so it runs at your
monitor's resolution and refresh rate, in proper widescreen, with modern
controllers.

Work in progress, and heavily AI-assisted.

> **You need your own copy of the game.** No game code or data is in this
> repository and none is downloaded. You supply a dump of your own disc and the
> included compiler turns it into a playable build on your machine. Your dump
> and the generated code stay on your disk.

---

## Getting it running

### 1. Check you have what you need

- **Dead or Alive 4 (USA, Europe) — Revision 1**, dumped from your own disc as
  an `.iso`. Inside it, `default.xex` must be exactly **16,465,920 bytes**.
  Other regions and revisions do **not** work: the function addresses in
  `doa4retail_manifest.toml` only match this one build. (If you want play the original build and not rev 1 go see [doa4-recomp-suite](https://github.com/redC0rvus/doa4-recomp-suite) by [redC0rvus](https://github.com/redC0rvus/) 
- **64-bit Windows 10 or 11** and a GPU that supports Direct3D 12.
- A CPU with **AVX2** — any Intel from 2013 (Haswell) or AMD from 2015 onward.
- About **15 GB** of free space for the dump, the generated code and the build.
- These build tools: **7-Zip, CMake, Ninja, LLVM/clang**, plus the **Visual
  Studio Build Tools** with "Desktop development with C++" (clang compiles
  against Microsoft's headers and libraries, so it needs them even though clang
  itself comes from LLVM).

  The compiler checks for all of these and offers to install the first four for
  you with `winget`. The Build Tools you install yourself, from
  <https://visualstudio.microsoft.com/downloads/> — it is a multi-gigabyte
  download, so it is not something to kick off behind your back.

### 2. Unzip it somewhere sensible

Download `DOA4-recomp-win-x64.zip` from
[Releases](https://github.com/lmaogoodcodenotreally/DOA4-recomp/releases) and
unzip it anywhere — with two things to know about the folder you choose:

- Its path **must not contain a `^`**. Windows' own build tools treat it as an
  escape character and the build fails right at the end, so the compiler checks
  for it and stops immediately instead of wasting your time.
- If the path contains **non-ASCII characters** — an accent, as in
  `C:\Users\José\…` — FidelityFX cannot be compiled there. The game builds and
  runs fine; you just lose the CAS and FSR sharpening options. Move the folder
  to an all-ASCII path if you want them.

Spaces, apostrophes and parentheses are all fine.

### 3. Build the game

Run **`compiler.exe`** (or `compiler-tui.exe` if you prefer a terminal), point
it at your ISO, and press **Build**. It extracts the disc into `game\`,
translates the game code, and compiles it.

This takes a while — a thousand-odd source files — and the progress bar tracks
the real compiler, not a guess. Everything it does is written to
`compiler.log` next to the exe, which is the first place to look if something
goes wrong. Running it again reuses the extracted disc and the generated code,
so a second build is much quicker.

### 4. Play

Run **`DOA4Launcher.exe`**, pick your settings, press **Launch**.

---

## Settings

The launcher keeps its own state in `launcher.json` and writes the game's
settings to `doa4retail.toml`, leaving any keys it does not manage alone.
Hover any setting in the launcher for an explanation of what it does.

**Graphics presets** — start here. *Performance* for laptops and older GPUs,
*Balanced* (the default) for most machines, *Quality* if you have a desktop
GPU. Change any individual setting and the preset switches itself to *Custom*.

| Tab | What is in it |
| --- | --- |
| **Display** | Fullscreen, monitor, aspect ratio, letterboxing, frame interpolation, V-Sync. **Match my display** sets the aspect ratio to your monitor's exact shape for true widescreen. |
| **Graphics** | Presets, internal resolution (1x–8x), output scaling (CAS / FSR), MSAA, post anti-aliasing, texture filtering. Per-axis render scales live under *Advanced*. |
| **Input** | Controller backend, keyboard-as-controller, skip intro, mute. Legacy DirectInput pads are under *Advanced* — leave that off unless you need it, as it adds input lag. |
| **Paths** | The game exe and data folder, both detected automatically, with **Browse** if you have moved them. |

Internal resolution and the per-axis render scales only take effect after the
game restarts.

The console launcher does the same job without a window:

```
DOA4Launcher-tui --list              every setting, with an explanation
DOA4Launcher-tui --preset Balanced   apply a graphics preset
DOA4Launcher-tui --set skip_intro=false resolution_scale=3
DOA4Launcher-tui --launch            launch with the saved settings
```

---

## If something goes wrong

- **The build failed.** Open `compiler.log` next to the exe; the actual
  compiler error is at the end. The launcher's error box shows the same lines,
  just fewer of them.
- **"This folder's path contains a `^`".** Move the folder. See step 2.
- **The Build Tools keep showing as missing.** Install "Desktop development
  with C++"; the Build Tools edition is enough, you do not need full Visual
  Studio.
- **CMake cannot find Ninja.** Ninja usually ships inside Visual Studio rather
  than on your `PATH`. The compiler finds it there by itself — but if you run
  `cmake` by hand, add Ninja's folder to `PATH` first.
- **CAS or FSR do nothing.** They need FidelityFX, which cannot be built under
  a non-ASCII path. `compiler.log` says so when that happens.
- **The game will not start.** Check the newest file in the `logs\` folder next
  to the game exe. The launcher will not let you press Launch until it can see
  both the executable and the game data.

Still stuck? [Open an issue](https://github.com/lmaogoodcodenotreally/DOA4-recomp/issues)
with `compiler.log` attached.

---

## Building from source

```bat
git clone --recurse-submodules https://github.com/lmaogoodcodenotreally/DOA4-recomp.git
cd DOA4-recomp
```

The build needs generated code, which comes from **your own** dump, so run
codegen before CMake:

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

:: now the game
cmake --preset win-amd64-relwithdebinfo-lean
cmake --build --preset win-amd64-relwithdebinfo-lean
```

`compiler.exe` simply does all of the above for you.

Presets:

- `win-amd64-relwithdebinfo-lean` — the **play** build. `DOA4_LEAN=1` compiles
  out the project's debug tooling; Tracy and perf counters off, FidelityFX on.
- `win-amd64-relwithdebinfo` — the **dev** build, keeping the F5/F6 debug tab,
  the state poller and the draw probe. See [docs/debug.md](docs/debug.md).

The launcher and compiler are plain Rust and need no game data:

```bat
cargo test  --manifest-path rust\compiler\Cargo.toml
cargo test  --manifest-path rust\launcher\Cargo.toml
cargo build --release --manifest-path rust\compiler\Cargo.toml
cargo build --release --manifest-path rust\launcher\Cargo.toml
```

Package a release zip (needs a built `rexgluerd.exe`):

```bat
powershell -File tools\package_release.ps1 -SdkBin out\sdk\sdk-bin
```

The zip vendors the FidelityFX SDK so players do not each have to `git clone`
~700 MB during configure. The script picks up the copy CMake's FetchContent
already populated; pass `-FfxSource <dir>` to point it elsewhere, or
`-FfxSource ""` to leave it out. `CMakeLists.txt` sets
`FETCHCONTENT_SOURCE_DIR_FIDELITYFX` automatically when a vendored copy is
present.

`REXSDK_DIR` defaults to the vendored submodule; pass `-DREXSDK_DIR=<path>` to
build against an SDK checkout elsewhere.

Docs: [architecture](docs/architecture.md) · [hooks](docs/hooks.md) ·
[debug](docs/debug.md) · [contributing](CONTRIBUTING.md)

---

## Legal

A fan project, not affiliated with or endorsed by the rights holders of Dead or
Alive 4, Team Ninja, Koei Tecmo or Microsoft.

No game assets, no decompiled game code and no copyrighted text are distributed
here — only the original hooks, launcher and compiler written for this project,
plus function addresses and sizes in `doa4retail_manifest.toml`. You must dump
your own copy; `compiler.exe` only ever reads the dump you hand it, and never
downloads anything.

Our code is BSD-3-Clause ([LICENSE](LICENSE)). Third-party components keep
their own licences — see [thirdparty/NOTICE.md](thirdparty/NOTICE.md).
