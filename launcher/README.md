# Imabikiso PC launcher

`ImabikisoLauncher.exe` prepares and starts the native port from **your own**
copy of the game (PS3, BLJM60042). It contains no game data.

## What it does

1. **Your game**: choose your disc image (`.iso`). The launcher checks that it
   is the supported release (`imbk verify`) and extracts it (`imbk unpack`,
   about 9 GB, resumable) into the data folder.
2. **Executable**: `EBOOT.BIN` is decrypted once with your own RPCS3
   (`rpcs3.exe --decrypt`), or you can point at an already decrypted
   `EBOOT.ELF`. It must match the supported SHA-256.
3. **Language**: Japanese (original) or the English draft. English builds a
   view of the extracted disc where only the main script archive is patched
   (folder junctions and hard links, nothing copied), and patches the menu
   labels of the executable from `lang/en/menu.tsv` (offsets only, no original
   text).
4. **Play**: starts `game/ImabikisoPC.exe` with the settings (VSync, mute).
   Each language has its own profile folder, so saves and settings stay
   separate: `profiles/<language>/gamedata`.

Without a window, for scripts and testing (uses `launcher.ini`):

```bat
ImabikisoLauncher.exe --install > install.log
ImabikisoLauncher.exe --play
```

## Folder layout

```
ImabikisoLauncher.exe
imbk.exe
game/ImabikisoPC.exe        the port
lang/en/story.tsv           English script (from translations/en)
lang/en/menu.tsv            English menu labels
launcher.ini                settings (created)
data/                       extracted disc, decrypted executable, languages (created)
profiles/<language>/        saves and game settings (created)
logs/                       game output of the last run (created)
```

`lang/en/story.tsv` is `translations/en/story.tsv`, produced by
`tools/export_translation.py --wrap ... --layout ...` (see
[translations/README.md](../translations/README.md)).

## Build

Part of the top-level CMake project on Windows (target `ImabikisoLauncher`).
UI: [Dear ImGui](https://github.com/ocornut/imgui) 1.91.9 (MIT) on Direct3D 11,
in `third_party/imgui`. The static C runtime is used, so no Visual C++
redistributable is needed for the launcher and `imbk.exe`.
