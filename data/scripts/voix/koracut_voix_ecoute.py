#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
# SPDX-License-Identifier: GPL-3.0-or-later
"""L'écoute des voix générées : ce que Whisper entend dans chaque fichier (chantier 5, voix off).

Lancé par koracut_voix.py avec le Python de la reconnaissance vocale de KoraCut (celui de la coupe
automatique). Le modèle se charge une seule fois pour toutes les prises. Une ligne par fichier :
    KORACUT-ECOUTE {"fichier": "...", "texte": "..."}
"""
import argparse
import json
import os
import sys


def main():
    parser = argparse.ArgumentParser("KoraCut, écoute des voix générées")
    parser.add_argument("fichiers", nargs="+")
    parser.add_argument("--model", default="base", help="nom du modèle Whisper ou chemin d'un .pt")
    parser.add_argument("--language", default="fr")
    parser.add_argument("--ffmpeg_path", default="", help="dossier de ffmpeg, s'il n'est pas dans le PATH")
    args = parser.parse_args()
    if args.ffmpeg_path:
        os.environ["PATH"] = args.ffmpeg_path + os.pathsep + os.environ.get("PATH", "")
    import whisper

    model = whisper.load_model(args.model, device="cpu")
    for f in args.fichiers:
        result = model.transcribe(f, language=args.language or None, fp16=False, condition_on_previous_text=False)
        print("KORACUT-ECOUTE " + json.dumps({"fichier": f, "texte": result.get("text", "").strip()}, ensure_ascii=False), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
