# Third-party attribution

## extract-xiso (bundled binary `tools/extract-xiso.exe`)

Built from <https://github.com/XboxDev/extract-xiso> v2.7.1 (in@fishtank.com),
BSD-style Free license, see `tools/extract-xiso-LICENSE.txt`.

## ReXGlue SDK (vendored as git submodule)

`thirdparty/rexglue-sdk` is a git submodule pinned to the **doa4-native**
branch of the fork <https://github.com/lmaogoodcodenotreally/rexglue-sdk>,
based on upstream <https://github.com/rexglue/rexglue-sdk> (v0.10.1,
Release v0.10.1).

Upstream: ReXGlue — Tom Clay (tomc@tctechstuff.com), 2026, BSD-3-Clause
(see `LICENSE` inside the submodule). Portions derived from the Xenia
project (Ben Vanik; https://xenia.jp). ReXGlue is not affiliated with nor
endorsed by Microsoft/Xbox. Credits and disclaimer: see the submodule's
`LICENSE` and `README.md` (Credits section).

The fork (`doa4-native`) adds DOA4-specific work on top of upstream:
input/D3D12/movie-skip fixes and frame interpolation (experimental cvar,
default off). All original license and copyright headers inside the
submodule are retained unchanged.

To build from a fresh clone:

```bat
git clone --recurse-submodules https://github.com/lmaogoodcodenotreally/DOA4-recomp.git
cmake --preset win-amd64-relwithdebinfo-lean -DREXSDK_DIR=thirdparty/rexglue-sdk
cmake --build --preset win-amd64-relwithdebinfo-lean
```
