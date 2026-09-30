/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <memory>

class QProcess;
class QTimer;
class TimelineItemModel;

/** @brief Une phrase à dire : le texte (celui du sous-titre), ce qu'on fait dire au moteur s'il
 *  diffère (« afebaba point com »), et le fichier WAV à écrire. */
struct KoraCutVoicePhrase
{
    QString id;
    QString text;
    QString say;
    QString output;
};

/** @brief Une demande de voix off : une ou plusieurs phrases, un moteur, une voix, un ton. */
struct KoraCutVoiceRequest
{
    QList<KoraCutVoicePhrase> phrases;
    /** local, elevenlabs ou openai. */
    QString engine{QStringLiteral("local")};
    /** Vide : la voix par défaut du moteur. */
    QString voice;
    double speed{1.0};
    /** pose, vivant, doux ou serieux. */
    QString tone{QStringLiteral("pose")};
    int takes{3};
};

/** @brief La voix off de synthèse dans KoraCut (chantier 5, 29 septembre 2026).
 *
 * Le travail lourd tourne dans un processus à part (data/scripts/voix/koracut_voix.py), jamais dans
 * le fil principal : le moteur génère plusieurs prises, Whisper (celui de la coupe automatique)
 * écoute chaque prise, la plus fidèle est gardée, nettoyée (bords de silence, pauses, traitement
 * studio, -16 LUFS) et écrite en WAV. La demande passe par l'entrée standard : une clé de service
 * n'apparaît jamais sur une ligne de commande.
 *
 * Moteurs : « local » (Kyutai TTS en local, gratuit, macOS Apple Silicon ; environ 4,1 Go qui ne
 * se téléchargent qu'après l'accord de la personne), « elevenlabs » et « openai » (la clé d'une
 * connexion de la page Connexions IA, lue dans le trousseau au moment de l'appel).
 * Rien ne tourne tant qu'on ne demande pas de voix. Recherche : Recherche/Voix off de synthese.md.
 */
class KoraCutVoiceOver : public QObject
{
    Q_OBJECT
public:
    explicit KoraCutVoiceOver(QObject *parent = nullptr);
    ~KoraCutVoiceOver() override;

    /** @name Ce qui est disponible sur cette machine */
    ///@{
    /** @brief L'état d'un moteur : `{ moteur, nom, pret, raison?, conseil?, ... }`. Pour « local » :
     *  `installe`, `modele_present`, `taille_mo`, `dossiers`, `installation` (en cours ou non). */
    static QJsonObject engineState(const QString &engine);
    /** @brief Les trois moteurs, dans l'ordre où « auto » les essaie. */
    static QStringList engines();
    /** @brief Le moteur que « auto » choisit : le local s'il est prêt, sinon une connexion de voix. */
    static QString autoEngine();
    /** @brief Les voix du moteur local livrées avec l'installation (licence CC-BY 4.0 ou CC0). */
    static QJsonArray localVoices();
    static QString defaultVoice(const QString &engine);
    /** @brief Le moteur local tourne ici (MLX : macOS sur Apple Silicon). */
    static bool localSupported();
    /** @brief Le dossier du moteur local (~/.cache/koracut-voix, ou KORACUT_VOIX_CACHE). */
    static QString cacheDir();
    /** @brief Le dossier du modèle Kyutai dans le cache de Hugging Face. */
    static QString modelDir();
    /** @brief Taille approximative de l'installation du moteur local, en Mo. */
    static int installSizeMb();
    /** @brief Le script de génération (KORACUT_VOIX_SCRIPT pour les essais). */
    static QString scriptPath();
    /** @brief Un Python 3.10 ou plus récent sur la machine, vide s'il n'y en a pas. */
    static QString systemPython();
    ///@}

    /** @name Les fichiers */
    ///@{
    /** @brief `<dossier du projet>/Voix off`, ou le dossier des données du projet s'il n'est pas enregistré. */
    static QString projectVoiceFolder();
    /** @brief « NN - Premiers mots du texte.wav », NN le premier numéro libre du dossier (plus @p skip,
     *  pour nommer d'un coup plusieurs fichiers qui n'existent pas encore). */
    static QString fileNameFor(const QString &folder, const QString &text, const QString &name = QString(), int skip = 0);
    ///@}

    /** @brief Importe les fichiers dans le chutier (dossier « Voix off ») et, pour chaque position
     *  positive, pose le son sur @p trackId (-1 : la piste audio « Voix off », créée si besoin).
     *  Le tout en une seule étape d'annulation. Rend `{ ok, medias: [{id, fichier, clip_id?, position?, decalee?}], piste_id? }`.
     *  @param flexible pour chaque fichier, vrai si sa place peut glisser à la première place libre qui suit. */
    static QJsonObject importAndPlace(const std::shared_ptr<TimelineItemModel> &timeline, const QStringList &files, const QList<int> &positions, int trackId,
                                      const QString &undoText, const QList<bool> &flexible = {});
    /** @brief La piste audio « Voix off » de la séquence, -1 s'il n'y en a pas. */
    static int voiceTrack(const std::shared_ptr<TimelineItemModel> &timeline);

    /** @brief Lance la génération en tâche de fond. Les signaux disent la suite. */
    void generate(const KoraCutVoiceRequest &request);
    /** @brief Les voix d'un moteur en ligne (elevenlabs, openai), en tâche de fond. */
    void listRemoteVoices(const QString &engine);
    void cancel();
    bool isRunning() const;

    /** @brief L'installation du moteur local, partagée par toute l'application (une seule à la fois). */
    static KoraCutVoiceOver *installer();
    /** @brief Installe le moteur local (Python, moshi_mlx, modèle, voix). À n'appeler qu'après l'accord de la personne. */
    void install();
    bool isInstalling() const { return m_installing; }
    int installPercent() const { return m_installPercent; }
    QString installStep() const { return m_installStep; }

Q_SIGNALS:
    void progress(const QString &text, int done, int total);
    /** @brief Une phrase est prête (`{ id, fichier, duree, fidelite, entendu, douteux, prises, lufs, crete }`). */
    void phraseReady(const QJsonObject &phrase);
    /** @brief Tout est fini : `{ ok: true, phrases: [...] }`. */
    void finished(const QJsonObject &result);
    void failed(const QString &code, const QString &message, const QString &advice);
    void remoteVoices(const QJsonArray &voices);
    void installProgress(int percent, const QString &step);
    void installFinished(const QString &error);

private:
    /** Le Python qui lance le script : celui du moteur local, sinon celui de la reconnaissance vocale, sinon celui du système. */
    static QString runnerPython(const QString &engine);
    /** La clé et l'adresse d'une connexion de voix (Connexions IA). Faux sans connexion ou sans clé. */
    static bool remoteCredentials(const QString &engine, QString *key, QString *baseUrl, QString *voice, QString *connection = nullptr);
    void start(const QString &python, const QJsonObject &job);
    void onLine(const QByteArray &line);
    void pollInstallSize();

    QPointer<QProcess> m_process;
    QByteArray m_buffer;
    QJsonArray m_phrases;
    QString m_mode;
    bool m_failed{false};
    bool m_installing{false};
    int m_installPercent{0};
    QString m_installStep;
    QTimer *m_sizeTimer{nullptr};
    qint64 m_sizeAtStart{0};
};
