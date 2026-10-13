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
| Scenario script `scenario.sn4`: full bytecode decoding, label table, line export | ✅ structure, 🚧 opcode semantics |
| Engine (rendering, audio, video, choices) | ⏳ not started |
| Translation patch format | ⏳ not started |

Format notes: [docs/sn4-format.md](docs/sn4-format.md) (in French for now).

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
imbk script text   "Imabikisou (Japan).iso" out/lines.tsv
imbk script disasm "Imabikisou (Japan).iso" out/scenario.txt
```

`<game>` can also be a folder that contains `PS3_GAME/` (a mounted disc or an extracted copy).

Files written by these commands contain game content. They are for your own use and are covered by
`.gitignore`. Never commit them.

### Line IDs

`imbk script text` gives every line of the script a stable ID, `<scene label>#<n>` (for example
`AMI_0000_0200#2`). Translation files will refer to lines by ID only, so they can be shared without
including any of the original Japanese text.

## Supported release

| Title ID | Version | `scenario.sn4` CRC32 |
|---|---|---|
| BLJM60042 | 01.00 | `3E472406` |

## License

The code in this repository is MIT-licensed (see [LICENSE](LICENSE)). Imabikisō and all of its content belong to
their respective rights holders. This project is not affiliated with or endorsed by them.
