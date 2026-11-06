# AXIOM — Tactical Competitive Shooter

AXIOM est un FPS tactique compétitif natif (C++17 / OpenGL 3.3), pensé pour l'esport :
simulation serveur à **128 tick**, rendu à framerate illimité avec interpolation, entrée souris brute,
mécaniques de pur skill (sprays déterministes, contre-strafe, air-strafe, wallbangs) et profondeur
stratégique (économie, utilitaires, bombe, mi-temps, prolongations).

Le jeu tourne sans aucun fichier externe : la police est embarquée et **tous les sons sont synthétisés
en temps réel**. Un **launcher** se charge des mises à jour via les *Releases* GitHub.

---

## 1. Mise en route (Windows) — 10 minutes

1. Créez un dépôt GitHub (public ou privé) et poussez ce dossier :
   ```bash
   git init
   git add .
   git commit -m "AXIOM 1.0.0"
   git branch -M main
   git remote add origin https://github.com/<vous>/<depot>.git
   git push -u origin main
   ```
2. Publiez la première version :
   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```
3. L'onglet **Actions** compile automatiquement Windows + Linux (≈ 5 min), lance un match bot contre bot
   de vérification, puis crée la *Release* `v1.0.0` avec `AXIOM-windows-x64.zip`.
4. Téléchargez le zip, extrayez-le, lancez **`AxiomLauncher.exe`** → **JOUER**.
   (Windows SmartScreen peut avertir car l'exécutable n'est pas signé : *Informations complémentaires → Exécuter quand même*.)

Le launcher connaît déjà votre dépôt (il est inscrit à la compilation et dans `launcher.ini`).

## 2. Publier une mise à jour

```bash
# modifiez le code, puis ajoutez une section dans CHANGELOG.md :  ## [1.0.1]
git commit -am "Equilibrage"
git tag v1.0.1
git push && git push origin v1.0.1
```
La CI publie la release ; au prochain démarrage chaque launcher affiche **METTRE À JOUR**, télécharge le
paquet de sa plateforme, remplace le dossier `game/` et se met à jour lui-même si besoin.
Les notes de version du launcher viennent de la section correspondante de `CHANGELOG.md`.
Les réglages et statistiques des joueurs sont stockés hors du dossier du jeu
(`%APPDATA%\AXIOM` / `~/.local/share/axiom`) : une mise à jour ne les touche jamais.

---

## 3. Le jeu

### Modes
| Mode | Description |
|---|---|
| **Compétitif 5v5** | Attaquants vs Défenseurs, poser/désamorcer le *Noyau* sur le site A ou B. MR24 (13 rounds pour gagner, réglable), mi-temps avec changement de camp, prolongations MR3 à 10 000 $. Classement Elo contre les bots. |
| **Deathmatch** | Chacun pour soi, réapparition instantanée, armurerie gratuite, option *headshots uniquement*. |
| **Pratique** | Argent/munitions infinis, achat partout, **trajectoire des grenades affichée**, cibles sur les positions de défense. |
| **Visée** | Gridshot, Flick, Tracking (60 s), précision et temps de réaction, records sauvegardés. |
| **Multijoueur** | Héberger (serveur intégré) ou rejoindre par IP. Les humains remplacent les bots. **Serveur dédié** inclus. |

### Mécaniques
- **Mouvement type Source** : friction/accélération réalistes, contre-strafe (s'arrêter net rend précis),
  air-strafe, crouch-jump (caisses de 1,35 m accessibles seulement accroupi en l'air), marche silencieuse,
  pénalité d'atterrissage anti bunny-hop, ralentissement quand on est touché (*tagging*).
- **Tir** : 1re balle parfaite à l'arrêt, imprécision selon vitesse/saut/accroupi, **patterns de recul
  déterministes** propres à chaque arme (apprenables), récupération du recul, recul visuel réglable.
- **Dégâts** : tête ×4, torse ×1, jambes ×0,75, gilet/casque avec pénétration d'armure par arme,
  atténuation à distance, **traversée des murs** selon matériau (bois < béton < métal) et épaisseur.
- **Hitboxes** sphère/capsules, **compensation de latence** (rewind 64 ticks) en réseau.
- **Économie** : récompenses de victoire/défaite avec bonus de défaites consécutives, primes par classe d'arme,
  bonus de pose, revente pendant la phase d'achat, armes ramassables et lâchables pour un coéquipier.
- **Utilitaires** : flash (selon l'angle de vue), fumigène 18 s (bloque la vue des joueurs ET des bots),
  frag, incendiaire (éteinte par la fumée). Lancer fort / faible / moyen (clic gauche / droit / les deux).

### Arsenal
| Arme | Prix | Rôle |
|---|---|---|
| VIPER | 200 | pistolet de départ |
| MAMBA | 300 | pistolet perforant |
| HORNET | 700 | pistolet lourd, one-tap tête |
| WASP | 1200 | SMG mobile, prime 600 |
| BREACH | 1100 | pompe, prime 900 |
| RAPTOR | 2000 | fusil économique |
| VANGUARD | 2700 | fusil lourd, one-tap tête même casqué |
| SPECTER | 2900 | fusil précis, recul faible |
| LANCER | 1700 | fusil léger à lunette, précis à l'apogée du saut |
| ORACLE | 4700 | sniper, one-shot torse |
| TITAN | 1700 | mitrailleuse 150 balles |

### Bots (5 niveaux : Recrue → Élite)
Temps de réaction, vitesse de rotation, erreur de visée qui se resserre, préférence tête, contrôle du spray,
contre-strafe, tirs en rafale à distance, écoute des pas et des tirs, partage d'informations d'équipe,
achats selon l'économie de l'équipe, stratégies (exécution groupée sur un site, rotations, retake, désamorçage),
utilisation de fumigènes/flashs/grenades.

### Contrôles par défaut (positions physiques : ZQSD sur AZERTY)
| Action | Touche | Action | Touche |
|---|---|---|---|
| Déplacement | Z Q S D | Tirer / lunette | Clic G / Clic D |
| Sauter | Espace / molette bas | S'accroupir | Ctrl |
| Marcher (silencieux) | Shift | Recharger | R |
| Utiliser / désamorcer | E | Inspecter / lâcher | F / G |
| Armes | 1 2 3 4 5, A (dernière) | Flash · Fumi · Frag · Incendiaire | C · X · V · W* |
| Achat | B | Scores | Tab |
| Console | ² | Pause | Échap |

\*Toutes les touches sont réassignables (2 touches par action) dans **Paramètres → Touches**.

### Paramètres
Sensibilité (échelle 0,022°/count identique aux FPS compétitifs + calculateur cm/360), ratio zoom, FOV,
FOV/position de l'arme, réticule complet (4 styles, longueur, écart, épaisseur, contour, point, T, couleur,
suit le recul, **code de partage**), mode d'affichage, résolution, V-Sync, limite FPS, MSAA, luminosité,
daltonisme, surbrillance des ennemis, volumes par catégorie, spatialisation, HUD (échelle, couleur, opacité,
radar rotatif/zoom), net graph… Tout est aussi modifiable dans la **console** (`sensitivity 1.8`, `fps_max 400`,
`net_graph 1`, `crosshair AX-...`, `connect 1.2.3.4`, `help`).

---

## 4. Multijoueur

- **Héberger** depuis *Multijoueur* : votre partie devient un serveur (port UDP 27115 par défaut).
- **Rejoindre** : saisissez `ip` ou `ip:port`.
- **Serveur dédié** (sans fenêtre) livré dans `game/` :
  ```
  AxiomServer --port 27115 --mode comp --bots 3 --teamsize 5 --rounds 13
  AxiomServer --mode dm --bots 2
  ```
- Architecture : serveur autoritaire 128 tick, snapshots 64 Hz, prédiction côté client avec réconciliation,
  interpolation des autres joueurs (réglable), compensation de latence des tirs, flux d'événements fiable.
- Internet : ouvrez/redirigez le port UDP sur la box de l'hôte. (Pas encore de serveurs de matchmaking ni d'anti-cheat.)

---

## 5. Compiler soi-même

**Windows** : Visual Studio 2022 (charge « Développement desktop C++ ») + CMake.
```bat
cmake -S . -B build
cmake --build build --config Release
build\Release\AxiomGame.exe
```
**Linux** :
```bash
sudo apt install build-essential cmake libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev libasound2-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/AxiomGame
```
raylib 5.5 est téléchargé automatiquement par CMake. Test de simulation : `build/AxiomSimTest 2 2`
joue un match complet bot contre bot en ~1 s.

## 6. Organisation du code

```
src/common.h        constantes (128 tick), maths, RNG
src/world.*         simulation autoritaire : mouvement, armes, balistique, grenades, bombe, rounds, économie
src/map.*           carte CITADEL (grille 48×48), collisions, raycast/pénétration, A*, mesh
src/weapons.*       définitions des armes et patterns de recul
src/bot.*           IA des bots (produit des commandes comme un joueur)
src/net*            UDP, serveur, client prédictif
src/render.*        shader (éclairage, ombres exactes, brouillard, textures procédurales), modèles, arme en main, effets
src/hud.*           HUD, radar, killfeed, armurerie, tableau des scores
src/audio.*         synthèse sonore + audio 3D avec occlusion
src/ui.*            boîte à outils d'interface
src/game.*          écrans, boucle principale, console, entraînement
server/             serveur dédié
launcher/           launcher + mises à jour GitHub
tools/mapgen.py     éditeur de la carte (texte)
.github/workflows/  CI : build Windows/Linux, test, release
```

## 7. Limites actuelles & pistes

- Une seule carte (CITADEL) ; `tools/mapgen.py` permet d'en dessiner d'autres en texte.
- Graphismes stylisés procéduraux (blocs) : aucun modèle 3D ni texture importés pour l'instant.
- Pas de matchmaking en ligne, d'anti-cheat, de chat vocal ni de replays.
- Pas de signature de code Windows (avertissement SmartScreen).

## Licences
Code : à vous. Police Poppins : SIL Open Font License 1.1 (`assets/fonts/OFL.txt`). raylib : zlib.
