# ONIRIQUE VOCAL MINI

Une chaîne de voix complète en une seule page, ultra simple : chaque bouton fait une chose, et la fait bien.
Plugin JUCE 8 (VST3, AU sur Mac, Standalone), Windows et Mac (binaire universel).

## Les réglages

| Rangée | Boutons |
|---|---|
| En-tête | LIVE / MIX, presets |
| Orbe | **COMP** (1176 puis LA-2A, un bouton), **MULTIBAND** (4 bandes voix, un bouton) |
| EQ | LOW, MID, HI, AIR (bipolaires, courbes pensées pour la voix), LOW CUT 90 Hz |
| FX | DE-ESS (fréquence trouvée toute seule), DRIVE, PHONE, DOUBLER, CHORUS |
| DELAY | envoi, FEED, FILTER, TIME (au tempo), PING-PONG |
| REVERB | SHORT, SMALL, BIG, LUSH (quatre envois auxiliaires) |
| Pied | IN (+ AUTO INPUT et la zone BEST), OUT |

DELAY et REVERB sont des pistes auxiliaires : la voix reste entière, le bouton règle l'envoi, le retour s'ajoute par-dessus.

## LIVE et MIX

- **LIVE** (par défaut) : latence nulle, pour enregistrer en s'écoutant.
- **MIX** : qualité maximale une fois l'enregistrement fini (cœur suréchantillonné x2, de-esser qui anticipe), ~1.2 ms de
  latence compensée par le DAW.

## Le moteur (Source/DSP)

- `MiniChain.h` : la chaîne complète, AUTO INPUT, les deux modes, le garde-fou de sortie.
- `VoiceEQ.h` : LOW / MID (avant la compression), HI / AIR avec le grain harmonique et la garde de sifflantes.
- `VoiceComp.h`, `Comp1176.h`, `CompLA2A.h` : le COMP (modèles de VOCALBOX retravaillés pour la voix).
- `VoiceMultiband.h` : le MULTIBAND à seuils adaptatifs.
- `SmartDeEsser.h`, `VoiceColor.h` (DRIVE, PHONE), `VoiceWide.h` (DOUBLER, CHORUS), `VoiceDelay.h`, `VoiceReverb.h`.

Les commentaires en tête de chaque fichier décrivent les algorithmes.

## Compiler

- Windows : `build_windows.bat`
- Mac : `./build_mac.sh`

Chaque envoi sur `main` est compilé par GitHub Actions (Mac + Windows) puis publié dans ONIRIQUE CENTRAL (identifiant
`onirique-vocal-mini`) si le secret `ONIRIQUE_PUBLISH_KEY` est présent. Le message du commit devient les notes de version.

Interface : `PluginEditor.*` (mise en page), `MiniUI.*` (potards, orbe de la voix, mètres), `UIKit.*` (kit ONIRIQUE commun),
`OniriqueKnob.h` (le potard de la marque, à ne modifier que dans plugin-ui/ d'ONIRIQUE CENTRAL).
Licence : dossier `License/` (commun à tous les plugins ONIRIQUE).
