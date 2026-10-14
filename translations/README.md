# English machine-translation preview

The planned English edition uses machine translation assisted by AI. This directory currently holds **33 draft script entries**, not a complete localization. The local native preview also has 27 menu/dialog replacements; their runtime packaging is not yet a portable public workflow.

`en/preview.json` maps stable script IDs to English text only. It does not include the original Japanese script. Literal `\n` sequences are script line breaks, and tags such as `<3F:02FF>` must be preserved.

Export the preview to the patcher's UTF-8 TSV format:

```sh
python tools/export_translation.py translations/en/preview.json out/english.tsv
imbk unpack "Imabikisou (Japan).iso" out/english-disc
imbk patch "Imabikisou (Japan).iso" out/english.tsv out/english-disc
```

This prepares the story archive in a separate disc directory. It does not translate menu data in the ELF or build the native executable. For native testing, use a separate working directory for saves/settings as well.

The patcher validates IDs, script round-trip, labels and size. These checks cannot guarantee good English, correct context or visual layout. The engine wraps Japanese by character, so English lines need explicit word-aware breaks or short wording. The prologue font has limited accented Latin coverage.

Next: glossary, context-aware batching, broader machine translation, UI inventory and in-game review. Human corrections are welcome. Draft-entry coverage and reviewed translation quality must be tracked separately.
