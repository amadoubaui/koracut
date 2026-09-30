#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
# SPDX-License-Identifier: GPL-3.0-or-later
"""Transcription au mot pour la coupe automatique de KoraCut (document 15, lot 2).

Sortie, sur la sortie standard :
    KORACUT-ETAPE <texte>                 où en est le travail
    KORACUT-MOTS <json>                   une seule ligne à la fin :
        {"langue": "fr", "mots": [{"m": "Bonjour", "d": 0.0, "f": 0.38, "p": 0.97}, ...]}
    (m = mot tel que transcrit, d = début et f = fin en secondes, p = probabilité)

Pourquoi un script à part plutôt que whispertotext.py : celui-là sert le panneau Voix et sort du
texte ligne à ligne ; ici il faut du JSON complet et une amorce (initial_prompt). Whisper a
tendance à omettre les « euh » : l'amorce en contient pour l'encourager à les écrire.
"""
import argparse
import json
import os
import sys


def main():
    parser = argparse.ArgumentParser("KoraCut, mots horodatés")
    parser.add_argument("--src", required=True, help="fichier audio ou vidéo")
    parser.add_argument("--model", default="base", help="nom du modèle Whisper ou chemin d'un .pt")
    parser.add_argument("--language", default="fr")
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--prompt", default="Euh, alors, bon, euh... hum, je vais, euh, commencer.")
    parser.add_argument("--ffmpeg_path", default="", help="dossier de ffmpeg, s'il n'est pas dans le PATH")
    args = parser.parse_args()

    if args.ffmpeg_path:
        os.environ["PATH"] = args.ffmpeg_path + os.pathsep + os.environ.get("PATH", "")
    if not os.path.exists(args.src):
        print(f"KORACUT-ERREUR fichier introuvable : {args.src}", flush=True)
        return 2

    print("KORACUT-ETAPE chargement du modèle", flush=True)
    import whisper

    model = whisper.load_model(args.model, device=args.device)
    print("KORACUT-ETAPE transcription", flush=True)
    result = model.transcribe(
        args.src,
        language=args.language or None,
        word_timestamps=True,
        initial_prompt=args.prompt or None,
        fp16=args.device != "cpu",
        condition_on_previous_text=False,
    )
    mots = []
    for segment in result.get("segments", []):
        for w in segment.get("words", []):
            mots.append({
                "m": w["word"].strip(),
                "d": round(float(w["start"]), 3),
                "f": round(float(w["end"]), 3),
                "p": round(float(w.get("probability", 1.0)), 3),
            })
    sortie = {"langue": result.get("language", args.language), "mots": mots}
    print("KORACUT-MOTS " + json.dumps(sortie, ensure_ascii=False), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
