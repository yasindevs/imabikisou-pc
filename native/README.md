# Native PC port integration

This is the hand-maintained integration layer for the ps3recomp-based Imabikisō port. It is separate from the earlier `imbk_player` executable. The local development build reaches the opening and classroom, but audio/movie issues remain.

## Source-only prerequisites

- Windows x64, Visual Studio 2022 clang-cl toolchain, CMake 3.20+ and Ninja.
- AVX2-capable CPU and Direct3D 12 support for the tested build.
- A separate ps3recomp checkout at the exact revision below.
- Your own BLJM60042 v01.00 dump and decrypted `EBOOT.ELF`.
- PPU and SPU sources generated locally with the patched ps3recomp tools. Generated code, import/function manifests and game files are not supplied here.

## Runtime patch

In a separate SDK checkout:

```sh
git clone https://github.com/sp00nznet/ps3recomp.git
cd ps3recomp
git checkout a2c20108af45174dbb7d3e2f87f6acaaef09c6b4
git apply --check /path/to/this-project/patches/ps3recomp/imabikisou-runtime.patch
git apply /path/to/this-project/patches/ps3recomp/imabikisou-runtime.patch
```

The patch contains the accumulated runtime and lifter work used by the prototype, including PS3 barrier layout, SPU events, font/input ABI, callback TLS, display timing and audio experiments. It is a development snapshot, not an upstream release. The manifest records the base commit and patched file hashes. Its application has been checked against the pinned base.

Use the pinned SDK's `docs/GETTING_STARTED.md` and its PPU/SPU tool help to generate private outputs after applying the patch. The PPU stack-mediated switch correction and SPU receive-event continuation require the corrected lifters. A complete clean-clone regeneration has **not yet been validated**; this repository does not promise a one-command conversion.

## Configure with private generated sources

From a Visual Studio developer shell, substitute your actual paths:

```sh
cmake -S native -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DPS3RECOMP_DIR=/absolute/path/to/ps3recomp -DRECOMP_DIR=/absolute/path/to/private/ppu-output -DSPU_DIR=/absolute/path/to/private/spu-output -DPORT_ENABLE_SPU=ON
cmake --build build-native
```

`RECOMP_DIR` must contain `ppu_recomp.h` and its generated translation units. `SPU_DIR` must match the SDK-generated per-image layout, including dispatch/table files. Build output: `build-native/audio-preview/ImabikisoPC.exe`.

The public runner is copied from the working prototype with SDK includes made independent of the developer's drive layout. Its clean-clone build remains a roadmap item. Do not confuse patch-application checks with a native rebuild.

## Run on Windows

From a private working directory (saves/settings are written relative to it):

```bat
set "PS3_VFS_ROOT=C:\path\to\your\disc"
set "PS3_ENTER_BUTTON=circle"
set "RSX_LIVE_DRAW=1"
set "RSX_VSYNC=0"
set "PS3_VERBOSE=0"
set "AUDIO_FILL_CURSOR=1"
set "AUDIO_HOST_PRIORITY=1"
set "SPU_HOST_PRIORITY=1"
C:\path\to\build-native\audio-preview\ImabikisoPC.exe C:\path\to\EBOOT.ELF
```

Quote executable and argument paths when they contain spaces. Use a separate disc view and working directory for English tests so they do not share Japanese save/settings files.

Controls: **X = Circle / confirm**, **Z = Cross / back**, arrows = navigation, Enter = Start. Xbox B = Circle / confirm. Focus the game window first.

## Diagnostics and limitations

`IMBK_SAMPLE` enables guest sampling; `tools/resolve_samples.py` resolves log RVAs using `build-native/MyGameRecomp.map`. Keep the map from the same executable. `AUDIO_RATE=1` reports mix cadence and backend drops. `AUDIO_WAV` writes raw stereo float32 PCM at 48 kHz, not a WAV container. Diagnostic captures can affect timing.

Approximately 57–60 presentations/s were measured in sampled scenes. Audio PCM still contains interruptions, movies still stutter, and a full playthrough has not been performed. No playable-release claim is made.
