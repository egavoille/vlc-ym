# vlc-ym — lecture des musiques .YM dans VLC

**Auteur : Emmanuel Gavoille** — <egavoille@gmail.com> —
[lesbidouillesdemonsieurm.com](https://lesbidouillesdemonsieurm.com)

Plugin pour **VLC 3.0.x** qui lit les fichiers `.ym` (puce sonore YM2149 / AY-3-8912 :
Amstrad CPC, Atari ST, ZX Spectrum, MSX…), basé sur la bibliothèque
[ST-Sound](https://github.com/arnaud-carre/StSound) d'Arnaud Carré.

- Tous les formats ST-Sound : YM2, YM3, YM3b, YM5, YM6, MIX1, YM-T1, YM-T2,
  compressés LHA ou non.
- Horloge lue dans le fichier (YM5/YM6) : les morceaux CPC à 1 MHz sont joués
  à la bonne hauteur.
- Titre, auteur et commentaire affichés dans VLC ; durée, barre de progression
  et déplacement dans le morceau (seek).
- Options (Préférences → Tout → Entrée/Codecs → Démultiplexeurs → YM) :
  filtre passe-bas (activé par défaut) et lecture en boucle.

## Installation

### Windows (VLC 3.0.x 64 bits)

1. Copier `bin/windows-x64/libym_plugin.dll` dans
   `C:\Program Files\VideoLAN\VLC\plugins\demux\`
2. Mettre à jour le cache des plugins : ouvrir une invite de commandes
   **en administrateur** puis :
   ```
   cd "C:\Program Files\VideoLAN\VLC"
   vlc-cache-gen.exe plugins
   ```
   (à défaut, Outils → Préférences → Réinitialiser les préférences.)
3. Ouvrir un fichier `.ym` dans VLC.

Pour vérifier que le plugin est bien chargé, lancer
`vlc.exe -vv fichier.ym > log.txt 2>&1` : le journal doit contenir
`using demux module "ym"`. Le message « cannot find plug-in entry point »
signifie que la DLL ne correspond pas à la version de VLC.

### Linux (VLC 3.0.x, x86_64)

Le binaire fourni vise **Debian / Ubuntu récents** : leurs paquets VLC utilisent
le point d'entrée `vlc_entry__3_0_0ft64`. Sur les autres distributions, recompiler
avec `make`, qui utilise les en-têtes VLC installés sur la machine.

```
sudo cp bin/linux-x86_64/libym_plugin.so /usr/lib/x86_64-linux-gnu/vlc/plugins/demux/
sudo /usr/lib/x86_64-linux-gnu/vlc/vlc-cache-gen /usr/lib/x86_64-linux-gnu/vlc/plugins
```

Ou depuis les sources : `sudo apt install libvlccore-dev g++ && make && sudo make install`.

VLC 4.x n'est pas pris en charge : l'interface des plugins y a changé.

## Compilation

| Cible | Commande | Prérequis |
|---|---|---|
| Linux | `make` | `libvlccore-dev`, `g++`, `pkg-config` |
| Windows 64 bits | `make win64` | `mingw-w64` |

Pour Windows, la compilation utilise les en-têtes officiels de VLC 3.0
(`win/vlc-3.0-include/`, copiés depuis la branche 3.0.x de VideoLAN). Les
en-têtes Debian/Ubuntu ne conviennent pas : ils renomment le point d'entrée en
`…ft64`, que VLC pour Windows ne reconnaît pas. La bibliothèque d'import de
`libvlccore.dll` est générée depuis `win/libvlccore.def` : le SDK Windows de VLC
n'est pas nécessaire.

## Fonctionnement

Le plugin contient deux modules :

- **un filtre de flux** (`stream_filter`) qui décompresse les .ym LHA à la
  volée. Il passe avant le module « archive » de VLC (libarchive), qui sinon
  ouvre ces fichiers comme des archives et, selon sa version, échoue à les
  extraire (« Truncated LHa file body » sous Windows) ;
- **un démultiplexeur** (`demux`) qui joue le contenu YM avec ST-Sound.

## Corrections apportées à ST-Sound

Le dossier `StSoundLibrary/` est la bibliothèque d'origine avec des corrections
(toutes marquées `[vlc-ym]` dans le code, et regroupées dans
`stsound-correctifs.patch`) :

1. **Son faux sur les YM5/YM6/YMT une fois compilé avec optimisation.**
   `readNtString((char**)&ptr)` modifiait un pointeur à travers un type
   incompatible (violation du *strict aliasing*). En -O1/-O2, le compilateur
   ignorait la mise à jour : les registres étaient lus au mauvais endroit du
   fichier. Le résultat changeait selon les options de compilation, et le
   Ym2Wav officiel compilé normalement en souffre aussi.
2. **Débordement d'entier** dans le calcul des périodes (`per * replayFrequency`)
   sur les longues enveloppes (Sync-Buzzer).
3. **Robustesse face aux fichiers corrompus** : contrôles de bornes sur les
   en-têtes, digidrums, blocs MIX1, données tracker ; marge à zéro derrière les
   buffers ; décompresseur LHA durci (longueurs de code, tailles de tables,
   contrôle de complétude de Huffman) ; cadence de lecteur bornée (une valeur
   aberrante figeait la lecture) ; index de digidrums bornés.

Le plugin lui-même sérialise la création des lecteurs : ST-Sound modifie une
table globale à la première instanciation, et VLC analyse les fichiers de la
playlist dans un autre thread pendant la lecture.

## Validation

- Les 11 morceaux d'exemple du dépôt ST-Sound (YM2, YM5, YM6, MIX1, YM-T1) :
  reconnus, durée exacte, sortie de VLC **identique bit à bit** à la
  bibliothèque corrigée, rendu identique en -O0, -O1, -O2 et -O3.
- Lecture temps réel complète jusqu'au dernier échantillon ; seek précis à
  moins d'1 ms.
- 3 000 fichiers corrompus passés sous AddressSanitizer + UBSan sans erreur,
  et 153 d'entre eux ouverts dans VLC sans aucun plantage.

## Auteur et licences

- Plugin vlc-ym (`src/`, `win/ym_plugin.rc`, corrections de ST-Sound marquées
  `[vlc-ym]`) : © 2026 **Emmanuel Gavoille** <egavoille@gmail.com> —
  https://lesbidouillesdemonsieurm.com — LGPL 2.1 ou ultérieure, comme les
  modules de VLC.
- ST-Sound : licence MIT/BSD d'Arnaud Carré (`StSoundLibrary/LICENSE`).
