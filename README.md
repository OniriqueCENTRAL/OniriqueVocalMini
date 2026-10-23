# ONIRIQUE VOCAL MINI · PRO SERIES

Une chaîne de voix complète en une seule page : chaque bouton fait une chose, et la fait bien.
Plugin JUCE 8 (VST3, AU sur Mac, Standalone), Windows et Mac (binaire universel). L'identifiant du plugin (nom, code,
paramètres) est celui de VOCAL MINI : les projets et presets enregistrés avant PRO se rouvrent (les quatre anciens envois
de reverb sont convertis en un espace du même caractère, voir `prm::migrateLegacy`).

## Les réglages

| Zone | Réglages |
|---|---|
| En-tête | LIVE / MIX, BYPASS, presets (21 d'usine + les vôtres) |
| Gauche | **COMP** (niveleur LA-2A puis crêtes 1176 puis garde, un bouton ; mètres LEVEL et PEAK ; AUTO GAIN), **MULTIBAND** (quantité) + panneau **BANDS** : THRESHOLD, RATIO, ATTACK, RELEASE, KNEE, GAIN et ON de chaque bande, avec mètres |
| CLEAN | **PLOSIVE** (de-plosive), **NOISE** (noise suppressor), **GATE** (THRESH, RANGE, RELEASE), chacun avec son interrupteur et son témoin |
| EQ | LOW, MID, HI, AIR (bipolaires, courbes pensées pour la voix), LOW CUT 90 Hz, et **PARAMETRIC** : l'EQ paramétrique 8 bandes en plein écran (voir plus bas) |
| FX | DE-ESS (fréquence trouvée toute seule, CH français compris), DRIVE, PHONE, DOUBLER, CHORUS |
| DELAY | envoi, FEED, FILTER, TIME (au tempo), PING-PONG |
| REVERB | mode ROOM / PLATE 1 / PLATE 2 / HALL / SHIMMER, MIX, DECAY, PRE-DELAY, CLEAN + panneau : SIZE, DIFFUSION, MOD RATE, MOD DEPTH, LOW CUT, HIGH CUT, WIDTH, DUCKING, PULSE (au tempo), et la visualisation voix / queue / ce que CLEAN retire |
| Pied | IN (+ AUTO INPUT et la zone BEST), OUT |

DELAY et REVERB sont des pistes auxiliaires : la voix reste entière, le bouton règle l'envoi, le retour s'ajoute par-dessus.

## PARAMETRIC (l'EQ paramétrique)

Le bouton PARAMETRIC, sous LOW CUT, ouvre l'EQ en grand (par-dessus le COMP et les rangées).

- **8 bandes** : BELL, LOW SHELF, HIGH SHELF (±18 dB, Q 0.1 à 10), LOW CUT, HIGH CUT (12 / 24 / 36 / 48 dB/oct, Q =
  résonance), NOTCH. Mode **DYNAMIQUE** par bande (cloche, shelfs) : la bande n'agit que quand sa zone dépasse son seuil.
- **L'écran** : la voix qui entre et celle qui sort (analyseur en direct), la courbe de l'EQ paramétrique (rouge), celle
  des boutons LOW / MID / HI / AIR et du LOW CUT (rose, pointillés, avec une poignée par bouton qu'on peut tirer), et la
  courbe TOTALE (blanc). Toutes les courbes sont exactes : ce sont les filtres qui tournent, à la cadence du son.
- **À la souris** : tirer un point = fréquence / gain (Maj : fin), molette = Q, double-clic = allumer / couper,
  double-clic dans le vide = nouvelle bande, clic droit = menu. LISTEN : tenir un point fait entendre sa bande seule.
- **Le son** : filtres appariés à l'analogique (la forme reste juste jusqu'à 20 kHz, même en LIVE à 44.1 kHz), aucune
  latence, réglages qui glissent échantillon par échantillon, changement de type sans clic. Une bande à 0 dB ou coupée
  ne touche pas au son ; les presets d'usine ne s'en servent pas.

## Le chemin du signal

IN + AUTO INPUT → DE-PLOSIVE → NOISE SUPPRESSOR → NOISE GATE → [x2 en MIX] LOW CUT, LOW, MID → COMP → MULTIBAND →
PARAMETRIC → DRIVE → HI, AIR → DE-ESS → PHONE → DOUBLER, CHORUS → DELAY et REVERB (auxiliaires) → OUT → garde-fou
(au-dessus de -1 dBFS seulement) → BYPASS.

Pourquoi cet ordre : le nettoyage d'abord (les plosives et le bruit sont retirés tant qu'ils sont simples à reconnaître,
avant que la compression ne les fasse bouger ; le gate agit avant le rattrapage du COMP et avant les effets, il ne coupe
jamais une queue de reverb), le corps de la voix avant la compression, l'EQ paramétrique après les compresseurs (le
MULTIBAND ne reprend pas ce qu'on y pousse ou creuse), les aigus et le de-esser après (un AIR poussé ne fait jamais
comprimer, le de-esser voit les S finaux). Le détail est en tête de `Source/DSP/MiniChain.h`.

## LIVE et MIX

- **LIVE** (par défaut) : latence nulle, pour enregistrer en s'écoutant.
- **MIX** : qualité maximale une fois l'enregistrement fini (cœur suréchantillonné x2, de-esser, de-plosive et gate qui
  anticipent, noise suppressor spectral bin par bin). ~27 ms de latence, constante, compensée par le DAW.

## Le moteur (Source/DSP)

- `MiniChain.h` : la chaîne complète, AUTO INPUT, les deux modes, le garde-fou de sortie.
- `DePlosive.h`, `NoiseSuppressor.h` (repris de VOCALBOX), `VoiceGate.h` : la section CLEAN.
- `VoiceEQ.h` : LOW / MID, HI / AIR avec le grain harmonique et la garde de sifflantes (filtres à coefficients glissés).
- `VoiceComp.h`, `Comp1176.h`, `CompLA2A.h` : le COMP. `VoiceMultiband.h` : le MULTIBAND et ses bandes.
- `VoiceParametric.h` : l'EQ paramétrique (8 bandes, filtres appariés, mode dynamique) ; `SpectrumTap.h` : les prises
  de l'analyseur.
- `SmartDeEsser.h`, `VoiceColor.h` (DRIVE, PHONE), `VoiceWide.h` (DOUBLER, CHORUS), `VoiceDelay.h`, `VoiceReverb.h`.
- `Parameters.h` : la table de tous les paramètres (identifiants, plages, valeurs par défaut) et leur conversion vers le
  moteur ; `FactoryPresets.h` : les presets d'usine.

Les commentaires en tête de chaque fichier décrivent les algorithmes.

## Essais

Le moteur est vérifié hors du DAW par un banc d'essai (prises de voix réelles et signaux de test) : transparence et
BYPASS, chaque module en balayage, reprise après un silence, pause (aucune queue qui regonfle), syllabes K, gate, noise
suppressor (sur une voix propre + bruit rose : bruit des silences et timbre de la voix), de-plosive (comparé à celui de
VOCALBOX), recombinaison et réglages du multiband, reverb (RT60 de chaque mode, compatibilité mono, CLEAN, PULSE,
filtres), EQ paramétrique (courbe affichée = son à 0.00 dB près, forme analogique près de Nyquist, neutralité exacte à
0 dB, aucun clic quand on change type / pente / réglages, mode dynamique, écoute d'une bande), clics à l'automation, cas
limites (silence, DC, bruit plein niveau, 44.1 à 192 kHz), presets, CPU.

Limites connues : le noise suppressor apprend le bruit en ~1.5 s (il agit pleinement après) ; en LIVE il travaille par
bandes (moins fin qu'en MIX) ; le MULTIBAND a des seuils absolus, calés pour une voix au niveau BEST (AUTO INPUT allumé).

## Compiler

- Windows : `build_windows.bat`
- Mac : `./build_mac.sh`

Chaque envoi sur `main` est compilé par GitHub Actions (Mac + Windows) puis publié dans ONIRIQUE CENTRAL (identifiant
`onirique-vocal-mini`) si le secret `ONIRIQUE_PUBLISH_KEY` est présent. Le message du commit devient les notes de version.

Interface : `PluginEditor.*` (mise en page), `MiniUI.*` (potards de VOCAL MINI PRO, COMP, mètres, panneaux BANDS et REVERB),
`ParametricUI.*` (l'écran PARAMETRIC : analyseur, courbes, bandes),
`UIKit.*` (kit ONIRIQUE commun : palette, polices de la charte, pastilles, presets). Licence : dossier `License/` (commun à
tous les plugins ONIRIQUE).
