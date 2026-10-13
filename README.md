# Imabikisō PC port tools

Tools to run **Imabikisō** (忌火起草, Chunsoft, PS3, `BLJM60042`) on PC from **your own copy** of the game, with
fan translations planned.

> **This repository contains no game data.** No images, audio, video, fonts or script text from the game are
> distributed here. Everything is read at runtime from a disc image you dumped yourself. Do not open issues or
> pull requests containing game files, and do not ask for or share ISOs.

## Status

| Part | State |
|---|---|
| Reading the game straight from the `.iso` (ISO 9660), or from a folder | ✅ |
| CRI AFS archives (list / extract) | ✅ |
| Main script `st.sns` (CSD-CSCNV 01.68): decryption, container, resource tables | ✅ |
| Main script `st.sns`: bytecode structure, labels, line export (19,018 lines) | ✅ structure, 🚧 opcode semantics |
| Secondary script `scenario.sn4` (ScenarioLink): bytecode, label table, line export | ✅ structure, 🚧 opcode semantics |
| Engine (rendering, audio, video, choices) | ⏳ not started |
| Translation patch: `imbk patch` rebuilds the main script and `residentchunk.afs` | ✅ verified offline, 🚧 in-game test |

Docs (in French for now): [docs/patching.md](docs/patching.md), [docs/sns-format.md](docs/sns-format.md),
[docs/sn4-format.md](docs/sn4-format.md).

## Building

Requirements: CMake ≥ 3.20 and a C++20 compiler (GCC 13+, Clang 16+ or MSVC 2022).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

On Linux/macOS the build also needs `iconv` (part of glibc/libSystem).

## Usage

```sh
imbk verify  "Imabikisou (Japan).iso"         # check that your dump is the supported release
imbk info    "Imabikisou (Japan).iso"         # metadata and archive overview
imbk ls      "Imabikisou (Japan).iso" bgm.afs
imbk extract "Imabikisou (Japan).iso" out/ scriptchunk.afs
imbk sns info    "Imabikisou (Japan).iso"
imbk sns text    "Imabikisou (Japan).iso" out/lines.tsv
imbk sns disasm  "Imabikisou (Japan).iso" out/script.txt
imbk sns decrypt "Imabikisou (Japan).iso" out/st.sns.dec
imbk sn4 text    "Imabikisou (Japan).iso" out/sn4_lines.tsv   # older secondary script

# Translation patch, tested in RPCS3 (see docs/patching.md)
imbk unpack "Imabikisou (Japan).iso" out/disc
imbk patch  "Imabikisou (Japan).iso" out/my_translation.tsv out/disc
```

`<game>` can also be a folder that contains `PS3_GAME/` (a mounted disc or an extracted copy).

Files written by these commands contain game content. They are for your own use and are covered by
`.gitignore`. Never commit them.

### Line IDs

`imbk sns text` gives every line of the script a stable ID: the game's own message ID for dialogue
(`__1_17`), or `<scene label>#<n>` for narration (`S00A_0000_0100#2`). Translation files will refer to lines by ID only, so they can be shared without
including any of the original Japanese text.

## Supported release

| Title ID | Version | `st.sns` CRC32 | `scenario.sn4` CRC32 |
|---|---|---|---|
| BLJM60042 | 01.00 | `C83EFAFA` | `3E472406` |

## License

The code in this repository is MIT-licensed (see [LICENSE](LICENSE)). Imabikisō and all of its content belong to
their respective rights holders. This project is not affiliated with or endorsed by them.
