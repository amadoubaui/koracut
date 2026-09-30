#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
# SPDX-License-Identifier: GPL-3.0-or-later
"""Voix off de synthèse pour KoraCut (chantier 5) : une phrase = un WAV nettoyé, vérifié par Whisper.

Lancé par l'application (KoraCutVoiceOver) en tâche de fond. La demande arrive en JSON sur l'entrée
standard, jamais sur la ligne de commande : une clé de service n'apparaît donc pas dans la liste
des processus. Les réponses sortent sur la sortie standard, une par ligne :

    KORACUT-VOIX-ETAPE {"texte": ..., "fait": n, "total": m}
    KORACUT-VOIX-PHRASE {"id", "fichier", "duree", "fidelite", "entendu", "prises", "lufs", "crete"}
    KORACUT-VOIX-LISTE [{"id", "nom", "description"}]        (mode voix_distantes)
    KORACUT-VOIX-ERREUR {"code", "message"}
    KORACUT-VOIX-FIN {}

Moteurs : « local » (Kyutai TTS 1.6B en/fr, poids CC-BY 4.0, voix CML-TTS CC-BY 4.0),
« elevenlabs » et « openai » (clé de la page Connexions IA, transmise par l'entrée standard).

Le moteur local existe en deux versions, même modèle, mêmes voix, même sortie (un <id>.wav par tâche) :
  - Mac à puce Apple : MLX (paquet moshi_mlx, module moshi_mlx.run_tts), quantifié en 8 bits ;
  - Windows, Linux, Mac Intel : PyTorch (paquet moshi, programme koracut_voix_torch.py à côté de
    celui-ci), sur la carte NVIDIA si elle répond (roue CUDA de torch), sinon sur le processeur.
Le venv est le même dossier (<cache>/kyutai) : un seul des deux paquets y est installé.

Variables d'environnement (essais) :
  KORACUT_VOIX_CACHE   dossier du venv et des journaux (défaut ~/.cache/koracut-voix)
  KORACUT_VOIX_MOTEUR  « mlx » ou « torch » : force la version du moteur local (défaut : selon la machine)
  KORACUT_VOIX_APPAREIL  « cuda », « mps » ou « cpu » : force l'appareil de la version PyTorch (défaut : le meilleur)
Même chaîne que Sources/koracut-voix/voix_off.py, écrite pour être pilotée par l'application.
Recherche et mesures : KoraCut/Recherche/Voix off de synthese.md.
"""
import difflib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unicodedata
import urllib.request
from pathlib import Path

CACHE = Path(os.environ.get("KORACUT_VOIX_CACHE") or (Path.home() / ".cache/koracut-voix"))
PY_KYUTAI = CACHE / "kyutai" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
SCRIPT_TORCH = Path(__file__).resolve().with_name("koracut_voix_torch.py")
MODELE = "kyutai/tts-1.6b-en_fr"
DEPOT_VOIX = "kyutai/tts-voices"
VOIX_DEFAUT = "cml-tts/fr/4482_3103_000063-0001_enhanced.wav"
# Les voix que l'installation télécharge (quelques centaines de Ko chacune).
VOIX_LOCALES = [VOIX_DEFAUT, "cml-tts/fr/6318_7016_000027-0002_enhanced.wav",
                "unmute-prod-website/developpeuse-3.wav", "unmute-prod-website/fabieng-enhanced-v2.wav"]

# Réécritures qui aident le moteur à bien prononcer (le texte d'origine reste celui du sous-titre).
PRONONCIATION = {r"\b([A-Za-z0-9-]+)\.com\b": r"\1 point com", r"\bTV\b": "télé", r"\bHLM\b": "H L M", r"…": ","}

# Le traitement studio : bords de silence, passe-haut, creux à 300 Hz, présence, de-esseur, compression douce.
CHAINE = ("silenceremove=start_periods=1:start_threshold=-48dB:start_silence=0.04,areverse,"
          "silenceremove=start_periods=1:start_threshold=-48dB:start_silence=0.08,areverse,"
          "highpass=f=75,equalizer=f=300:t=q:w=1.1:g=-2,equalizer=f=3500:t=q:w=0.9:g=2,equalizer=f=9000:t=h:w=1:g=1,"
          "deesser=i=0.35:m=0.5:f=0.5,acompressor=threshold=-21dB:ratio=2.5:attack=8:release=140:makeup=2,afade=t=in:d=0.01")

# Le ton : ce qu'il change selon le moteur. En local, seule la « température » bouge : l'effet est léger.
TONS = {
    "pose": {"temp": 0.6, "stabilite": 0.45, "style": 0.35, "consigne": "Voix posée et chaleureuse, rythme de publicité, articulation nette."},
    "vivant": {"temp": 0.72, "stabilite": 0.3, "style": 0.6, "consigne": "Voix enthousiaste et énergique, sourire dans la voix, rythme de publicité."},
    "doux": {"temp": 0.5, "stabilite": 0.6, "style": 0.2, "consigne": "Voix douce et calme, proche, sans hâte."},
    "serieux": {"temp": 0.5, "stabilite": 0.7, "style": 0.1, "consigne": "Voix grave et sérieuse, ton de documentaire, sans emphase."},
}

FFMPEG = "ffmpeg"


def dire(ligne, obj):
    print(ligne + " " + json.dumps(obj, ensure_ascii=False), flush=True)


def etape(texte, fait=0, total=0):
    dire("KORACUT-VOIX-ETAPE", {"texte": texte, "fait": fait, "total": total})


class Echec(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code
        self.message = message


def norm(s):
    s = unicodedata.normalize("NFD", s.lower().replace("%", " pour cent "))
    s = "".join(c for c in s if unicodedata.category(c) != "Mn")
    s = s.replace("vingt-quatre", "24").replace("quarante-huit", "48")
    eq = {"0": "zero", "waive": "wave", "francs": "franc", "cent": "100", "payes": "paies", "paye": "paie", "trois": "3", "dix": "10",
          "douze": "12", "preuves": "preuve", "taps": "tapes", "h": "heures", "un": "1", "deux": "2", "cinq": "5"}
    return [eq.get(m, m) for m in re.findall(r"[a-z0-9]+", s)]


def fidelite(texte, entendu):
    """Part du texte retrouvée dans ce que Whisper a entendu, lettre à lettre (1 = tout).

    Lettre à lettre plutôt que mot à mot : « talangue » pour « ta langue » ou « Coracut » pour
    « KoraCut » sont bien dits, seule l'orthographe de Whisper change."""
    a, b = "".join(norm(texte)), "".join(norm(entendu))
    return difflib.SequenceMatcher(None, a, b, autojunk=False).ratio() if a else 1.0


def mots_douteux(texte, entendu):
    """Les mots du texte que Whisper n'a pas entendus, même à peu près."""
    entendus = norm(entendu)
    douteux = []
    for m in norm(texte):
        if not any(difflib.SequenceMatcher(None, m, e).ratio() >= 0.75 for e in entendus) and m not in "".join(entendus):
            douteux.append(m)
    return douteux


def a_dire(p):
    t = p.get("dire") or p["texte"]
    for a, b in PRONONCIATION.items():
        t = re.sub(a, b, t)
    return t


def duree(f):
    r = subprocess.run([FFMPEG, "-hide_banner", "-i", str(f)], capture_output=True, text=True).stderr
    m = re.search(r"Duration: (\d+):(\d+):([\d.]+)", r)
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3)) if m else 0.0


def ffmpeg(*args):
    r = subprocess.run([FFMPEG, "-v", "error", "-y", *args], capture_output=True, text=True)
    if r.returncode != 0:
        raise Echec("ffmpeg", "ffmpeg a échoué : " + r.stderr.strip()[-400:])


# ---- Les moteurs -------------------------------------------------------------------------------

def moteur_local():
    """« mlx » sur Mac à puce Apple, « torch » partout ailleurs (Windows, Linux, Mac Intel)."""
    force = (os.environ.get("KORACUT_VOIX_MOTEUR") or "").strip().lower()
    if force in ("mlx", "torch"):
        return force
    import platform
    return "mlx" if sys.platform == "darwin" and platform.machine() == "arm64" else "torch"


def carte_nvidia():
    """Vrai si nvidia-smi répond et voit une carte : on prend alors la roue CUDA de torch."""
    outil = shutil.which("nvidia-smi")
    if not outil:
        return False
    try:
        r = subprocess.run([outil, "-L"], capture_output=True, text=True, timeout=15)
    except (OSError, subprocess.SubprocessError):
        return False
    return r.returncode == 0 and "GPU" in r.stdout


def voix_en_cache(voix):
    hub = os.environ.get("HF_HUB_CACHE") or str(Path(os.environ.get("HF_HOME") or (Path.home() / ".cache/huggingface")) / "hub")
    base = Path(hub) / "models--kyutai--tts-voices" / "snapshots"
    return any(base.glob(f"*/{voix}*.safetensors")) if base.exists() else False


def local(taches, dossier, demande):
    if not PY_KYUTAI.exists():
        raise Echec("moteur_absent", "Le moteur de voix local n'est pas installé.")
    voix = demande.get("voix") or VOIX_DEFAUT
    temp = TONS.get(demande.get("ton") or "pose", TONS["pose"])["temp"]
    jsonl = Path(dossier) / "taches.jsonl"
    jsonl.write_text("".join(json.dumps({"turns": [t], "voices": [voix], "id": i}, ensure_ascii=False) + "\n" for i, t in taches), encoding="utf-8")
    env = {**os.environ, "HF_HUB_DISABLE_XET": "1", "HF_HUB_DISABLE_TELEMETRY": "1"}
    if voix_en_cache(voix):
        # Tout est sur la machine : aucun accès au réseau, donc aucun téléchargement surprise.
        env["HF_HUB_OFFLINE"] = "1"
    if moteur_local() == "mlx":
        commande = [str(PY_KYUTAI), "-m", "moshi_mlx.run_tts", "--out-folder", str(dossier), "--only-wav", "--quantize", "8",
                    "--padding-bonus", "-1", "--temp", str(temp), str(jsonl)]
    else:
        if not SCRIPT_TORCH.exists():
            raise Echec("moteur_absent", f"Programme du moteur introuvable : {SCRIPT_TORCH}")
        commande = [str(PY_KYUTAI), str(SCRIPT_TORCH), "--out-folder", str(dossier), "--padding-bonus", "-1",
                    "--temp", str(temp), str(jsonl)]
    # Les messages du moteur vont dans un fichier : un tube lu seulement à la fin peut se remplir et tout bloquer.
    journal = Path(dossier) / "moteur.log"
    with open(journal, "w", encoding="utf-8", errors="replace") as sortie_moteur:
        proc = subprocess.Popen(commande, env=env, stdout=sortie_moteur, stderr=subprocess.STDOUT, text=True)
        vus = 0
        etape("Chargement du modèle de voix", 0, len(taches))
        while proc.poll() is None:
            time.sleep(0.5)
            n = sum(1 for i, _ in taches if (Path(dossier) / f"{i}.wav").exists())
            if n != vus:
                vus = n
                etape("Voix générée", n, len(taches))
    if proc.returncode != 0:
        erreurs = journal.read_text(encoding="utf-8", errors="replace")
        raise Echec("moteur_en_erreur", "Le moteur local s'est arrêté : " + erreurs.strip()[-400:])


def http(req):
    try:
        return urllib.request.urlopen(req, timeout=120).read()
    except urllib.error.HTTPError as e:
        corps = e.read().decode("utf-8", "replace")[:300]
        if e.code in (401, 403):
            raise Echec("cle_refusee", "Le service a refusé la clé. Vérifie-la dans Préférences > Connexions IA.")
        if e.code in (402, 429) or "quota" in corps.lower() or "credit" in corps.lower():
            raise Echec("credit_epuise", "Le compte du service n'a plus de crédit ou atteint son quota.")
        raise Echec("service_en_erreur", f"Le service a répondu {e.code} : {corps}")
    except OSError as e:
        raise Echec("reseau", f"Pas de réponse du service : {e}")


def elevenlabs(taches, dossier, demande):
    cle = demande.get("cle") or ""
    if not cle:
        raise Echec("cle_absente", "Aucune clé ElevenLabs. Branche ElevenLabs dans Préférences > Connexions IA.")
    voix = demande.get("voix") or ""
    if not voix:
        raise Echec("entree_invalide", "Choisis une voix ElevenLabs (voix_off_voix moteur=elevenlabs).")
    ton = TONS.get(demande.get("ton") or "pose", TONS["pose"])
    base = (demande.get("url_base") or "https://api.elevenlabs.io").rstrip("/")
    for k, (i, t) in enumerate(taches):
        etape("Voix demandée au service", k, len(taches))
        corps = json.dumps({"text": t, "model_id": "eleven_multilingual_v2", "language_code": "fr",
                            "voice_settings": {"stability": ton["stabilite"], "similarity_boost": 0.8, "style": ton["style"], "use_speaker_boost": True}}).encode()
        req = urllib.request.Request(f"{base}/v1/text-to-speech/{voix}?output_format=mp3_44100_192", data=corps,
                                     headers={"xi-api-key": cle, "Content-Type": "application/json"})
        (Path(dossier) / f"{i}.mp3").write_bytes(http(req))
        ffmpeg("-i", str(Path(dossier) / f"{i}.mp3"), str(Path(dossier) / f"{i}.wav"))
    etape("Voix reçues", len(taches), len(taches))


def openai(taches, dossier, demande):
    cle = demande.get("cle") or ""
    if not cle:
        raise Echec("cle_absente", "Aucune clé OpenAI. Branche OpenAI dans Préférences > Connexions IA.")
    ton = TONS.get(demande.get("ton") or "pose", TONS["pose"])
    base = (demande.get("url_base") or "https://api.openai.com/v1").rstrip("/")
    for k, (i, t) in enumerate(taches):
        etape("Voix demandée au service", k, len(taches))
        corps = json.dumps({"model": "gpt-4o-mini-tts", "voice": demande.get("voix") or "coral", "input": t,
                            "instructions": "Parle en français. " + ton["consigne"], "response_format": "wav"}).encode()
        req = urllib.request.Request(f"{base}/audio/speech", data=corps, headers={"Authorization": "Bearer " + cle, "Content-Type": "application/json"})
        (Path(dossier) / f"{i}.wav").write_bytes(http(req))
    etape("Voix reçues", len(taches), len(taches))


def voix_distantes(demande):
    moteur = demande.get("moteur")
    if moteur == "openai":
        noms = ["alloy", "ash", "ballad", "coral", "echo", "fable", "nova", "onyx", "sage", "shimmer", "verse"]
        return [{"id": n, "nom": n.capitalize(), "description": "Voix d'OpenAI (multilingue)"} for n in noms]
    cle = demande.get("cle") or ""
    if not cle:
        raise Echec("cle_absente", "Aucune clé ElevenLabs. Branche ElevenLabs dans Préférences > Connexions IA.")
    base = (demande.get("url_base") or "https://api.elevenlabs.io").rstrip("/")
    data = json.loads(http(urllib.request.Request(f"{base}/v1/voices", headers={"xi-api-key": cle})))
    sortie = []
    for v in data.get("voices", []):
        lab = v.get("labels") or {}
        desc = ", ".join(x for x in [lab.get("gender"), lab.get("accent"), lab.get("description"), lab.get("use_case")] if x)
        sortie.append({"id": v.get("voice_id"), "nom": v.get("name"), "description": desc})
    return sortie


# ---- L'écoute par Whisper ----------------------------------------------------------------------

def ecouter(fichiers, ecoute):
    """Transcrit tous les fichiers en un seul processus (le modèle se charge une fois)."""
    if not ecoute or not ecoute.get("python") or not ecoute.get("script") or not ecoute.get("modele"):
        return {}
    args = [ecoute["python"], ecoute["script"], "--model", ecoute["modele"], "--language", ecoute.get("langue") or "fr"]
    if ecoute.get("ffmpeg_dossier"):
        args += ["--ffmpeg_path", ecoute["ffmpeg_dossier"]]
    proc = subprocess.Popen(args + [str(f) for f in fichiers], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    textes = {}
    for ligne in proc.stdout:
        if ligne.startswith("KORACUT-ECOUTE "):
            o = json.loads(ligne[15:])
            textes[o["fichier"]] = o["texte"]
            etape("Écoute par Whisper", len(textes), len(fichiers))
    proc.wait()
    return textes


# ---- Le nettoyage ------------------------------------------------------------------------------

def resserrer(entree, sortie, pause_max):
    r = subprocess.run([FFMPEG, "-hide_banner", "-i", entree, "-af", f"silencedetect=n=-42dB:d={pause_max}", "-f", "null", "-"],
                       capture_output=True, text=True).stderr
    a = [float(x) for x in re.findall(r"silence_start: ([0-9.]+)", r)]
    b = [float(x) for x in re.findall(r"silence_end: ([0-9.]+)", r)]
    coupes = [(x + pause_max / 2, y - pause_max / 2) for x, y in zip(a, b) if y - x > pause_max and x > 0.05]
    if not coupes:
        shutil.copyfile(entree, sortie)
        return
    expr = "+".join(f"between(t,{x:.4f},{y:.4f})" for x, y in coupes)
    ffmpeg("-i", entree, "-af", f"aselect='not({expr})',asetpts=N/SR/TB", sortie)


def nettoyer(brut, sortie, pause_max, lufs, vitesse):
    court, tmp = f"{sortie}.court.wav", f"{sortie}.tmp.wav"
    chaine = CHAINE + (f",atempo={vitesse:.3f}" if abs(vitesse - 1.0) > 0.005 else "")
    try:
        ffmpeg("-i", brut, "-af", chaine, "-ar", "48000", "-ac", "2", court)
        resserrer(court, tmp, pause_max)
        m = subprocess.run([FFMPEG, "-hide_banner", "-i", tmp, "-af", f"loudnorm=I={lufs}:TP=-1.5:LRA=11:print_format=json", "-f", "null", "-"],
                           capture_output=True, text=True).stderr
        j = json.loads(m[m.rindex("{"):m.rindex("}") + 1])
        ln = (f"loudnorm=I={lufs}:TP=-1.5:LRA=11:linear=true:measured_I={j['input_i']}:measured_TP={j['input_tp']}:measured_LRA={j['input_lra']}:"
              f"measured_thresh={j['input_thresh']}:offset={j['target_offset']},areverse,afade=t=in:d=0.02,areverse")
        ffmpeg("-i", tmp, "-af", ln, "-ar", "48000", "-ac", "2", "-c:a", "pcm_s24le", str(sortie))
    finally:
        for f in (court, tmp):
            if os.path.exists(f):
                os.remove(f)


def mesurer(fichier):
    r = subprocess.run([FFMPEG, "-hide_banner", "-nostats", "-i", str(fichier), "-af", "ebur128=peak=true", "-f", "null", "-"],
                       capture_output=True, text=True).stderr
    resume = r[r.rfind("Summary:"):] if "Summary:" in r else r
    i = re.search(r"I:\s+(-?[\d.]+) LUFS", resume)
    p = re.search(r"Peak:\s+(-?[\d.]+|-inf) dBFS", resume)
    return (float(i.group(1)) if i else None, float(p.group(1)) if p and p.group(1) != "-inf" else None)


# ---- Les modes ---------------------------------------------------------------------------------

def generer(demande):
    phrases = demande.get("phrases") or []
    if not phrases:
        raise Echec("entree_invalide", "Aucune phrase à dire.")
    moteur = demande.get("moteur") or "local"
    prises = max(1, min(int(demande.get("prises") or 3), 5))
    vitesse = max(0.8, min(float(demande.get("vitesse") or 1.0), 1.25))
    pause_max = float(demande.get("pause_max") or 0.36)
    lufs = float(demande.get("lufs") or -16)
    fonction = {"local": local, "elevenlabs": elevenlabs, "openai": openai}.get(moteur)
    if not fonction:
        raise Echec("entree_invalide", f"Moteur inconnu : {moteur}.")
    with tempfile.TemporaryDirectory(prefix="koracut-voix-") as d:
        taches = [(f"p{n}__{k}", a_dire(p)) for n, p in enumerate(phrases) for k in range(prises)]
        fonction(taches, d, demande)
        prises_faites = [Path(d) / f"{i}.wav" for i, _ in taches if (Path(d) / f"{i}.wav").exists()]
        entendus = ecouter(prises_faites, demande.get("ecoute"))
        for n, p in enumerate(phrases):
            notes = []
            for k in range(prises):
                w = Path(d) / f"p{n}__{k}.wav"
                if not w.exists():
                    continue
                t = entendus.get(str(w))
                f = fidelite(p["texte"], t) if t is not None else None
                notes.append({"fidelite": f, "duree": duree(w), "entendu": t, "fichier": w})
            if not notes:
                dire("KORACUT-VOIX-PHRASE", {"id": p["id"], "erreur": "Aucune prise n'a été produite."})
                continue
            # La plus fidèle ; à égalité, la plus courte (moins de pauses).
            notes.sort(key=lambda x: (round(x["fidelite"], 2) if x["fidelite"] is not None else 0, -x["duree"]), reverse=True)
            meilleure = notes[0]
            sortie = Path(p["sortie"])
            sortie.parent.mkdir(parents=True, exist_ok=True)
            etape("Nettoyage de la voix", n, len(phrases))
            nettoyer(str(meilleure["fichier"]), sortie, pause_max, lufs, vitesse)
            niveau, crete = mesurer(sortie)
            dire("KORACUT-VOIX-PHRASE", {
                "id": p["id"], "fichier": str(sortie), "duree": round(duree(sortie), 3),
                "fidelite": None if meilleure["fidelite"] is None else round(meilleure["fidelite"], 3),
                "entendu": meilleure["entendu"], "dit": a_dire(p),
                "douteux": mots_douteux(p["texte"], meilleure["entendu"]) if meilleure["entendu"] is not None else [], "lufs": niveau, "crete": crete,
                "prises": [{"fidelite": None if x["fidelite"] is None else round(x["fidelite"], 3), "duree": round(x["duree"], 2), "entendu": x["entendu"]} for x in notes],
            })


def installer(demande):
    """Environnement Python du moteur local, puis le modèle et les voix : après l'accord de la personne."""
    CACHE.mkdir(parents=True, exist_ok=True)
    if not PY_KYUTAI.exists():
        base = demande.get("python_systeme") or ""
        if not base:
            raise Echec("python_absent", "Python 3.10 ou plus récent est nécessaire.")
        etape("Création de l'environnement Python", 0, 3)
        r = subprocess.run([base, "-m", "venv", str(CACHE / "kyutai")], capture_output=True, text=True)
        if r.returncode != 0:
            raise Echec("installation", "L'environnement Python n'a pas pu être créé : " + r.stderr.strip()[-300:])
    pip = [str(PY_KYUTAI), "-m", "pip", "install", "-q", "-U"]
    if moteur_local() == "mlx":
        etape("Installation du moteur (moshi_mlx)", 1, 3)
        r = subprocess.run(pip + ["pip", "moshi_mlx>=0.3", "huggingface_hub"], capture_output=True, text=True)
    else:
        # PyTorch d'abord, depuis l'index officiel : la roue CUDA seulement si une carte NVIDIA répond,
        # sinon la roue processeur (environ 200 Mo au lieu de 2 à 3 Go de bibliothèques CUDA inutiles).
        # Sur Mac Intel, la roue ordinaire de PyPI est déjà sans CUDA.
        index = None
        if sys.platform != "darwin":
            index = "https://download.pytorch.org/whl/cu124" if carte_nvidia() else "https://download.pytorch.org/whl/cpu"
        etape("Installation de PyTorch" + (" (carte NVIDIA)" if index and "cu124" in index else ""), 1, 3)
        r = subprocess.run(pip + ["pip"], capture_output=True, text=True)
        if r.returncode == 0:
            # moshi 0.2.x demande torch >= 2.2 et < 2.10 : la même borne ici, sinon pip réinstalle torch ensuite.
            r = subprocess.run(pip + ["torch>=2.2,<2.10"] + (["--index-url", index] if index else []), capture_output=True, text=True)
        if r.returncode == 0:
            etape("Installation du moteur (moshi)", 1, 3)
            r = subprocess.run(pip + ["moshi>=0.2.11,<0.3", "huggingface_hub"], capture_output=True, text=True)
    if r.returncode != 0:
        raise Echec("installation", "Le moteur n'a pas pu être installé : " + r.stderr.strip()[-400:])
    etape("Téléchargement du modèle", 2, 3)
    code = ("import sys\nfrom huggingface_hub import snapshot_download, hf_hub_download\n"
            f"snapshot_download({MODELE!r}, allow_patterns=['*.safetensors','*.json','*.model'])\n"
            "for v in sys.argv[1:]:\n"
            f"    hf_hub_download({DEPOT_VOIX!r}, v + '.1e68beda@240.safetensors')\n")
    r = subprocess.run([str(PY_KYUTAI), "-c", code, *VOIX_LOCALES], capture_output=True, text=True, env={**os.environ, "HF_HUB_DISABLE_XET": "1"})
    if r.returncode != 0:
        raise Echec("installation", "Le téléchargement du modèle a échoué : " + r.stderr.strip()[-400:])
    etape("Prêt", 3, 3)


def main():
    global FFMPEG
    try:
        demande = json.loads(sys.stdin.read() or "{}")
    except json.JSONDecodeError as e:
        dire("KORACUT-VOIX-ERREUR", {"code": "entree_invalide", "message": f"Demande illisible : {e}"})
        return 2
    FFMPEG = demande.get("ffmpeg") or shutil.which("ffmpeg") or "ffmpeg"
    try:
        mode = demande.get("mode") or "generer"
        if mode == "installer":
            installer(demande)
        elif mode == "voix_distantes":
            dire("KORACUT-VOIX-LISTE", voix_distantes(demande))
        else:
            generer(demande)
    except Echec as e:
        dire("KORACUT-VOIX-ERREUR", {"code": e.code, "message": e.message})
        return 1
    except Exception as e:  # noqa: BLE001 : tout ce qui casse doit remonter en clair à l'application
        dire("KORACUT-VOIX-ERREUR", {"code": "inattendu", "message": f"{type(e).__name__} : {e}"})
        return 1
    dire("KORACUT-VOIX-FIN", {})
    return 0


if __name__ == "__main__":
    sys.exit(main())
