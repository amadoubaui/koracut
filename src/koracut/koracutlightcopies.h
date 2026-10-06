/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Chantier 10 (document 6, journal du 4 octobre 2026) : copies légères tout-intra pour les sources lourdes.
//
// Un rush HEVC, AV1, VP9, 10 bits ou 4K se redécode depuis son image clé à chaque saut : 156 ms pour reculer
// de 14 images sur un rush d'iPhone, 3 ms sur une copie H.264 tout-intra en 540p. Ici, chaque vidéo lourde
// posée dans une timeline reçoit une telle copie, sur tous les profils de machine : une à la fois, en priorité
// basse, suspendue pendant la lecture et les gestes, sous un plafond de disque. La copie devient le proxy du
// clip (propriété kdenlive:proxy) : tout le reste est le mécanisme de Kdenlive, et l'export reprend l'original.
//
// Réglages, koracutrc, groupe [KoraCut] :
// - copiesLegeres : vrai par défaut ; faux = aucune copie, et les clips reviennent à leur original
// - copiesLegeresPlafondMo : place totale des copies, 6144 par défaut ; au-delà, les plus anciennes partent
// - copiesLegeresLibreMinMo : pas d'encodage s'il reste moins que ça sur le disque, 8192 par défaut
// - copiesLegeresHauteur : petit côté de la copie, 0 = selon l'aperçu du moniteur (540, ou 720)

#pragma once

#include "jobs/abstracttask.h"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <memory>

class ProjectClip;

/** Les règles, sans interface ni projet : ce que les tests vérifient. */
namespace KoraCutLightCopy {

struct Source {
    QString codec;
    QString pixFormat;
    int width{0};
    int height{0};
    bool hasAlpha{false};
};

/** Codecs où chaque image se décode seule : un saut y est déjà bon marché. */
bool isIntraCodec(const QString &codec);
/** Faut-il une copie légère ? HEVC, AV1, VP9, plus de 8 bits, ou 4K, hors codecs intra et couche alpha. */
bool isHeavy(const Source &source);

/** Le petit côté visé selon l'image d'aperçu du moniteur : 540, ou 720 si l'aperçu fait 720 ou plus. */
int targetShortSide(const QSize &previewFrame);
/** Le petit côté de la copie : jamais plus grand que la source, toujours pair. */
int shortSideFor(int width, int height, int target);

enum class Encoder { VideoToolbox, Nvenc, Qsv, Amf, Software };
/** L'encodeur matériel de ce système parmi ceux qui ont réussi l'essai du premier lancement, sinon logiciel. */
Encoder encoderFor(const QStringList &supportedHwCodecs);
QString encoderId(Encoder encoder);
/** Le décodeur matériel à demander à ffmpeg pour lire la source (vide : logiciel). */
QString hwDecoderFor(Encoder encoder);

/** Les arguments de ffmpeg. @p keepCodedOrientation : le clip impose sa rotation (propriétés rotate ou
 * autorotate), on ne tourne pas l'image ; sinon la rotation est appliquée et la copie sort droite.
 * @p decodeThreads : fils du décodage logiciel, 0 = au choix de ffmpeg.
 * @p streams : les flux à garder, par leur numéro dans la source (la vidéo et les sons que MLT sait lire) ;
 * vide = la première vidéo et le premier son. Un flux de son sans décodeur (le son spatial des iPhone
 * récents) ferait échouer ffmpeg : on ne le demande pas. */
QStringList ffmpegArguments(const QString &source, const QString &dest, int shortSide, Encoder encoder, const QString &hwDecoder, bool keepCodedOrientation,
                            int decodeThreads, const QStringList &streams = {});

/** Place à prévoir, mesurée sur des rushs d'iPhone (document 6) : environ 50 Mo par minute en 540p
 * avec VideoToolbox, 80 avec x264 ; 1,4 fois plus en 720p. */
qint64 estimateBytes(double seconds, int shortSide, Encoder encoder);

QString copyFileName(const QString &hash, int shortSide);
bool isCopyFile(const QString &fileName);

struct File {
    QString path;
    qint64 size{0};
    qint64 lastUsedSecs{0};
    bool inUse{false}; ///< tenue par le projet ouvert : on ne l'efface pas
};
struct Budget {
    qint64 capBytes{0};
    qint64 minFreeBytes{0};
};
enum class Verdict { Go, CapReached, DiskFull };
struct Plan {
    Verdict verdict{Verdict::Go};
    QStringList evict; ///< à effacer avant d'encoder, les plus anciennes d'abord
};
/** Peut-on écrire @p neededBytes ? Sous le plancher de disque libre : non. Au-dessus du plafond : les
 * copies les plus anciennes que le projet ne tient pas partent ; si ça ne suffit pas : non. */
Plan plan(const QList<File> &files, qint64 freeBytes, qint64 neededBytes, const Budget &budget);

/** Vrai si l'ordinateur est sur batterie sous 30 %. Faux quand on ne sait pas. */
bool onLowBattery();

/** Ce que l'encodage en cours partage avec le fil principal : le processus, à suspendre et à reprendre. */
class Process
{
public:
    void setPid(qint64 pid);
    /** Suspend ou reprend le processus (SIGSTOP et SIGCONT, NtSuspendProcess sous Windows). */
    void setSuspended(bool suspended);
    bool suspended() const;

private:
    void apply();
    mutable QMutex m_mutex;
    qint64 m_pid{0};
    bool m_wanted{false};
    bool m_applied{false};
};

} // namespace KoraCutLightCopy

/** L'encodage d'une copie : une tâche du chutier, pour que la personne la voie avancer sur le clip. */
class KoraCutLightCopyTask : public AbstractTask
{
    Q_OBJECT
public:
    struct Request {
        QString binId;
        QString source;
        QString dest;
        int shortSide{540};
        bool keepCodedOrientation{false};
        QStringList streams;
        double seconds{0};
        QString ffmpeg;
    };
    KoraCutLightCopyTask(const ObjectId &owner, const Request &request, const std::shared_ptr<KoraCutLightCopy::Process> &process, QObject *clip,
                         QObject *receiver);

protected:
    void run() override;

private:
    bool attempt(KoraCutLightCopy::Encoder encoder, const QString &hwDecoder, const QString &partial, QString *log);
    Request m_request;
    std::shared_ptr<KoraCutLightCopy::Process> m_process;
    QPointer<QObject> m_receiver;
};

/** La file : quels clips, quand, et sous quel plafond. Vit dans le fil principal. */
class KoraCutLightCopies : public QObject
{
    Q_OBJECT
public:
    static KoraCutLightCopies *instance();

    static bool enabled();
    /** Coupe ou remet les copies. Coupé : l'encodage s'arrête et les clips reviennent à leur original. */
    static void setEnabled(bool on);
    static KoraCutLightCopy::Budget budget();

    /** Un clip du chutier vient d'être posé dans une timeline (ou y était à l'ouverture du projet). */
    static void noteTimelineClip(const QString &binId);
    /** Vrai si le projet ouvert a au moins un clip sur une copie légère (le rendu d'aperçu prend alors les originaux). */
    static bool projectUsesCopies();
    /** Pour la porte (app performance), le Diagnostic et le banc. */
    static QJsonObject stateJson();
    /** Vrai tant qu'il reste une copie à faire, à poser ou en cours. */
    static bool busy();
    /** Au lancement : retire les ffmpeg de copies restés suspendus après un plantage (Unix). */
    static void removeOrphans();

public Q_SLOTS:
    void taskFinished(const QString &binId, const QString &source, const QString &dest, bool ok, bool canceled, int elapsedMs, const QString &encoder,
                      const QString &log);

private:
    KoraCutLightCopies();
    void schedule(int msecs = 1500);
    void tick();
    void rescan();
    void applyReady();
    void revertAll();
    bool startNext();
    QList<KoraCutLightCopy::File> files() const;
    QString destFor(const std::shared_ptr<ProjectClip> &clip, int shortSide) const;
    int shortSide() const;
    void block(const QString &reason, const QString &message);

    QTimer m_tick;
    QTimer m_watch;
    QPointer<QObject> m_doc;
    QStringList m_pending;
    QMap<QString, QString> m_ready; ///< identifiant du chutier → copie écrite, à poser quand la main est libre
    /** @brief Poser ou retirer une copie ne compte pas comme une modification du projet : si la personne n'avait
     *  rien changé avant, le projet redevient « enregistré » une fois le clip rechargé (rien dans l'historique). */
    void keepClean();
    QTimer m_cleanTimer;
    int m_cleanUndoIndex{-1}; ///< l'indice de l'historique quand le projet était propre, -1 : rien à rétablir
    QSet<QString> m_failed;         ///< sources qui ont échoué pendant cette session
    QString m_currentBinId;
    QString m_currentName;
    std::shared_ptr<KoraCutLightCopy::Process> m_process;
    QString m_blocked; ///< « plafond », « disque », « batterie » ou vide
    QSet<QString> m_told;
    int m_done{0};
    qint64 m_encodeMs{0};
    QString m_lastEncoder;
    bool m_announced{false};
    int m_retries{0};
};
