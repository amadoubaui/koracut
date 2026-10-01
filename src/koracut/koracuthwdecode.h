/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Chantier 10 (document 6, journal du 1er octobre 2026) : le décodage matériel n'est demandé à MLT
// que si tous les clips du projet passent vraiment par la puce.
//
// MLT 7.41 ne sait choisir le décodage matériel que pour tout le processus (MLT_AVFORMAT_HWACCEL,
// relu à chaque ouverture d'un fichier). Et quand la puce accepte un flux puis échoue à le décoder
// (H.264 de caméra à plusieurs PPS sous VideoToolbox), il n'y a pas de repli : l'image sort blanche.
// Ici, chaque clip vidéo est essayé une fois (ffmpeg, douze images) ; tant qu'un clip du projet
// échoue ou attend son essai, tout le projet est décodé par le processeur.

#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

class QProcessEnvironment;

class KoraCutHwDecode : public QObject
{
    Q_OBJECT
public:
    enum class State { Ok, Pending, Refused };
    struct Clip
    {
        QString name;
        QString path;
        QString format;
        State state{State::Ok};
    };

    static KoraCutHwDecode *instance();

    /** Le décodeur choisi dans les réglages (vide : logiciel). */
    static QString wanted();
    /** Celui que MLT doit prendre maintenant : vide si un clip du projet ne passe pas par la puce. */
    static QString effective();
    /** Pose MLT_AVFORMAT_HWACCEL pour ce processus. À appeler quand le réglage change. */
    static void apply();
    /** Même chose pour un processus de rendu (aperçu, export). */
    static void applyTo(QProcessEnvironment &env);

    /** Un clip vidéo du projet. @p codec vide : inconnu (proxy), on essaie quand même.
     *  @p legacy : le clip était déjà dans le projet à son ouverture, ses caches sont à soigner s'il échoue. */
    static void noteClip(const void *owner, const QString &name, const QString &path, const QString &codec, const QString &pixFmt, bool legacy);
    static void forgetClip(const void *owner);
    /** Les clips qui font retomber le projet en logiciel (refusés ou en cours d'essai). */
    static QList<Clip> softwareClips();
    /** Pour le Diagnostic : décodage demandé, décodage en cours, clips en cause. */
    static QJsonObject stateJson();
    /** Attend la fin des essais en cours (avant un export). Faux si le délai est dépassé. */
    static bool waitForTrials(int msecs);

    /** Vrai si ce décodeur matériel peut accepter un flux puis échouer : il faut un essai par fichier. */
    static bool needsTrial(const QString &hw, const QString &codec);
    /** Lit la sortie d'erreur de ffmpeg : vrai si la puce a échoué à décoder. */
    static bool outputSaysRefused(const QString &ffmpegStderr);
    /** « H.264 4:2:2 10 bits » à partir des noms de FFmpeg. */
    static QString describeFormat(const QString &codec, const QString &pixFmt);
    /** Le décodeur matériel de ce système quand les réglages n'en demandent pas (pour soigner les caches). */
    static QString platformDecoder();

    /** L'essai : vrai si @p path se décode avec @p hw. Les tests le remplacent. */
    using Trial = std::function<bool(const QString &hw, const QString &path)>;
    static void setTrial(const Trial &trial);
    /** L'essai réel, par ffmpeg. */
    static bool ffmpegTrial(const QString &ffmpeg, const QString &hw, const QString &path);
    /** Tests : oublie les clips et les verdicts en mémoire, et garde les verdicts dans @p verdictsPath
     *  (effacé d'abord). Vide : retour au fichier du cache de l'application, sans y toucher. */
    static void resetForTests(const QString &verdictsPath = QString());

    /** Pendant le chargement d'un fichier, avant que son clip existe : le projet reste en logiciel. */
    class Hold
    {
    public:
        explicit Hold(const QString &path);
        ~Hold();
        Hold(const Hold &) = delete;
        Hold &operator=(const Hold &) = delete;
    };

Q_SIGNALS:
    /** Le décodage en cours ou la liste des clips en cause a changé. */
    void changed();
    /** Un clip ne passe pas par la puce. @p legacy : ses vignettes et les rendus d'aperçu sont peut-être faux. */
    void clipRefused(const QString &path, bool legacy);

private:
    KoraCutHwDecode() = default;
};
