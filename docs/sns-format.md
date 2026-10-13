# Script principal `st.sns` (CSD-CSCNV 01.68)

Source : `PS3_GAME/USRDIR/data/chunk/residentchunk.afs`, entrée `st.sns` (2 416 367 octets, CRC32 `C83EFAFA`
sur BLJM60042 v01.00).

C'est **le vrai script du jeu**. L'EBOOT charge les fichiers `data/script/*.sns|.flw|.ara` et contrôle la version
(`SCRIPT ENGINE %s != SNS DATA %s`, moteur `01.68`). `scenario.sn4` (ScenarioLink, labels `AMI_…`) n'est
référencé nulle part dans l'EBOOT : c'est un fichier secondaire, dont le rôle reste à déterminer.

## Chiffrement

Retrouvé dans le chargeur de l'EBOOT (fonction `0x00090418`, analysée avec Ghidra sur l'EBOOT déchiffré par RPCS3).

- Les 16 premiers octets sont à la fois le magic et la clé : `"CSD-CSCNV:01.68\0"`.
- Chaque octet suivant est stocké sous la forme `clair[i] + clé[i % 16]` (mod 256).
- Déchiffrement : `data[i] -= data[i % 16]` pour `i >= 16`.

Comme le texte clair contient beaucoup de zéros, la clé apparaît telle quelle à de nombreux endroits du fichier
chiffré. C'est ce qui l'avait trahie.

Implémentation : `src/core/sns.cpp` (`decrypt` / `encrypt`), et `imbk sns decrypt <game> <out>`.

## Conteneur déchiffré

Statut : **structure entièrement décodée et validée** sur BLJM60042. Les 60 fichiers se lisent jusqu'au dernier
octet, toutes les cibles de labels tombent sur une instruction, chaque fichier se termine par `9C`, et les
identifiants de ligne sont uniques. `imbk verify` contrôle tout ça.

Les champs sont en little-endian.

| Offset | Contenu |
|---|---|
| `0x00` | clé / magic `"CSD-CSCNV:01.68\0"` (en clair) |
| `0x14` | `u32` = 0x55 (?) ; `0x18` : `u16` 0x3A, `u16` 0x3B (?) |
| `0x20` | `u32` offset d'une table de paramètres inconnue (`0x224228`) |
| `0x28` | `u32` table + `u32` pool : 8 contrôles AISAC (`Globals/Aisac/…`) |
| `0x60` | 4 × (`u32` table, `u32` pool) : images (1 270 `.psd`), vidéos (342 `.sfd`), musiques (176 `.adx`), sons (5 223 cues `.csb`) |
| `0xE0` | label de départ, ASCII (`TOP_0000_0100`) |
| `0x100` | `u32` nombre de fichiers (60), puis 60 × (`u32` offset absolu, `u32` taille) |

Une table de chaînes est un tableau de `u32` (offsets dans le pool) immédiatement suivi du pool de chaînes
terminées par `\0`. Le nombre d'entrées vaut `(pool - table) / 4`, et il correspond exactement au nombre d'entrées
des archives AFS correspondantes. Le bytecode désigne donc les ressources par leur index.

### Fichiers de script

Chaque fichier (`scenario_top.xml`, `s00a_day1.xml` … `common.xml`, `s09a.xml`) a la même structure qu'un bloc
`SCR-file` de `scenario.sn4` (code commun : `src/core/scr.cpp`) :

- `+0x00` : nom d'origine (0x30 octets) ;
- `+0x30` : 256 buckets `u32` (relatifs au début du fichier, 0 = vide), puis `u32` offset du code ;
- entrées de labels chaînées : `u16 next` (= taille de l'entrée, 0 = fin de chaîne), `u16 tag` (`"No"`),
  `u32 cible` (relative au code), nom ASCII terminé par `\0` et complété à 4 octets. Dans `scenario.sn4`, les
  entrées font toujours 24 octets ; ici leur taille varie (noms jusqu'à 31 caractères) ;
- le bytecode, suivi de 0 à 3 octets `00` de remplissage (alignement sur 4).

### Instructions

Toutes les instructions ont la forme `[op][u8 taille][données]`, sauf :

| Op | Forme | Rôle |
|---|---|---|
| `01` | `01 <UTF-8> 02` | morceau de texte (terminé par `02`, qui ne peut pas apparaître dans de l'UTF-8) |
| `18` | `18` | début de furigana : les morceaux `01` suivants sont la lecture… |
| `19` | `19` | …fin de la lecture ; le morceau `01` suivant est le texte de base |

> **La référence complète des 165 opcodes, tirée du code de l'interpréteur de l'EBOOT, est dans
> [opcodes.md](opcodes.md).** Le tableau ci-dessous est la première analyse, faite uniquement à partir des
> données ; il est conservé pour l'historique. Une correction importante : l'octet qui précède un nom de label
> n'est pas un « flags » mais le **numéro du fichier de script** cible.

Opcodes identifiés à partir des données seules :

| Op | Nb | Hypothèse |
|---|---|---|
| `4D` | 5 750 | label : `u8` flags + nom ; la cible pointe juste après |
| `1E` | 5 750 | suit toujours un label |
| `7E` / `7F` | 3 518 / 3 518 | début / fin de réplique ; la charge utile se termine par `01` + identifiant (`__1_17`), unique dans tout le jeu |
| `1F` / `21` | 2 972 / 2 972 | début / fin d'un bloc de narration |
| `17` | 14 317 | attente de clic (fin de phrase) |
| `1A` | 9 616 | retour à la ligne |
| `1B` | 3 744 | fin de page |
| `37` | 2 226 | insère le nom du personnage `u8` (souvent précédé de sa lecture en furigana : `<ruby:…><name:00>`) |
| `29` | 10 646 | attente (`u32` BE, en frames : `00 00 00 3C` = 60) |
| `96` / `97` | 2 308 / 2 308 | paire ouverture / fermeture (?) |
| `9C` | 60 | fin du fichier de script (dernière instruction de chacun des 60 fichiers) |

## Lignes à traduire (`imbk sns text`)

Une ligne est une suite de morceaux de texte et de codes intégrés, qui se termine par `17` (attente), `1B`
(page), `7F` ou `21` (fin de bloc), un nouveau label, ou un code non textuel hors bloc. Au total : **19 018 lignes**.

- Identifiant : celui de la réplique (`__1_17`, puis `__1_17#1`… si elle contient plusieurs attentes), ou bien
  `<label>#<n>` pour la narration (par exemple `S00A_0000_0100#2`).
- Rendu : `\n` pour `1A`, `<ruby:lecture>` avant le texte de base, `<name:XX>`, et `<XX:hex>` pour les autres codes
  (`29` attente, `3F` …).

## Observations

- Le texte de `st.sns` est une version **retravaillée** de celui de `scenario.sn4` : mêmes scènes, phrases
  reformulées. `scenario.sn4` est probablement un ancien brouillon resté sur le disque.

## À faire

- Sémantique des opcodes restants : voir [opcodes.md](opcodes.md).
- Exporter aussi les titres de scène (`98`, en UTF-8 dans la charge utile) pour la traduction.
- Table de paramètres à `0x20`, champs `0x14`–`0x1B`.
- Formats de `st.flw` (organigramme) et `st.ara`.

## Fichiers compagnons dans `residentchunk.afs`

| Fichier | Contenu |
|---|---|
| `st.flw` | organigramme des scènes, labels `S0xA_…` en clair (≈ 9 000 occurrences) |
| `st.ara` | table d'offsets + labels (≈ 340) |
| `scenario_pt.csv` | choix : `PAGE_LABEL`, points par route (S00A…S03B), texte du choix (Shift-JIS) |
| `passage_flag.csv`, `flag.txt` | définition des flags (Shift-JIS, avec commentaires) |
| `text_jap_utf8.ddd` | table de caractères (UTF-8), probablement pour la police |
