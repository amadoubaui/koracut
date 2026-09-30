/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Document 24 (30 septembre 2026) : la section Outils de l'inspecteur. Les recettes, la pose et le
// retrait en une étape d'annulation, l'ordre de la chaîne, les outils d'agent, et le fichier d'un
// niveau de stabilisation écrit à partir d'une analyse.

#include "catch.hpp"
#include "test_utils.hpp"

#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "effects/effectsrepository.hpp"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "koracut/koracutagenttools.h"
#include "koracut/koracutcliptools.h"
#include "koracut/koracutstabilizer.h"

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

using KoraCutClipTools::Tool;

namespace {
/** Les outils posés dans la pile, de haut en bas (« outil:variante » par effet). */
QStringList tags(const std::shared_ptr<EffectStackModel> &stack)
{
    QStringList list;
    for (int row = 0; row < stack->rowCount(); ++row) {
        auto effect = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (effect) {
            list << QString::fromUtf8(effect->filter().get("kdenlive:koracut_outil")) + QLatin1Char(':') +
                        QString::fromUtf8(effect->filter().get("kdenlive:koracut_variante"));
        }
    }
    return list;
}

int countTag(const QStringList &list, const QString &tool)
{
    int n = 0;
    for (const QString &t : list) {
        if (t.startsWith(tool + QLatin1Char(':'))) {
            ++n;
        }
    }
    return n;
}
} // namespace

TEST_CASE("Outils du son : les recettes", "[KoraCutClipTools]")
{
    for (const auto &c : KoraCutClipTools::denoiseLevels()) {
        CHECK_FALSE(KoraCutClipTools::recipe(Tool::Denoise, c.id).isEmpty());
        CHECK_FALSE(c.label.isEmpty());
    }
    for (const auto &c : KoraCutClipTools::voiceEffects()) {
        CHECK_FALSE(KoraCutClipTools::recipe(Tool::VoiceEffect, c.id).isEmpty());
        CHECK_FALSE(c.icon.isEmpty());
    }
    CHECK(KoraCutClipTools::voiceEffects().size() == 10);
    CHECK_FALSE(KoraCutClipTools::recipe(Tool::Enhance, QStringLiteral("standard")).isEmpty());
    CHECK(KoraCutClipTools::recipe(Tool::VoiceEffect, QStringLiteral("inconnu")).isEmpty());
    // Le suppresseur de bruit garde son seuil de voix à 0 : c'est ce qui tient le retard à 10 ms.
    const auto moyen = KoraCutClipTools::recipe(Tool::Denoise, QStringLiteral("moyen"));
    REQUIRE(moyen.size() == 2);
    CHECK(moyen.at(1).effect == QStringLiteral("ladspa.9354877"));
    CHECK(moyen.at(1).params.value(QStringLiteral("2")) == QStringLiteral("0"));
    // Passe-haut de Butterworth écrit en largeur de bande : Q = 80 / 113 = 0,707.
    CHECK(moyen.at(0).params.value(QStringLiteral("av.t")) == QStringLiteral("h"));
    CHECK(moyen.at(0).params.value(QStringLiteral("av.w")) == QStringLiteral("113"));
}

TEST_CASE("Outils du son : poser, changer, retirer", "[KoraCutClipTools]")
{
    if (!EffectsRepository::get()->exists(QStringLiteral("avfilter.highpass")) || !EffectsRepository::get()->exists(QStringLiteral("avfilter.agate"))) {
        WARN("filtres avfilter absents : test sauté");
        return;
    }
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    const int videoTrack = timeline->getTrackIndexFromPosition(2);
    const QString binId = KdenliveTests::createProducerWithSound(pCore->getProjectProfile(), binModel, 100);
    QMap<int, QString> audioInfo;
    audioInfo.insert(1, QStringLiteral("stream1"));
    KdenliveTests::setAudioTargets(timeline, audioInfo);
    int avClip;
    REQUIRE(timeline->requestClipInsertion(binId, videoTrack, 0, avClip));
    const int audio = timeline->getClipSplitPartner(avClip);
    REQUIRE(audio > -1);
    // Le plan vidéo mène à sa partie son, et l'inverse.
    CHECK(KoraCutClipTools::audioTarget(timeline, avClip) == audio);
    CHECK(KoraCutClipTools::audioTarget(timeline, audio) == audio);
    auto stack = timeline->getClipEffectStackModel(audio);
    REQUIRE(stack->rowCount() == 0);

    SECTION("Réduire le bruit : une étape par geste, le niveau se remplace")
    {
        const int start = undoStack->index();
        auto r = KoraCutClipTools::apply(timeline, audio, Tool::Denoise, QStringLiteral("leger"));
        REQUIRE(r.ok);
        CHECK(r.effects == QStringList{QStringLiteral("avfilter.highpass"), QStringLiteral("avfilter.agate")});
        CHECK(undoStack->index() == start + 1);
        CHECK(KoraCutClipTools::current(timeline, audio, Tool::Denoise) == QStringLiteral("leger"));
        CHECK(stack->rowCount() == 2);

        r = KoraCutClipTools::apply(timeline, audio, Tool::Denoise, QStringLiteral("fort"));
        REQUIRE(r.ok);
        CHECK(undoStack->index() == start + 2);
        CHECK(KoraCutClipTools::current(timeline, audio, Tool::Denoise) == QStringLiteral("fort"));
        CHECK(countTag(tags(stack), QStringLiteral("bruit")) == r.effects.size());
        CHECK(r.effects.size() + r.missing.size() == 3);

        undoStack->undo();
        CHECK(KoraCutClipTools::current(timeline, audio, Tool::Denoise) == QStringLiteral("leger"));
        CHECK(stack->rowCount() == 2);
        undoStack->redo();
        CHECK(KoraCutClipTools::current(timeline, audio, Tool::Denoise) == QStringLiteral("fort"));

        r = KoraCutClipTools::apply(timeline, audio, Tool::Denoise, QString());
        REQUIRE(r.ok);
        CHECK(stack->rowCount() == 0);
        CHECK(KoraCutClipTools::current(timeline, audio, Tool::Denoise).isEmpty());
        // Retirer ce qui n'est pas posé ne laisse pas d'étape vide.
        const int before = undoStack->index();
        CHECK(KoraCutClipTools::apply(timeline, audio, Tool::Denoise, QString()).ok);
        CHECK(undoStack->index() == before);
    }

    SECTION("L'ordre de la chaîne : le bruit avant la voix, l'effet de voix à la fin")
    {
        REQUIRE(KoraCutClipTools::apply(timeline, audio, Tool::VoiceEffect, QStringLiteral("radio")).ok);
        REQUIRE(KoraCutClipTools::apply(timeline, audio, Tool::Enhance, QStringLiteral("standard")).ok);
        REQUIRE(KoraCutClipTools::apply(timeline, audio, Tool::Denoise, QStringLiteral("leger")).ok);
        const QStringList t = tags(stack);
        REQUIRE_FALSE(t.isEmpty());
        CHECK(t.first().startsWith(QStringLiteral("bruit:")));
        CHECK(t.last().startsWith(QStringLiteral("effet_voix:")));
        const int lastNoise = t.lastIndexOf(QStringLiteral("bruit:leger"));
        const int firstVoice = t.indexOf(QStringLiteral("voix:standard"));
        CHECK(lastNoise < firstVoice);
        // Un autre effet de voix remplace le premier.
        REQUIRE(KoraCutClipTools::apply(timeline, audio, Tool::VoiceEffect, QStringLiteral("telephone")).ok);
        CHECK(KoraCutClipTools::current(timeline, audio, Tool::VoiceEffect) == QStringLiteral("telephone"));
        CHECK(countTag(tags(stack), QStringLiteral("effet_voix")) == 5);
        // Les réglages posés sont connus du modèle de l'effet (la pile d'effets les montre).
        auto first = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(0));
        REQUIRE(first);
        CHECK(first->getParamFromName(QStringLiteral("av.f")).toString() == QStringLiteral("80"));
    }

    SECTION("Les outils d'agent passent par le plan vidéo")
    {
        QJsonObject r = KoraCutAgentTools::call(timeline, QStringLiteral("reduire_bruit"), {{QStringLiteral("clip_id"), avClip}, {QStringLiteral("niveau"), QStringLiteral("leger")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("clip_id")).toInt() == audio);
        r = KoraCutAgentTools::call(timeline, QStringLiteral("effet_voix"), {{QStringLiteral("clip_id"), audio}, {QStringLiteral("effet"), QStringLiteral("robot")}});
        CHECK(r.value(QStringLiteral("ok")).toBool() == EffectsRepository::get()->exists(QStringLiteral("avfilter.tremolo")));
        r = KoraCutAgentTools::call(timeline, QStringLiteral("effet_voix"), {{QStringLiteral("clip_id"), audio}, {QStringLiteral("effet"), QStringLiteral("chat")}});
        CHECK_FALSE(r.value(QStringLiteral("ok")).toBool());
        r = KoraCutAgentTools::call(timeline, QStringLiteral("ameliorer_voix"), {{QStringLiteral("clip_id"), audio}, {QStringLiteral("actif"), false}});
        CHECK(r.value(QStringLiteral("ok")).toBool());
        // Un plan de couleur n'a ni son ni image à stabiliser.
        const QString color = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 20, true);
        int colorClip;
        REQUIRE(timeline->requestClipInsertion(color, videoTrack, 200, colorClip));
        r = KoraCutAgentTools::call(timeline, QStringLiteral("stabiliser"), {{QStringLiteral("clip_id"), colorClip}});
        CHECK(r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("mauvais_type_de_piste"));
        r = KoraCutAgentTools::call(timeline, QStringLiteral("reduire_bruit"), {{QStringLiteral("clip_id"), colorClip}});
        CHECK(r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("mauvais_type_de_piste"));
    }
    pCore->projectManager()->closeCurrentDocument(false, false);
}

TEST_CASE("Stabiliser : le fichier d'un niveau relit l'analyse", "[KoraCutClipTools]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString analysis = dir.filePath(QStringLiteral("plan-analyse.mlt"));
    QFile f(analysis);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("<?xml version=\"1.0\"?><mlt><producer id=\"p\"><property name=\"resource\">plan.mp4</property>"
            "<filter id=\"f\"><property name=\"smoothing\">15</property><property name=\"optzoom\">1</property>"
            "<property name=\"mlt_service\">vidstab</property><property name=\"results\">plan.trf</property></filter></producer></mlt>");
    f.close();
    const QString level = dir.filePath(QStringLiteral("plan-fort.mlt"));
    REQUIRE(KoraCutStabilizer::writeLevelFile(analysis, level, QStringLiteral("fort")));
    QFile out(level);
    REQUIRE(out.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(out.readAll());
    CHECK(text.contains(QStringLiteral("<property name=\"smoothing\">30</property>")));
    CHECK(text.contains(QStringLiteral("<property name=\"results\">plan.trf</property>")));
    CHECK(text.contains(QStringLiteral("zoomspeed")));
    CHECK_FALSE(KoraCutStabilizer::writeLevelFile(analysis, level, QStringLiteral("extreme")));
    CHECK(KoraCutStabilizer::levels().size() == 3);
}
