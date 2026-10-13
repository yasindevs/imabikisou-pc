# Format `scenario.sn4` (ScenarioLink 3.25)

Source : `PS3_GAME/USRDIR/data/chunk/scriptchunk.afs`, entrée 0 (`scenario.sn4`, 215 064 octets).
L'entrée 1 (`scene_pk.spk`) a le même en-tête, en version « ScenePack » (voir plus bas).

Release de référence : **BLJM60042**, v01.00, CRC32 de `scenario.sn4` = `3E472406` (`imbk verify` le vérifie).

Statut : **structure entièrement décodée** (les 14 236 instructions se lisent d'un bout à l'autre du fichier sans erreur, et les 858 labels pointent tous au bon endroit). **Sémantique partielle** : la taille de chaque opcode est connue, mais le rôle de plusieurs d'entre eux reste à confirmer, au désassemblage de l'EBOOT ou en jouant au jeu dans RPCS3.

## Vue d'ensemble

| Offset | Taille | Contenu |
|---|---|---|
| `0x000` | 0x20 | `"ScenarioLink\0\0\0\0"` + `":Ver:3.25.2.157\0"` |
| `0x020` | 0x40 | `"start_label"` (padding à zéro) |
| `0x060` | 0x40 | `"edit_start_label"` (padding à zéro) |
| `0x0A0` | 0x0C | `u32 count = 1`, `u32 offset = 0xAC`, `u32 size` (offset + size = taille du fichier) |
| `0x0AC` | 0x30 | `"SCR-file:Ver:3.25.2.157\0"` + `"****************"` + padding |
| `0x0DC` | 256 × 4 | table de hachage des labels (`u32`, relatifs à `0xAC`, 0 = bucket vide) |
| `0x4DC` | 4 | `u32` offset du code, relatif à `0xAC` (→ `0x5690`) |
| `0x4E0` | … | entrées de labels (24 octets chacune, chaînées) |
| `0x5690` | → fin | bytecode (il commence par `2F 30` / `31 32`, deux instructions à argument `u8`) |

Les champs de l'en-tête et de la table sont en **little-endian**, mais les arguments numériques du bytecode
sont en **big-endian** (par exemple `50 00 00 00 3C` = 60).

## Table des labels

256 buckets (le `u32` qui suit la table est l'offset du code, pas un bucket). Chaque entrée :

```
u16  next      // delta vers l'entrée suivante du même bucket (0 = fin de chaîne), en pratique 0x18
u16  tag       // 0x6F4E ("No") = label de scène ; 0xFFFF = label local "_AMIn"
u32  target    // offset dans le code, relatif à 0x5690, qui pointe JUSTE APRÈS l'instruction 0x85 du label
char name[16]  // ASCII, terminé par \0, ex. "AMI_0000_0110"
```

Il y a 858 labels de scène `AMI_xxxx_yyyy` (`xxxx` = chapitre/scène, `yyyy` = sous-section), plus une
vingtaine de labels locaux `_AMI0`…`_AMI19` utilisés par les choix.

## Bytecode

La taille de chaque instruction se déduit uniquement de son opcode :

| Plage | Taille | Notes |
|---|---|---|
| `0x02` | jusqu'à `0x03` inclus | **ligne de texte** en Shift-JIS (cp932), sans code de contrôle inline repéré pour l'instant |
| `0x00`–`0x1F` (sauf `02`) | 1 | sans argument |
| `0x22` | 1 | sans argument ; suit toujours un `0x85` (début de bloc ?) |
| `0x2F`–`0x3F` | 2 | `op u8` |
| `0x40` | 2 | `op u8` (0/1, interrupteur on/off ?) |
| `0x4C` | 3 | `op u16` |
| `0x50`–`0x5F` | 5 | `op u32` BE (durées en frames ? 0x3C = 60, 0x78 = 120…) |
| `0x66` | 5 | `op u16 x, u16 y` BE ? (texte positionné à l'écran) |
| `0x60`–`0x8F` (sauf `66`) | 2 + n | `op u8 n` puis `n` octets de payload |

### Opcodes observés (fréquence)

| Op | Nb | Hypothèse |
|---|---|---|
| `02` | 3 893 | texte (une ligne/phrase) |
| `09` | 3 080 | fin de phrase / attente de clic |
| `04` | 2 315 | retour à la ligne / paragraphe |
| `0B` | 740 | fin de page / effacement de la fenêtre de texte |
| `85` | 858 | déclaration de label de scène : payload = `00` + nom ASCII `\0` |
| `22` | 858 | toujours juste après `85` |
| `50` | 797 | attente (frames) |
| `73` | 692 | image de fond : nom `AMI0000C01A` (→ `scriptdatachunk.afs`) |
| `74` | 692 | variante d'image : `AMI0000C01A_00` (→ `_efc` / `_thumb` ?) |
| `40` | 76 | `u8` 0/1 |
| `66` | 71 | position du texte (x, y) |
| `6A` | 31 | référence de label : payload = `00` + nom (goto, ou marqueur de label local `_AMIn`) |
| `5C`, `5A`, `32`, `15` | 19–29 | séquence d'effet récurrente : `5A t; … 32 15; 5C t; 15` |
| `16` | 11 | précède un `6A` vers un label de scène (fin de branche + goto ?) |
| `4C` | 12 | `u16` |
| `70` | 9 | choix ? payload : `00 00 07 00 nn 00` + label local `_AMIn` |
| `6C` | 5 | branchement multiple : `01 count 00 00 00` + `count` noms de labels séparés par `00 00` |
| `68` | 3 | ? |
| `00` | 3 | ? |
| `57`, `2F` | 1 | ? |

## Identifiants de lignes pour la traduction

Proposition : `<label de scène>#<n>`, où `n` est l'index (à partir de 0) de l'instruction `0x02` à
l'intérieur du label (par exemple `AMI_0000_0200#2`). Ces identifiants sont stables, lisibles et ne
contiennent aucun texte du jeu. Les fichiers de traduction peuvent donc se publier sans le japonais.

## `scene_pk.spk` (ScenePack)

Même en-tête `ScenarioLink`, puis `"ScenePack"` et une table de hachage. Ses entrées sont de taille
variable : `u16 next, u16 ?, u32, u32, nom` (noms d'images du type `AMI0070C31`). C'est probablement un
index image → position dans le script, pour la galerie ou l'organigramme. Pas encore analysé en détail.

## Implémentation

Le parseur se trouve dans `src/core/sn4.cpp`. `imbk script disasm` produit un listing complet et
`imbk script text` exporte les lignes avec leurs identifiants.
