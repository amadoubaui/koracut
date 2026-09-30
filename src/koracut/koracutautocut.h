/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracutducking.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

/** @brief Coupe automatique (type AutoCut), la partie calcul.
 *
 * Demande d'Amadou Ba du 23 septembre 2026. Décisions et valeurs dans
 * « 15 - Coupe automatique (type AutoCut).md ». Ce fichier ne connaît pas la timeline : il reçoit
 * un niveau de voix par image et rend des intervalles à couper. Il se teste seul
 * (tests/koracutautocuttest.cpp) contre la référence mesurée par ffmpeg sur le jeu d'essai.
 */
namespace KoraCutAutoCut {

using Interval = KoraCutDucking::Interval;

/** @brief Les trois rythmes, qui règlent d'un coup les durées et les marges. */
enum class Rythme { Pose, Normal, Nerveux };

struct SilenceSettings
{
    bool autoThreshold = true; ///< seuil mesuré sur le bruit de fond de la voix
    double thresholdDb = -40.; ///< seuil fixé à la main (dBFS, crête), si autoThreshold est faux
    int minSilenceMs = 500;    ///< un silence plus court n'est pas coupé
    int minSpeechMs = 200;     ///< un son plus court que ça, entre deux silences, n'est pas de la parole
    int padBeforeMs = 100;     ///< air gardé avant que la parole reprenne
    int padAfterMs = 150;      ///< air gardé après que la parole s'arrête

    /** @brief Les valeurs d'un rythme. Posé laisse respirer, Nerveux coupe serré. */
    static SilenceSettings forRythme(Rythme rythme);
};

/** @brief Le seuil automatique : entre le bruit de fond et le niveau de la voix.
 * Bruit de fond = 10e centile, voix = 90e centile des images non vides ; le seuil est au quart de
 * l'écart en partant du bruit, borné entre -60 et -25 dBFS. Une voix de synthèse sans aucun bruit
 * donne donc -60, un micro de salle bruyante un seuil plus haut. */
double estimateThreshold(const QVector<double> &levelsDb);

/** @brief Les silences à couper, en images de timeline : [début, fin[.
 * @param levelsDb niveau de voix par image (le plus fort des pistes de voix), -144 là où rien ne joue
 * @param usedThreshold reçoit le seuil réellement employé (utile quand il est automatique)
 * Chaque silence rendu est déjà rogné de ses marges. Un silence en tout début ou toute fin de
 * timeline n'a de marge que du côté de la parole. */
QList<Interval> silenceIntervals(const QVector<double> &levelsDb, const SilenceSettings &settings, double fps, double *usedThreshold = nullptr);

/** @brief Durée totale des intervalles, en images. */
int totalFrames(const QList<Interval> &intervals);

/** @brief Fusionne des intervalles qui se touchent ou se chevauchent, et les trie. */
QList<Interval> mergeIntervals(QList<Interval> intervals);

// ---------------------------------------------------------------------------------------------
// Lot 2 : les mots parasites.

/** @brief Un mot transcrit, en secondes de timeline. */
struct Word
{
    QString text;
    double start = 0.;
    double end = 0.;
};

/** @brief Lit la sortie JSON du script koracut_mots.py : {"mots": [{"m", "d", "f", "p"}, ...]}. */
QList<Word> parseWords(const QByteArray &json);

/** @brief Fusionne les mots de plusieurs sources (plusieurs micros, le son d'une caméra) : un mot qui
 * chevauche de plus de moitié un mot identique déjà gardé est le même mot entendu par un autre micro,
 * il part. Sans ça, une phrase captée par trois micros devient une phrase dite trois fois.
 * @return les mots triés par début */
QList<Word> mergeTranscriptions(QList<Word> words);

/** @brief Minuscules, sans ponctuation autour : « Eux, » devient « eux ». */
QString normalizeWord(const QString &word);

struct FillerSettings
{
    /** Toujours des mots parasites. */
    QStringList sure = {QStringLiteral("euh"), QStringLiteral("heu"), QStringLiteral("euuh"), QStringLiteral("hum"), QStringLiteral("hmm"),
                        QStringLiteral("mmh"), QStringLiteral("mh")};
    /** Des mots qui existent (« eux »), mais qui sont un « euh » mal transcrit quand ils sont
     *  courts et isolés entre deux pauses. Whisper écrit souvent « euh » ainsi. */
    QStringList suspects = {QStringLiteral("eux"), QStringLiteral("eh"), QStringLiteral("e"), QStringLiteral("heuh")};
    /** Signalés pour relecture, jamais coupés d'office : « ben », « du coup »… */
    QStringList ambiguous = {QStringLiteral("ben"), QStringLiteral("bah"), QStringLiteral("genre"), QStringLiteral("du coup"), QStringLiteral("en fait"),
                             QStringLiteral("bon")};
    int isolatedMaxMs = 450;   ///< un suspect plus long est un vrai mot
    int isolatedPauseMs = 200; ///< pause exigée avant et après un suspect
    int marginMs = 40;         ///< air ajouté autour d'un mot coupé, sans mordre sur ses voisins
    bool voiceWithoutWord = true; ///< une voix que la transcription n'a pas écrite est un « euh » probable
    int voiceWithoutWordMinMs = 200;
    int voiceWithoutWordMaxMs = 1200;
};

enum class FillerKind { Sure, Isolated, Ambiguous, VoiceWithoutWord };

struct Filler
{
    Interval frames;
    FillerKind kind = FillerKind::Sure;
    QString text; ///< le ou les mots, vide pour une voix sans mot
};

// ---------------------------------------------------------------------------------------------
// Lot 3 : les répétitions (prises refaites).

struct RepeatSettings
{
    int minWords = 3;        ///< une suite de moins de mots n'est pas une reprise (« c'est, c'est » en est une vraie répétition naturelle)
    int windowMs = 15000;    ///< la reprise doit commencer dans ce délai après la première prise
    int maxMismatches = 0;   ///< mots différents tolérés dans la suite (0 : identique mot pour mot)
};

struct Repeat
{
    Interval frames;  ///< la première prise et ce qui la suit jusqu'à la reprise : ce qu'on coupe
    QString text;     ///< le début de la phrase reprise, pour la marque
    int words = 0;    ///< longueur de la suite reprise
};

/** @brief Les prises refaites : une suite d'au moins minWords mots qui revient peu après. On garde
 * la dernière prise, comme TimeBolt ; la première et ce qui la sépare de la reprise sont à couper.
 * @param words les mots transcrits, en secondes de timeline, triés */
QList<Repeat> findRepeats(const QList<Word> &words, const RepeatSettings &settings, double fps);

// ---------------------------------------------------------------------------------------------
// Lot 5 : le choix de la caméra selon qui parle.

struct CameraSettings
{
    double thresholdDb = -40.; ///< sous ce niveau, un micro n'entend personne
    double marginDb = 6.;      ///< l'intervenant le plus fort doit l'être d'autant : un micro entend aussi les autres
    int minShotMs = 2000;      ///< un plan plus court n'est pas gardé : on reste sur le précédent
    int maxShotMs = 10000;     ///< au-delà, on passe par le plan large, s'il y en a un
    int wideCutawayMs = 2000;  ///< durée d'un passage par le plan large
    bool hasWide = false;      ///< un plan large existe
};

/** @brief Un plan : qui on montre, sur quelle durée. speaker = -1 pour le plan large. */
struct Shot
{
    Interval frames;
    int speaker = 0;
};

/** @brief Le montage des caméras : un plan par prise de parole.
 * @param speakers un niveau par image pour chaque intervenant (son micro)
 * Quand personne ne parle, on reste sur le plan en cours. Quand deux parlent en même temps, plan
 * large s'il existe. Les plans trop courts sont absorbés par le précédent. */
QList<Shot> cameraPlan(const QList<QVector<double>> &speakers, const CameraSettings &settings, double fps);

/** @brief Les mots parasites, en images de timeline, triés.
 * @param words les mots transcrits, en secondes de timeline
 * @param levelsDb niveau de voix par image (pour « voix sans mot »), peut être vide
 * @param thresholdDb seuil de parole, le même que pour les silences */
QList<Filler> findFillers(const QList<Word> &words, const QVector<double> &levelsDb, double thresholdDb, const FillerSettings &settings, double fps);

}
