# Opcodes du script principal (`st.sns`)

Source : l'interpréteur de l'EBOOT (BLJM60042, déchiffré par RPCS3, analysé avec Ghidra 12.1.4).

Légende : ✅ confirmé par le code du handler · 🟡 hypothèse appuyée (chaînes, contexte, fréquences) · vide = inconnu.

## Comment l'interpréteur fonctionne

- **Table de dispatch** : `0x100AE740`, 165 entrées (opcodes `00`–`A4`), chacune une paire de 8 octets
  `{descripteur .opd, 0}` (pointeur de méthode C++). Elle est utilisée par `FUN_000b8140(ctx, op)`, qui appelle
  `table[op]`. `FUN_000b87f0` est une variante qui n'exécute qu'une liste de 19 opcodes (mode saut / reprise) et
  saute les autres.
- **Machine virtuelle** : `*0x003C14B8`. `vm+0x20` = PC, `vm+0x18` = fichier courant, `vm+0x28[i]` = les 60
  fichiers (`fichier+0x430` = offset du code), `vm+0x5D230` = mode saut rapide (la plupart des handlers ne font
  alors que passer l'instruction).
- **Valeur de retour d'un handler** : 0 = continuer, 1 = rendre la main jusqu'à la frame suivante. Les attentes
  (`5A`…) reculent le PC pour se réexécuter tant que la condition n'est pas remplie.

Fonctions utilitaires :

| Fonction | Rôle |
|---|---|
| `8a8a0(vm, n)` | lit un entier big-endian de n octets et avance le PC |
| `8ea40(vm)` | lit une chaîne terminée par `\0` |
| `8d7a8(vm)` / `8a8f0(vm, n)` | avance le PC de 1 / de n (n négatif = recule) |
| `8a788(vm, off)` / `8a808(vm, f, off)` | saut dans le fichier courant / dans le fichier f |
| `8a440(vm, i)` | lit le flag n° i (2 bits par flag) |
| `8a4b8(t, i)` | nom de la ressource i de la table t (0 images, 1 vidéos, 2 musiques, 3 sons) |
| `8ba18(vm, s)` | change l'état de l'interpréteur (4 = mode texte, 3/5 = fin de texte, 0x1E = fin de fichier…) |

## Conventions d'opérandes

- Sauf `01`, `18` et `19`, chaque instruction est `[op][u8 taille][charge utile]` ; la plupart des handlers
  sautent l'octet de taille (`8d7a8`) puis lisent leurs champs en **big-endian**.
- **Références de label** : `[u8 n° de fichier][nom\0]`. Le numéro désigne le fichier de script (0 =
  `scenario_top.xml`, 1 = `s00a_day1.xml`, … 57 = `common.xml`). Position dans la charge utile : 0 pour `49`, `4A`,
  `4D`, `50`, `51`, `91`, `92`, `93` ; 3 pour `62`, `6A` ; 5 pour `4B`, `4C` ; 20 pour `7E` (l'identifiant de
  réplique est un label local) ; après la liste de conditions pour `4E`. `imbk verify` contrôle les plus de
  13 000 références : toutes existent.
- **Conditions de `4E`** : une seule = `u16 flag, u8 comparaison, u8, u8 valeur` ; plusieurs = `s16 -n`
  (`FF FE` = 2 … `FF FB` = 5), puis n conditions de 6 octets (`4E, u16 flag, u8, u8, u8`) reliées par n−1 octets.
- **Ressources** : `40` image = `u16` index (table 0), `u8` calque, `u8` attente ; `8E` vidéo = `u16` index
  (table 1, masque 0x7FFF), `u16`, `u8` calque, `u8` attente ; `88` musique = `u16` index (table 2, bit 0x4000 =
  mode de lecture), 3 × `u16`, `u8`.

## Familles

| Opcodes | Famille | Indices |
|---|---|---|
| `01`, `02`, `12`, `17`–`1B`, `37`, `0D`–`36` (la plupart) | texte et fenêtre de texte | gestionnaire `*0x003C1208` |
| `40`, `60`, `61`, `90` | images | `PICTURE %04d`, fonctions `ae308` / `b0fd8` |
| `59`–`5B` | transitions de calques | gestionnaire `d6450` |
| `5C`–`5F` | effets (shaders) | gestionnaire `c3e58` |
| `70`–`87`, `7E` | sons, voix (cues `.csb`) | `sound/csbs/%s.csb`, `SeRandomTask`, `SeCountTask` |
| `88`–`8D` | musique | `BGM %04d`, `sound/bgm/%s` |
| `8E`, `8F`, `3D`, `3E` | vidéo (CRI Sofdec) | `MOVIE %04d`, `mwPly…` |
| `49`–`52`, `62`, `6A` | flux : labels, sauts, appels, conditions | `FlowchartScriptBoxMakeSub`, `GetHashBoxNo` |
| `96`–`98`, `1E` | scènes : début et fin, lieu, titre, case de l'organigramme | `ENV %04d`, `ScriptBox…` |
| `9D`–`A3` | à identifier (fonctions `4exxx`–`4fxxx`) | |

## Tous les opcodes

« Lectures » = champs lus par le handler, dans l'ordre (sans l'octet de taille). Certains handlers ne lisent une
partie des champs que sous condition. « Nb » = occurrences dans le script (le `02` n'est pas compté à part : il
termine chaque `01`).

| Op | Nom | Nb | Lectures | Chaînes atteintes |
|---|---|---|---|---|
| `00` |  | 0 |  |  |
| `01` | `text` ✅ | 21414 | u16 u24 |  |
| `02` | `text_end` ✅ | 0 |  |  |
| `03` |  | 505 |  |  |
| `04` |  | 505 |  |  |
| `05` |  | 27 |  |  |
| `06` |  | 27 |  |  |
| `07` |  | 0 |  |  |
| `08` |  | 0 |  |  |
| `09` |  | 0 |  |  |
| `0A` |  | 0 |  |  |
| `0B` |  | 1 |  |  |
| `0C` |  | 1181 |  |  |
| `0D` |  | 0 | u8 |  |
| `0E` |  | 0 | u8 |  |
| `0F` |  | 0 | u8 |  |
| `10` |  | 1713 |  |  |
| `11` |  | 0 | u16 u16 |  |
| `12` | `text_pos` ✅ (x, y) | 73 | u16 u16 |  |
| `13` |  | 2 | u8 u8 u8 |  |
| `14` |  | 0 | u8 u8 u8 |  |
| `15` |  | 2 |  |  |
| `16` |  | 0 | u8 u8 u8 |  |
| `17` | `wait` ✅ (clic) | 14317 |  |  |
| `18` | `ruby` ✅ | 27 | u8 u8 |  |
| `19` | `ruby_end` ✅ | 27 |  |  |
| `1A` | `newline` 🟡 | 9616 |  |  |
| `1B` | `page` 🟡 | 3744 |  |  |
| `1C` |  | 118 |  |  |
| `1D` |  | 57 |  |  |
| `1E` | `scene_box` 🟡 (organigramme) | 5750 |  | FlowchartScriptBoxMakeSub:JUMP Abnormal script_no[%d], GetHashBoxNo:HASH TBL NONE %s, GetScenejumphashBoxNo:HASH TBL NONE %s |
| `1F` | `narration` 🟡 | 2972 | u8 |  |
| `20` |  | 1 | u8 |  |
| `21` | `narration_end` 🟡 | 2972 |  |  |
| `22` |  | 0 | u8 |  |
| `23` |  | 0 | u8 |  |
| `24` |  | 110 | u8 u16 |  |
| `25` |  | 9 | u8 u16 |  |
| `26` |  | 110 |  |  |
| `27` |  | 75 | u8 |  |
| `28` |  | 0 | u16 |  |
| `29` | `delay` ✅ (frames) | 10646 | u32 |  |
| `2A` |  | 3 | u32 u32 |  |
| `2B` |  | 0 | u32 |  |
| `2C` |  | 1 | u32 |  |
| `2D` |  | 0 | u32 |  |
| `2E` |  | 1 | u32 |  |
| `2F` |  | 2 | u32 |  |
| `30` |  | 1 | u32 |  |
| `31` |  | 2 | u32 |  |
| `32` |  | 1 | u32 |  |
| `33` |  | 2 | u32 |  |
| `34` |  | 1 | u32 |  |
| `35` |  | 2 | u32 |  |
| `36` |  | 1 | u32 |  |
| `37` | `name` 🟡 (nom de personnage) | 2226 | u8 |  |
| `38` |  | 0 | u8 str str str |  |
| `39` |  | 5 | u8 str str |  |
| `3A` |  | 4 |  |  |
| `3B` |  | 4 |  |  |
| `3C` | `sys_sound` 🟡 | 2 |  | st_sys_001_sh |
| `3D` | `movie_pause` 🟡 | 4 | u8 | E1122604 mwPlyPause; handle is invalid., E2007 mwPlyPause; can't pause (%s) |
| `3E` | `movie_resume` 🟡 | 4 | u8 | E1122604 mwPlyPause; handle is invalid., E2007 mwPlyPause; can't pause (%s) |
| `3F` |  | 327 | u8 u8 |  |
| `40` | `picture` ✅ | 3353 | u8 u16 u16 u8 u8 | sp/ps3/s99a/s99a_9999_c01.dds, PICTURE %04d |
| `41` |  | 158 | u8 |  |
| `42` |  | 158 |  |  |
| `43` |  | 158 | u8 |  |
| `44` |  | 0 |  |  |
| `45` |  | 29 |  |  |
| `46` |  | 32 |  |  |
| `47` |  | 37 | u16 u8 u16 |  |
| `48` |  | 0 | u16 u8 u16 |  |
| `49` | `branch` ✅ | 1253 | u8 str u8 str | BoxEventPassageSet:jump Abnormal %s, FlowchartScriptBoxMakeSub:JUMP Abnormal script_no[%d], GetHashBoxNo:HASH TBL NONE %s |
| `4A` | `jump` ✅ | 517 | u8 str |  |
| `4B` |  | 168 | u8 u8 u8 |  |
| `4C` |  | 8 | u8 u8 u8 |  |
| `4D` | `label` ✅ | 5750 | u8 str |  |
| `4E` | `if_flag` ✅ | 404 | u16 u8 u16 u8 str u8 u16 u8 |  |
| `4F` |  | 0 | u16 u8 u16 u8 str |  |
| `50` |  | 6 | u8 str u8 str |  |
| `51` | `call` ✅ | 1713 | u8 str |  |
| `52` | `ret` ✅ | 478 |  |  |
| `53` |  | 144 | u16 |  |
| `54` |  | 17 | u16 |  |
| `55` |  | 476 | u8 str |  |
| `56` |  | 0 |  |  |
| `57` |  | 0 |  |  |
| `58` |  | 38 | u8 u16 |  |
| `59` | `transition_set` 🟡 | 3522 | u8 u8 u8 |  |
| `5A` | `transition_wait` ✅ | 3525 | u8 |  |
| `5B` | `transition_start` 🟡 | 3525 | u8 |  |
| `5C` |  | 2962 | u8 u8 u8 u8 |  |
| `5D` |  | 1442 | u8 u8 u8 u8 |  |
| `5E` |  | 999 | u8 u8 |  |
| `5F` |  | 3236 | u8 u8 |  |
| `60` | `picture_clear` 🟡 | 718 | u8 |  |
| `61` |  | 665 | u8 |  |
| `62` |  | 84 | u8 u16 u8 str |  |
| `63` |  | 0 |  |  |
| `64` |  | 84 |  |  |
| `65` |  | 84 |  |  |
| `66` |  | 0 | u8 u16 u8 str |  |
| `67` |  | 0 |  |  |
| `68` |  | 0 |  |  |
| `69` |  | 0 |  |  |
| `6A` |  | 107 | u8 u16 u8 str |  |
| `6B` |  | 0 |  |  |
| `6C` |  | 107 |  |  |
| `6D` |  | 108 |  |  |
| `6E` |  | 0 | u16 u16 u8 str |  |
| `6F` |  | 0 |  |  |
| `70` |  | 2138 | u8 u16 u16 u16 u16 u16 u16 u16 | /dev_bdvd, sound/csbs/%s.csb, csb%04d |
| `71` |  | 392 | u16 u16 u16 u16 u8 |  |
| `72` |  | 0 |  |  |
| `73` |  | 25 | u16 u16 u16 u8 |  |
| `74` |  | 166 | u16 u8 u8 |  |
| `75` |  | 1 | u16 u8 u16 u8 u16 u16 u16 u8 |  |
| `76` |  | 264 | u8 u32 u32 u32 u32 u32 u16 u16 | /dev_bdvd, sound/csbs/%s.csb |
| `77` |  | 0 | u16 u8 u8 u32 u8 u32 u16 u16 |  |
| `78` |  | 45 | u8 u16 u32 u32 u32 u16 u16 u16 | /dev_bdvd, sound/csbs/%s.csb, SeRandomTask |
| `79` |  | 70 | u16 u16 u16 u8 |  |
| `7A` |  | 0 | u16 u16 u8 |  |
| `7B` |  | 343 | u8 u16 u32 u32 u16 u16 u8 u16 | /dev_bdvd, sound/csbs/%s.csb, SeCountTask |
| `7C` |  | 36 | u16 u16 u16 u8 |  |
| `7D` |  | 0 | u16 u16 u8 |  |
| `7E` | `message` ✅ (+ voix) | 3518 | u8 u16 u16 u16 u16 u16 u16 u16 | /dev_bdvd, sound/bgm/%s, sound/csbs/%s.csb |
| `7F` | `message_end` 🟡 | 3518 |  |  |
| `80` |  | 3061 | u16 u16 u16 u16 u8 |  |
| `81` |  | 0 | u16 u16 u16 u8 |  |
| `82` |  | 61 | u16 u8 u8 u16 u16 u8 u16 u8 |  |
| `83` |  | 158 | u8 u16 u16 u32 u16 u16 u8 | /dev_bdvd, sound/csbs/%s.csb, csb%04d |
| `84` |  | 8 | u16 u16 u16 u8 |  |
| `85` |  | 6 | u16 u16 u8 |  |
| `86` |  | 83 | u8 u16 u8 u32 u8 u8 u16 u32 |  |
| `87` |  | 719 | u8 u16 u16 u16 u16 u16 u16 u8 | /dev_bdvd, sound/csbs/%s.csb, csb%04d |
| `88` | `bgm_play` ✅ | 301 | u8 u16 u16 u16 u16 u16 u8 | /dev_bdvd, sound/bgm/%s, sound/wave/%s |
| `89` |  | 266 | u16 u16 u16 u16 u8 | BGM %04d |
| `8A` |  | 59 | u16 u16 u8 |  |
| `8B` |  | 0 |  |  |
| `8C` |  | 86 | u16 u8 u16 u8 u16 u32 u16 u16 | BGM %04d |
| `8D` |  | 0 | u8 u16 u16 u16 u16 u16 u16 u16 | /dev_bdvd, sound/bgm/%s, sound/wave/%s |
| `8E` | `movie` ✅ | 530 | u16 u16 u16 u8 u8 | E1122604 mwPlyPause; handle is invalid., E1122605 mwPlySetOutVol: handle is invalid., E2007 mwPlyPause; can't pause (%s) |
| `8F` | `movie_stop` 🟡 | 539 | u8 u16 u16 u8 | E5090602 mwPlyRequestStop: handle is invalid., E602201 mwPlyGetPlayingHdrInf: handle is invalid. |
| `90` |  | 82 | u16 u8 u16 u8 |  |
| `91` |  | 54 | u8 str u8 |  |
| `92` |  | 55 | u8 str |  |
| `93` |  | 51 | u0 u8 str |  |
| `94` |  | 43 |  |  |
| `95` |  | 100 |  |  |
| `96` | `scene_start` 🟡 (lieu / ambiance) | 2308 | u0 u8 str | ENV %04d |
| `97` | `scene_end` 🟡 | 2308 |  |  |
| `98` | `scene_title` 🟡 | 638 |  |  |
| `99` |  | 147 |  |  |
| `9A` |  | 147 |  |  |
| `9B` |  | 60 |  |  |
| `9C` | `end` ✅ (fin de fichier) | 98 |  |  |
| `9D` |  | 112 | u8 u8 u16 u16 |  |
| `9E` |  | 86 | u16 u16 |  |
| `9F` |  | 31 |  |  |
| `A0` |  | 0 |  |  |
| `A1` |  | 87 |  |  |
| `A2` |  | 109 |  |  |
| `A3` |  | 83 |  |  |
| `A4` |  | 221 | u8 |  |

## Méthode (pour continuer)

Scripts Ghidra (headless) utilisés, à garder hors du dépôt avec les données : recherche des fonctions qui
utilisent une chaîne (via la TOC), table de dispatch, décompilation des 165 handlers et extraction de leurs
lectures d'opérandes. Le projet Ghidra doit être dans un chemin sans parenthèses (`analyzeHeadless.bat` ne les
gère pas). Langage : `PowerPC:BE:64:A2ALT-32addr`.