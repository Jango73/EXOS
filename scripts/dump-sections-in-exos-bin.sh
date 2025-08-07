#!/bin/bash

MAP="../kernel/bin/exos-bin.map"
BIN="../kernel/bin/exos.bin"

# Trouve chaque section et son load address
grep 'load address' "$MAP" | while read -r line; do
    # Récupère la section (tout début de ligne)
    section=$(echo "$line" | awk '{print $1}')
    # Récupère l'adresse physique (dernier champ)
    addr=$(echo "$line" | awk '{print $NF}')
    # Affiche la section et l'adresse
    echo "$section @ $addr"
    # Appelle hexdump (64 octets à partir de cette adresse physique)
    hexdump -Cv -s $((addr)) -n 64 "$BIN"
    echo
done
