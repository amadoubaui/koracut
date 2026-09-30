#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
# SPDX-License-Identifier: GPL-3.0-or-later
"""Un faux moteur de voix off pour koracutvoixtest : même protocole que data/scripts/voix/koracut_voix.py,
sans modèle ni réseau. Chaque phrase devient un WAV (un la à 440 Hz, 0,1 s par mot) en bibliothèque standard."""
import json
import math
import struct
import sys
import wave


def dire(ligne, obj):
    print(ligne + " " + json.dumps(obj, ensure_ascii=False), flush=True)


def main():
    demande = json.loads(sys.stdin.read() or "{}")
    if demande.get("mode") == "voix_distantes":
        dire("KORACUT-VOIX-LISTE", [{"id": "v1", "nom": "Voix un", "description": "essai"}])
        dire("KORACUT-VOIX-FIN", {})
        return 0
    if demande.get("mode") == "installer":
        dire("KORACUT-VOIX-ETAPE", {"texte": "Prêt", "fait": 3, "total": 3})
        dire("KORACUT-VOIX-FIN", {})
        return 0
    if "ÉCHEC" in json.dumps(demande, ensure_ascii=False):
        dire("KORACUT-VOIX-ERREUR", {"code": "moteur_en_erreur", "message": "Le moteur local s'est arrêté (essai)."})
        return 1
    phrases = demande.get("phrases") or []
    for n, p in enumerate(phrases):
        dire("KORACUT-VOIX-ETAPE", {"texte": "Voix générée", "fait": n + 1, "total": len(phrases)})
        secondes = max(0.5, 0.1 * len(p["texte"].split()))
        taux = 48000
        with wave.open(p["sortie"], "wb") as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(taux)
            images = bytearray()
            for i in range(int(secondes * taux)):
                v = int(8000 * math.sin(2 * math.pi * 440 * i / taux))
                images += struct.pack("<hh", v, v)
            w.writeframes(bytes(images))
        dire("KORACUT-VOIX-PHRASE", {"id": p["id"], "fichier": p["sortie"], "duree": round(secondes, 3), "fidelite": 1.0,
                                     "entendu": p["texte"], "douteux": [], "dit": p.get("dire") or p["texte"], "lufs": -16.0, "crete": -3.0,
                                     "prises": [{"fidelite": 1.0, "duree": secondes, "entendu": p["texte"]}]})
    dire("KORACUT-VOIX-FIN", {})
    return 0


if __name__ == "__main__":
    sys.exit(main())
