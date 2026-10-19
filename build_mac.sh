#!/bin/bash
# Nettoie puis compile ONIRIQUE VOCAL MINI (Release) sur Mac. A lancer depuis ce dossier.
# Prérequis : Xcode (ou Command Line Tools : xcode-select --install), CMake, Git.
set -e
cd "$(dirname "$0")"

rm -rf build
cmake -B build -G Xcode
cmake --build build --config Release -- -quiet

OUT="build/OniriqueVocalMini_artefacts/Release"
# signature ad hoc : obligatoire pour que le plugin se charge sur Apple Silicon
for p in "$OUT/VST3/ONIRIQUE VOCAL MINI.vst3" "$OUT/AU/ONIRIQUE VOCAL MINI.component" "$OUT/Standalone/ONIRIQUE VOCAL MINI.app"; do
    codesign --force --deep --sign - "$p"
done

echo
echo "OK :"
echo "  $OUT/VST3/ONIRIQUE VOCAL MINI.vst3        -> ~/Library/Audio/Plug-Ins/VST3"
echo "  $OUT/AU/ONIRIQUE VOCAL MINI.component     -> ~/Library/Audio/Plug-Ins/Components"
echo "  $OUT/Standalone/ONIRIQUE VOCAL MINI.app"
