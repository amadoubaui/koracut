/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

/** @class MachineProfile
    @brief KoraCut : détecte la puissance de la machine et propose des réglages adaptés.

    Trois profils (voir « 6 - Performance et machines modestes.md », section 5) :
    - Modeste     : 4 cœurs ou moins, ou moins de 12 Go de RAM, ou disque lent
    - Moyen       : 6 à 8 cœurs, 12 à 24 Go
    - Confortable : Apple Silicon, ou encodeur matériel dédié, ou plus de 24 Go

    Cette classe ne lit ni n'écrit KdenliveSettings : elle mesure et recommande. Les
    réglages sont posés par KoraCutPerformance (src/koracut/koracutperformance.cpp), au
    premier lancement et depuis les Préférences, section Performances. Tout ce qui décide
    (classement, critère de proxy, aperçu adaptatif) est ici, sans interface, pour les tests.
 */
class MachineProfile
{
public:
    enum class Level { Modest, Medium, Comfortable };

    /** Ce qui a été mesuré sur la machine. */
    struct Hardware
    {
        int logicalCores{1};
        /** RAM physique en Mo, 0 si la mesure a échoué. */
        qint64 ramMb{0};
        /** x86_64, arm64... (QSysInfo::currentCpuArchitecture). */
        QString cpuArchitecture;
        bool isAppleSilicon{false};
        /** Vrai si un encodeur matériel dédié a été détecté (NVENC, AMF). À renseigner par l'appelant
            à partir de KdenliveSettings::supportedHWCodecs(), la classe ne lance pas ffmpeg elle-même. */
        bool hasDedicatedHwEncoder{false};
        /** Espace libre du volume de cache, en Mo, 0 si inconnu. */
        qint64 cacheVolumeFreeMb{0};
        /** Débit d'écriture mesuré sur le volume de cache, en Mo/s, 0 si non mesuré. */
        double diskWriteMbPerSec{0.};
    };

    /** Les réglages proposés pour un profil. Les noms suivent les clés de kdenlivesettings.kcfg. */
    struct Recommendation
    {
        Level level{Level::Medium};
        /** Diviseur de résolution du moniteur : 1 = pleine, 2 = 720p, 4 = 540p (clé previewScaling). */
        int previewScaling{2};
        /** Plus petite résolution autorisée par l'aperçu adaptatif en lecture, 0 = pas d'adaptation. */
        int adaptivePreviewFloorHeight{540};
        bool enableProxy{true};
        bool generateProxy{true};
        /** Largeur au-dessus de laquelle un proxy est généré (clé proxyminsize). */
        int proxyMinSize{1920};
        /** Largeur du proxy produit (clé proxyscale). */
        int proxyScale{960};
        /** Toujours générer un proxy pour HEVC, AV1 et VP9, quelle que soit la taille. */
        bool proxyForHeavyCodecs{false};
        int proxyThreads{2};
        /** Hauteur du rendu d'aperçu de timeline, 0 = résolution du projet. */
        int previewRenderHeight{720};
        bool autoPreview{true};
        /** Threads d'export (clé processingthreads). */
        int processingThreads{4};
        /** Taille maximale du cache disque, en Mo (clé maxcachesize). */
        int maxCacheSizeMb{5000};
        /** Nettoyage automatique des aperçus des projets non ouverts depuis N mois. */
        int cacheCleanupMonths{4};
        bool videoThumbnails{true};
        /** Aperçu simplifié des effets lourds pendant la lecture. */
        bool simplifiedHeavyEffects{false};
        /** Nombre maximal d'images sautées par seconde toléré : fps / dropDivisor. */
        int dropDivisor{4};
        /** Largeur à partir de laquelle un média HEVC, AV1, VP9 ou 10 bits reçoit un proxy,
            -1 = pas de règle à part (seule la largeur compte). */
        int proxyHeavyMinWidth{1920};
        /** Nom du profil de proxy dans data/encodingprofiles.rc, section [proxy]. */
        QString proxyProfile;
        /** Plafond du cache producteurs MLT (décodeurs ouverts), 0 = celui de Kdenlive. */
        int producerCacheMax{0};
    };

    /** Les règles de proxy d'un projet, lues dans ses propriétés. */
    struct ProxyRule
    {
        bool generate{false};
        int minWidth{1000};
        int heavyMinWidth{-1};
    };

    /** L'aperçu adaptatif du moniteur (document 6, section 3c) : descend d'un cran de résolution
        après deux secondes d'images sautées, revient à la résolution choisie à la pause. Retient
        d'une lecture à l'autre le cran qui a suffi, et tente de remonter d'un cran après une
        lecture sans saut. Les valeurs sont celles de la clé previewScaling (0 = 1:1, 1 = 1080p,
        2 = 720p, 4 = 540p, 8 = 360p, 16 = 270p). */
    class AdaptiveScaling
    {
    public:
        /** @p floorScaling : le cran le plus bas permis (8 = 360p) ; 0 coupe l'adaptation. */
        void configure(int floorScaling);
        bool enabled() const { return m_floor > 0; }
        /** Lecture lancée avec la résolution choisie par la personne ; rend le cran à poser
            tout de suite (celui retenu de la lecture d'avant), ou -1. */
        int start(int userScaling);
        /** Une seconde de lecture : @p dropped images sautées. Rend le cran à poser, ou -1. */
        int tick(int dropped, double fps);
        /** Pause : rend la résolution choisie à remettre, ou -1 si rien n'a changé. */
        int stop();
        bool degraded() const { return m_playing && m_current != m_user; }
        int current() const { return m_current; }
        int learned() const { return m_learned; }
        /** 0 → 1 → 2 → 4 → 8 → 16 : le rang d'un cran, et le cran suivant (plus bas). */
        static int rank(int scaling);
        static int lower(int scaling);
        static int higher(int scaling);

    private:
        int m_floor{0};
        int m_user{1};
        int m_current{1};
        int m_learned{-1};
        int m_badStreak{0};
        int m_skip{0};
        int m_seconds{0};
        int m_droppedTotal{0};
        bool m_playing{false};
    };

    /** Mesure la machine. Rapide (pas de test disque). */
    static Hardware detect(const QString &cachePath = QString());
    /** Mesure le débit d'écriture du volume de cache avec un fichier temporaire (environ 1 s, 64 Mo au plus). */
    static double measureDiskWriteSpeed(const QString &cachePath);
    /** Classe la machine dans un des trois profils. */
    static Level classify(const Hardware &hw);
    /** Réglages proposés pour un profil, ajustés aux cœurs et au disque mesurés. */
    static Recommendation recommend(Level level, const Hardware &hw);
    /** Nom du profil pour l'interface (traduit). */
    static QString levelName(Level level);
    /** Résumé lisible d'une machine, pour l'assistant et le mode diagnostic. */
    static QString describe(const Hardware &hw);

    /** Identifiants stables des profils (réglage profilMachine, porte, agents). */
    static QString levelId(Level level);
    /** « modeste », « moyen », « confortable » (et modest, medium, comfortable) ; @p ok faux sinon. */
    static Level levelFromId(const QString &id, bool *ok = nullptr);
    /** Le profil en service : KORACUT_PROFIL, sinon le choix enregistré (koracutrc, [KoraCut]
        profilMachine), sinon la détection (faite une fois par lancement). */
    static Level effectiveLevel();
    /** La détection seule, faite une fois par lancement. */
    static Level detectedLevel();
    /** Vrai si le choix vient de la personne ou de KORACUT_PROFIL, pas de la détection. */
    static bool isForced();

    /** HEVC, AV1, VP9, ou un format de pixels à plus de 8 bits. */
    static bool isHeavyCodec(const QString &codecName, const QString &pixFormat = QString());
    /** Faut-il un proxy pour une vidéo de cette largeur, de ce codec ? */
    static bool wantsProxy(const ProxyRule &rule, int width, const QString &codecName, const QString &pixFormat);
    /** Le profil de proxy à utiliser, parmi @p available (clés de [proxy]), selon le profil et les
        encodeurs matériels trouvés (SupportedHWCodecs). Vide : laisser Kdenlive choisir. */
    static QString proxyProfileFor(Level level, const QStringList &hwCodecs, const QStringList &available);
    /** Les décodeurs matériels à essayer sur ce système, du plus sûr au moins sûr. */
    static QStringList hwDecoderCandidates(const QStringList &hwCodecs);
    /** Plafond du cache disque en Mo : 10 % de l'espace libre, entre 2 et 20 Go, jamais plus du quart du libre. */
    static int cacheBudgetMb(qint64 freeMb);
    /** Hauteur maximale de l'aperçu pour un cran previewScaling (0 : pas de plafond). */
    static int scalingHeight(int scaling);
    /** Le cran previewScaling dont la hauteur vaut @p height (360 → 8), 0 si aucune. */
    static int scalingForHeight(int height);
};
