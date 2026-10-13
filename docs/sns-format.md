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

## Contenu déchiffré (premières observations)

- Le texte est en **UTF-8**, pas en Shift-JIS : environ 313 000 caractères japonais (`scenario.sn4` en a environ
  4,5 fois moins).
- Il contient environ 11 000 occurrences de labels de scène `S0xA_xxxx_…` en ASCII
  (par exemple `S00A_0000_PROLOGUE_0600`), cohérents avec `st.flw` et les noms des vidéos (`s00a_0220_m12.sfd`).
- Les lignes de dialogue sont précédées d'identifiants courts du type `__1_2`.
- L'en-tête (après les 16 octets de clé) contient des `u32` little-endian qui ressemblent à des tables d'offsets
  (par exemple `0x20: 0x00224228`).

À faire : décoder l'en-tête et le bytecode (la même méthode que pour `scenario.sn4` devrait marcher : en-tête,
table, puis désassemblage itératif), et déterminer le format de `st.flw` (organigramme) et de `st.ara`.

## Fichiers compagnons dans `residentchunk.afs`

| Fichier | Contenu |
|---|---|
| `st.flw` | organigramme des scènes, labels `S0xA_…` en clair (≈ 9 000 occurrences) |
| `st.ara` | table d'offsets + labels (≈ 340) |
| `scenario_pt.csv` | choix : `PAGE_LABEL`, points par route (S00A…S03B), texte du choix (Shift-JIS) |
| `passage_flag.csv`, `flag.txt` | définition des flags (Shift-JIS, avec commentaires) |
| `text_jap_utf8.ddd` | table de caractères (UTF-8), probablement pour la police |
