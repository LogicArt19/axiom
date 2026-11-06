# AXIOM — Hero Strategy

AXIOM est un jeu de stratégie compétitif natif (C++17 / OpenGL 3.3) qui mélange **StarCraft II** et
**League of Legends** : chaque joueur contrôle **un héros** (compétences, niveaux, objets) **et une armée**
(base, drones, deux ressources, bâtiments, améliorations, expansions). Pas de navigateur, pas de moteur
tiers : la simulation tourne à 30 ticks/s, le rendu à framerate libre avec interpolation, la police est
embarquée et tous les sons sont synthétisés en temps réel. Un **launcher** installe les mises à jour
depuis les *Releases* GitHub.

## Contenu

- **4 cartes** (rotation ou au choix) : *Faille d'Obsidienne* (ouest/est), *Plateaux Jumeaux* (nord/sud),
  *Cratère de Vesta* (bases décalées), *Haute Tension* (départs collés au bord). Les départs ne sont pas
  dans les coins.
- **Terrain façon RTS** : base principale sur un plateau en hauteur, falaises, **une rampe**, naturelle,
  troisièmes, bases riches (cristaux dorés), crêtes rocheuses qui créent des passages étroits, lacs.
- **Vision en hauteur** : depuis le bas, on ne voit pas le plateau. Tirer depuis le haut révèle le tireur.
  **Phares de vision** : une unité à côté donne une grande vision (même sur les hauteurs).
- **Économie à deux ressources** : *cristaux* (gisements épuisables, 2 drones par gisement) et *flux*
  (Extracteur sur un geyser, 3 drones). Le **Centre** crée une nouvelle base (dépôt, drones, +10 ravit.).
- **6 héros** (Colosse, Mage, Tireur, Soutien, Commandant, Assassin) — 24 compétences, 12 niveaux,
  9 objets en boutique dans la base.
- **Armée** : Drone, Gardien, Tireur, Lancier, Soigneur, Artilleur ; Caserne, Usine, Forge (armes / blindage
  niv. 3), Relais, Sentinelle (tourelle + détection), Extracteur, Centre.
- **Objectifs neutres** : camps de golems (or + expérience pour le héros) et **Titan** central (bonus
  d'équipe +20 % de dégâts pendant 2 min).
- **Formats** : 1v1, 2v2, 3v3 — contre l'IA (5 niveaux, sans triche de vision) ou en ligne (les joueurs
  remplacent les IA). Défaite = votre **Noyau** détruit. Mort subite après 25 min.
- Classement Elo contre l'IA, APM, statistiques par héros, console (`²`), 27 raccourcis remappables,
  vidéo / audio / interface entièrement réglables.

## Contrôles (par défaut)

| Action | Touche |
| --- | --- |
| Sélection / ordres | clic gauche (glisser) / clic droit |
| Attaque-déplacement, Stop, Tenir | A, S, H |
| Compétences du héros | Q W E R |
| Construire / entraîner (carte de commande) | Z X C V B N M |
| Héros, armée, ouvrier inactif | F1, F2, F3 |
| Groupes | Ctrl + 1-9 / 1-9 (double tap : caméra) |
| Boutique, scores, centrer, verrouiller caméra | P, Tab, Espace, Y |

## Multijoueur

- **Héberger** depuis le menu (port UDP 27115 par défaut) ou lancer le serveur dédié :
  `AxiomServer --port 27115 --size 3 --bots 2 --map 0 --lobby 45`.
- Le serveur est autoritaire et n'envoie à chaque client **que ce que son équipe voit** (anti map-hack).

## Compilation

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/AxiomSimTest 1 4 0 2     # match IA contre IA sans affichage : [joueurs/équipe] [IA A] [IA B] [carte]
```

Linux : paquets `libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev`.

## Mises à jour

Modifier le fichier `VERSION` puis pousser sur `main` : la CI compile Windows + Linux, lance un match
IA contre IA de vérification et publie la *Release* `vX.Y.Z`. Le launcher la détecte et l'installe.
