# Moteur (prototype)

## Lancer

```sh
imbk_player "Imabikisou (Japan).iso"                    # démarre à S00A_0000_0100 (salle de cours)
imbk_player "Imabikisou (Japan).iso" S00A_0015_0100     # n'importe quel label de scène
```

Clic, Entrée ou Espace : suite. Ctrl maintenu : avance rapide. Échap : quitter. La fenêtre fait 1280×720 (la
résolution du jeu) et peut être redimensionnée.

Mode test, sans fenêtre : `--shot capture.bmp --frames N` fait tourner N frames (60 par seconde) en cliquant
automatiquement une seconde après chaque attente, puis enregistre l'image. C'est ce qui permet de vérifier le
rendu sans lancer le jeu à la main.

## Architecture

| Partie | Fichiers | Rôle |
|---|---|---|
| core | `src/core/` | lecture du disque (ISO, AFS), déchiffrement et décodage du script |
| engine | `src/engine/` | indépendant de la plateforme : machine virtuelle du script (`Player`), décodage DDS (DXT1/3/5) |
| front-end | `src/player/main_win32.cpp` | fenêtre, rendu GDI, police, entrées (Windows uniquement pour l'instant) |

Le front-end Win32/GDI a été choisi pour démarrer sans aucune dépendance externe. Le moteur ne dépend pas de lui :
un front-end SDL3 (multiplateforme, avec le son) pourra le remplacer.

`Player` exécute le script une frame à la fois et expose ce qu'il faut afficher : les calques (image courante,
image précédente pendant un fondu, légende), la page de texte, et l'état d'attente. Les images sont chargées
depuis `scriptdatachunk.afs` (le nom `x.psd` du script correspond à l'entrée `x.dds`) et mises à l'échelle 1280×720.
La police du texte est `sn4_p03.TTF`, lue dans `systemchunk.afs` du disque de l'utilisateur.

## Ce qui est géré

| Opcodes | Comportement |
|---|---|
| `01`, `1A`, `17`, `1B`, `12` | texte, retour à la ligne, attente de clic, fin de page, texte positionné |
| `18`, `19`, `37` | furigana (pas encore affichées), nom de personnage (remplacé par sa lecture en furigana) |
| `7E`, `7F` | une réplique occupe ses propres lignes |
| `29` | attente en frames |
| `40`, `59`, `5A`, `5B`, `60` | image dans un calque, réglage du fondu (`u16` durée), début du fondu, effacement |
| `8E`, `8F` | vidéo : écran noir avec le nom de la vidéo (pas encore décodée) |
| `88` | musique : le nom est affiché en bas de l'écran (pas encore jouée) |
| `4D`, `4A`, `49`, `51`, `52`, `9C` | labels, sauts, appels et retours de sous-programme, fin de fichier |

Les autres opcodes sont ignorés et comptés ; la liste s'affiche à la fermeture (`not implemented yet: …`).

## Limites connues

- Pas de son ni de vidéo.
- Les conditions (`4E`) ne sont pas évaluées (jamais vraies) et `49` prend toujours la première branche : pour
  l'instant on suit l'histoire de façon linéaire depuis un label.
- Les effets (`5C`–`5F`, `61`) et la plupart des réglages de la fenêtre de texte (`0D`–`36`) sont ignorés.
- Position et taille du texte mesurées sur une capture de la version PS3 (colonne de 23 caractères à x = 131,
  interligne de 66 px, police de 44 px) : à affiner.
