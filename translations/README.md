# English machine-translation preview

The planned English edition uses machine translation assisted by AI. This directory currently holds **33 draft script entries**, not a complete localization. The local native preview also has 27 menu/dialog replacements; their runtime packaging is not yet a portable public workflow.

`en/preview.json` maps stable script IDs to English text only. It does not include the original Japanese script. A literal `\n` is the script's newline instruction (`1A`), and tags such as `<3F:02FF>` must be preserved. Keep `\n` only at the start of an entry, where the original has it: the engine starts a new line there, but ignores the instruction in the middle of a sentence and glues the two words together.

Export the preview to the patcher's UTF-8 TSV format:

```sh
python tools/export_translation.py translations/en/preview.json out/english.tsv
imbk unpack "Imabikisou (Japan).iso" out/english-disc
imbk patch "Imabikisou (Japan).iso" out/english.tsv out/english-disc
```

This prepares the story archive in a separate disc directory. It does not translate menu data in the ELF or build the native executable. For native testing, use a separate working directory for saves/settings as well.

The patcher validates IDs, script round-trip, labels and size. These checks cannot guarantee good English, correct context or visual layout. The engine wraps by glyph width, not at word boundaries, so a long English line can be cut inside a word. `export_translation.py --wrap` fixes that by padding the end of a line with spaces until the next word moves to the next line whole:

```sh
imbk sns layout "Imabikisou (Japan).iso" out/layout.tsv
python tools/export_translation.py translations/en/preview.json out/english.tsv --wrap font_adv.log --layout out/layout.tsv
```

`font_adv.log` holds the dialogue font's advance widths, measured on your machine by running the native port once with `FONT_ADV_DUMP=1` (keep the `[font-adv] ... scale=48.00` line). `imbk sns layout` tells where each line starts (new page, new line, or continuing the previous text). The width model was fitted on 15 observed breaks: advances rounded up to whole pixels, 24 px per space, a 1022.5 px line. When one extra space is needed to force the break, the next line starts with a one-space indent. The prologue font has limited accented Latin coverage.

Next: glossary, context-aware batching, broader machine translation, UI inventory and in-game review. Human corrections are welcome. Draft-entry coverage and reviewed translation quality must be tracked separately.
