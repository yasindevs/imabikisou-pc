# Patcher le texte et tester dans RPCS3

## Marche à suivre

```sh
# 1. Copier le disque dans un dossier que RPCS3 peut lancer (≈ 9,6 Go, une seule fois)
imbk unpack "Imabikisou (Japan).iso" out/disc

# 2. Exporter les lignes, puis éditer une copie (ne garder que les lignes modifiées suffit)
imbk sns text "Imabikisou (Japan).iso" out/lines.tsv

# 3. Appliquer les traductions au dossier (toujours à partir de l'ISO d'origine)
imbk patch "Imabikisou (Japan).iso" out/ma_traduction.tsv out/disc

# 4. Lancer le jeu
rpcs3.exe "out/disc/PS3_GAME/USRDIR/EBOOT.BIN"
```

`imbk patch` ne réécrit que `out/disc/PS3_GAME/USRDIR/data/chunk/residentchunk.afs`. Pour revenir au texte
d'origine, il suffit de relancer `imbk patch` avec un fichier vide.

## Format du fichier de traduction

Le fichier est en UTF-8 et contient une ligne par entrée : `<id>`, une tabulation, puis le texte. Les lignes vides
et celles qui commencent par `#` sont ignorées. Les lignes absentes du fichier gardent leur texte d'origine.

Dans le texte :

| Écriture | Effet |
|---|---|
| `\n` | retour à la ligne (instruction `1A`) |
| `\t`, `\\` | tabulation, barre oblique inverse |
| `<ruby:lecture>` | furigana, placé avant le texte qu'il annote |
| `<name:00>` | insère le nom du personnage n° 00 (`00` = le héros) |
| `<XX>` / `<XX:hex>` | n'importe quelle autre instruction (pauses `<29:…>`, `<3F:…>`…) : à conserver telle quelle |

Un `<` qui n'ouvre pas une balise valide est écrit tel quel.

## Ce que vérifie `imbk patch`

Le patch n'est écrit **que si tout est cohérent** :

- chaque identifiant existe ;
- le script modifié se relit, et tous les labels pointent sur une instruction ;
- chaque ligne se relit **exactement** comme dans le fichier de traduction. Par exemple, une balise `<17>`
  (attente) ajoutée au milieu d'une ligne la couperait en deux et décalerait les identifiants : c'est refusé ;
- la taille du script ne dépasse pas le tampon du jeu (0x2EE000 = 3 072 000 octets, d'après le chargeur de l'EBOOT).
  Le script d'origine fait 2 416 367 octets, ce qui laisse environ 640 Ko de marge.

En interne : les lignes modifiées sont réencodées, les cibles des labels, les tailles des 60 fichiers, le
répertoire du conteneur et les pointeurs vers les tables de ressources sont recalculés, puis le script est
rechiffré et `residentchunk.afs` est reconstruit avec la même disposition (reconstruit sans modification, il est
identique à l'original octet pour octet).

## Démarrer directement à une scène (tests)

```sh
imbk patch "Imabikisou (Japan).iso" out/test.tsv out/disc --start S00A_0000_0000
```

Le script de démarrage (`TOP_0000_0100` dans `scenario_top.xml`) initialise les réglages, puis saute (`4A`) à
l'aiguillage `S00A_0000_PROLOGUE_GOAROUND_CHK`. Celui-ci prépare la scène (`96`, `1E`, `4E`), puis se branche
(`49`) soit vers `S00A_0000_0000` (prologue sauté), soit vers `S00A_0000_PROLOGUE_0100` (prologue joué).
`--start` redirige uniquement cette seconde branche vers le label choisi : le jeu passe donc par sa propre
préparation. Les flags normalement posés par les scènes sautées ne le sont pas : c'est fait pour tester le texte,
pas pour jouer. Sans `--start`, le jeu retrouve son démarrage normal.

> Première version (abandonnée) : rediriger directement le saut `4A` du script de démarrage. Écran noir après le
> logo Sega, sans erreur dans le log : la scène démarrait sans la préparation de l'aiguillage.

## Résultats des tests dans RPCS3

Test 1 (prologue, 14/10/2026) :

- ✅ Le patch fonctionne : le texte remplacé s'affiche, et le jeu enchaîne normalement sur les scènes suivantes.
- ✅ L'ASCII s'affiche.
- ❌ Les accents et les guillemets (`é è à ç ù ô « »`) n'apparaissent pas. Le prologue utilise la police
  calligraphique `hgrgy.ttc` (HG行書体), qui n'a pas ces glyphes. La police de la boîte de texte normale,
  `sn4_p03.TTF` (HGP創英プレゼンスEB, version modifiée), les possède (affichage pas encore vérifié, voir test 2).
- ⚠️ Dans les scènes « cinématiques », chaque phrase est placée à des coordonnées fixes par l'instruction `12`
  (`u16 x, u16 y`, en BE), qui précède le bloc `1F`. Une traduction plus longue que l'original est coupée sur
  deux lignes et chevauche la phrase suivante. Il faudra pouvoir ajuster ces positions, ou garder ces phrases
  courtes.

Test 2 (boîte de texte normale, 14/10/2026), testé ligne par ligne en lançant RPCS3 automatiquement :

- ✅ `--start` seul : démarrage normal (écran de sauvegarde automatique, puis nouvelle partie).
- ✅ Les lignes contenant de l'anglais long, `é è ê à â ç ù û ô ï œ « » — …` et des `\n` explicites ne gênent pas le
  démarrage.
- ❌ La ligne contenant des caractères **pleine chasse** (`ＡＢＣ ａｂｃ １２３`) rend le démarrage beaucoup plus
  lent : écran noir après le logo Sega, sauvegarde système chargée vers 28 s au lieu de 14 s, écran de sauvegarde
  automatique visible au bout d'environ 95 s. Le jeu semble traiter tout le texte du script au démarrage. Cause
  exacte non déterminée ; à éviter dans les traductions pour l'instant.
- L'affichage du texte dans la boîte n'a pas encore été vérifié.

Points encore à vérifier :

- le retour à la ligne automatique et les accents dans la boîte de texte normale ;
- le découpage en lignes (sur `17` attente, `1B` page…), qui reste une hypothèse.
