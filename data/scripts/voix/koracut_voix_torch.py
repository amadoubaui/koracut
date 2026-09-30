#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
# SPDX-License-Identifier: GPL-3.0-or-later
"""Moteur de voix local de KoraCut pour Windows, Linux et Mac Intel : Kyutai TTS 1.6B en/fr par PyTorch.

Même modèle, mêmes voix et même sortie que le moteur MLX des Mac à puce Apple (moshi_mlx.run_tts) :
lancé par koracut_voix.py dans le venv du moteur, il lit un fichier JSONL, une tâche par ligne,

    {"turns": ["texte"], "voices": ["cml-tts/fr/....wav"], "id": "p0__0"}

et écrit `<id>.wav` (24 kHz, mono) dans --out-folder, lot par lot, dès que chaque lot est fini.
Le fichier n'apparaît sous son nom qu'une fois complet (écriture à côté puis renommage) : le
processus parent peut compter les WAV présents pour suivre l'avancement.

Reprend la génération de moshi/run_tts.py (paquet pip `moshi`, Kyutai, MIT) : mêmes réglages par
défaut (cfg 2, 32 codebooks, marges 2/8/4, un pas entre les mots), seule la découpe en lots et le
choix de l'appareil changent. Appareil : cuda si une carte NVIDIA répond, sinon mps (Mac), sinon cpu ;
la variable KORACUT_VOIX_APPAREIL (cuda, mps, cpu) le force, pour les essais.
"""
import argparse
import json
import os
import sys
import time
from pathlib import Path


def choisir_appareil(demande):
    import torch
    if demande and demande != "auto":
        return demande
    if torch.cuda.is_available():
        return "cuda"
    if getattr(torch.backends, "mps", None) is not None and torch.backends.mps.is_available():
        return "mps"
    return "cpu"


def choisir_precision(appareil, demande):
    import torch
    if demande == "float32":
        return torch.float32
    if demande == "float16":
        return torch.float16
    if demande == "bfloat16":
        return torch.bfloat16
    # Automatique : bfloat16 partout où il est pris en charge (c'est la précision des poids),
    # float16 sur les anciennes cartes NVIDIA qui ne savent pas faire de bfloat16.
    if appareil.startswith("cuda") and not torch.cuda.is_bf16_supported():
        return torch.float16
    return torch.bfloat16


def main():
    p = argparse.ArgumentParser(description="Voix off locale KoraCut (Kyutai TTS, PyTorch)")
    p.add_argument("--out-folder", type=Path, required=True)
    p.add_argument("--hf-repo", default="kyutai/tts-1.6b-en_fr")
    p.add_argument("--voice-repo", default="kyutai/tts-voices")
    p.add_argument("--temp", type=float, default=0.6)
    p.add_argument("--cfg-coef", type=float, default=2.0)
    p.add_argument("--nq", type=int, default=32)
    p.add_argument("--padding-bonus", type=float, default=0.0)
    p.add_argument("--padding-between", type=int, default=1)
    p.add_argument("--device", default=os.environ.get("KORACUT_VOIX_APPAREIL") or "auto",
                   help="auto, cuda, mps ou cpu (défaut : variable KORACUT_VOIX_APPAREIL, sinon auto)")
    p.add_argument("--dtype", default="auto", help="auto, bfloat16, float16 ou float32")
    p.add_argument("--batch-size", type=int, default=0,
                   help="Tâches générées ensemble ; 0 = automatique (8 sur carte graphique, 3 sinon)")
    p.add_argument("--threads", type=int, default=0, help="Fils de calcul sur processeur (0 = choix de PyTorch)")
    p.add_argument("jsonl", type=Path)
    args = p.parse_args()

    import sphn
    import torch
    from moshi.models.loaders import CheckpointInfo
    from moshi.models.tts import TTSModel

    taches = [json.loads(l) for l in args.jsonl.read_text(encoding="utf-8").split("\n") if l.strip()]
    if not taches:
        return 0
    args.out_folder.mkdir(parents=True, exist_ok=True)

    appareil = choisir_appareil(args.device)
    precision = choisir_precision(appareil, args.dtype)
    if args.threads > 0:
        torch.set_num_threads(args.threads)
    lot = args.batch_size if args.batch_size > 0 else (8 if appareil.startswith("cuda") else 3)
    print(f"appareil {appareil}, précision {precision}, lots de {lot}, {torch.get_num_threads()} fils", flush=True)

    debut = time.time()
    info = CheckpointInfo.from_hf_repo(args.hf_repo)
    tts = TTSModel.from_checkpoint_info(
        info, voice_repo=args.voice_repo, n_q=args.nq, temp=args.temp, cfg_coef=args.cfg_coef,
        max_padding=8, initial_padding=2, final_padding=4, padding_bonus=args.padding_bonus,
        device=appareil, dtype=precision)
    # Même logique que run_tts.py : ce modèle a appris la CFG par distillation, elle passe en conditionnement.
    cfg_conditionnement = None
    if tts.valid_cfg_conditionings:
        cfg_conditionnement = tts.cfg_coef
        tts.cfg_coef = 1.0
        sans_texte = sans_prefixe = False
    else:
        sans_texte = sans_prefixe = True
    mimi = tts.mimi
    print(f"modèle chargé en {time.time() - debut:.1f} s", flush=True)

    for a in range(0, len(taches), lot):
        paquet = taches[a:a + lot]
        t0 = time.time()
        entrees, attributs = [], []
        prefixes = None if tts.multi_speaker else []
        for t in paquet:
            entrees.append(tts.prepare_script(t["turns"], padding_between=args.padding_between))
            voix = [tts.get_voice_path(v) for v in t["voices"]] if tts.multi_speaker else []
            attributs.append(tts.make_condition_attributes(voix, cfg_conditionnement))
            if prefixes is not None:
                prefixes.append(tts.get_prefix(tts.get_voice_path(t["voices"][0])))
        with torch.no_grad():
            res = tts.generate(entrees, attributs, prefixes=prefixes,
                               cfg_is_no_prefix=sans_prefixe, cfg_is_no_text=sans_texte)
            morceaux = []
            with mimi.streaming(len(paquet)):
                for trame in res.frames[tts.delay_steps:]:
                    morceaux.append(mimi.decode(trame[:, 1:]))
            ondes = torch.cat(morceaux, dim=-1).float().cpu()
        voix_totale = 0.0
        for k, t in enumerate(paquet):
            fin = res.end_steps[k]
            longueur = ondes.shape[-1] if fin is None else int(mimi.sample_rate * (fin + tts.final_padding) / mimi.frame_rate)
            onde = ondes[k, :, :longueur]
            if prefixes is not None:
                onde = onde[:, int(mimi.sample_rate * prefixes[k].shape[-1] / mimi.frame_rate):]
            voix_totale += onde.shape[-1] / mimi.sample_rate
            final = args.out_folder / f"{t['id']}.wav"
            partiel = args.out_folder / f"{t['id']}.partiel.wav"
            sphn.write_wav(str(partiel), onde.clamp(-1, 1).numpy(), mimi.sample_rate)
            os.replace(partiel, final)
            if fin is None:
                print(f"attention : fin non trouvée pour {t['id']}", flush=True)
        duree = time.time() - t0
        print(f"lot de {len(paquet)} : {voix_totale:.2f} s de voix en {duree:.1f} s "
              f"({duree / max(voix_totale, 0.01):.2f} s de calcul par seconde de voix)", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
