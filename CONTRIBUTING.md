# Contributing

Patches are welcome. The project is small and the rules are few.

## Before you start

- You need your own **Dead or Alive 4 (USA, Europe) Rev 1** dump to build or
  test anything. We cannot provide one, and we will close issues or PRs that
  ask for game files or link to them.
- Read [docs/architecture.md](docs/architecture.md) first — it explains how a
  hook reaches the recompiled game, and which parts are generated.
- `README.md` has the build steps. `compiler.exe` is the shortcut: it runs
  codegen and the CMake build for you.

## What never goes in a commit

The repository must stay free of game content, forever:

- no game code, decompiled or otherwise, and no `generated/` output
- no game assets, dumps, `.xex`, `.iso` or `.afs` files
- no strings copied out of the game binary

`.gitignore` already covers the usual paths — please do not add exceptions to
it. `doa4retail_manifest.toml` holds function addresses and sizes only; that is
the one place game-derived metadata belongs.

## Code

**Game-side hooks** (`src/`) — one file per concern, named `doa4_<topic>.cpp`,
registered in `CMakeLists.txt`. A hook that changes what the player sees needs
a comment saying which game function it patches and why, with the address. Put
anything dev-only behind `#if !DOA4_LEAN` so the play build stays clean.

**Launcher and compiler** (`rust/`) — `cargo build` and `cargo test` must pass
for both crates. Shared logic lives in `lib.rs`; the GUI and TUI binaries are
thin front-ends over it, so a behaviour change belongs in the library where
both get it. New non-trivial logic gets a test next to it.

**The SDK submodule** (`thirdparty/rexglue-sdk`) — fixes there belong in the
[fork](https://github.com/lmaogoodcodenotreally/rexglue-sdk) on the
`doa4-native` branch, as their own commit, with the submodule pointer bumped
here in a separate commit. Say in the PR why the change cannot live in this
repo instead.

Match the style already in the file you are editing. Comment the *why*, not the
what.

## Testing a change

State in the PR what you actually ran. At minimum:

- `cargo test` for both Rust crates, if you touched them
- a full `compiler.exe` run from an unzipped release, if you touched the
  compiler, CMake files or the packaging script — that is the path every player
  takes, and it is where this project has broken before
- for a gameplay or rendering hook: which fight, stage or menu you checked, and
  a before/after screenshot when it is visual

Known-good reference settings live in the launcher defaults; `present_effect =
"cas"` needs a FidelityFX build.

## Reporting a bug

Use the issue templates. For a build failure, attach `compiler.log` — it has
the real compiler error, which the summary in the UI only excerpts. For a game
bug, attach the newest file from the game's `logs\` folder and say which
settings you used.

Three things that are already known and are not bugs:

- a `^` anywhere in the install path breaks the build (cmd.exe treats it as an
  escape character); the compiler refuses up front and tells you to move the
  folder
- a non-ASCII character in the install path makes FidelityFX's shader compiler
  fail, so the compiler builds without FidelityFX and CAS/FSR are unavailable.
  Spaces, apostrophes and parentheses are fine - only non-ASCII is a problem
- regions and revisions other than USA/Europe Rev 1 do not work at all
