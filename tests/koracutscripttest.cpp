/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Document 21 (24 septembre 2026) : l'éditeur de script. Le modèle (blocs, tableau, colonnes),
// l'enregistrement dans le projet et l'annulation, les modèles par type de vidéo, l'export
// Markdown et CSV, et les trois outils d'agent script_lire, script_ecrire, script_modele.
// 1er octobre 2026 : un script par séquence (stockage, annulation, fichier, projet d'avant, copie).

#include "catch.hpp"
#include "test_utils.hpp"

#include "bin/clipcreator.hpp"
#include "bin/model/markerlistmodel.hpp"
#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutagenttools.h"
#include "koracut/script/koracutscript.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUndoGroup>

using KoraCutAgentTools::call;

namespace {
std::string dump(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)).toStdString();
}

QString errorCode(const QJsonObject &r)
{
    return r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString();
}

// L'uuid de la séquence dont le clip du chutier est binId.
QUuid sequenceOf(const QString &binId)
{
    const QMap<QUuid, QString> all = pCore->projectItemModel()->getAllSequenceClips();
    for (auto it = all.cbegin(); it != all.cend(); ++it) {
        if (it.value() == binId) {
            return it.key();
        }
    }
    return QUuid();
}

// Un script d'une ligne, reconnaissable à son titre.
KoraCutScript titled(const QString &title)
{
    KoraCutScript s;
    KoraCutScriptBlock b = KoraCutScript::newBlock(KoraCutScriptBlock::Kind::Heading);
    b.id = QStringLiteral("titre");
    b.text = title;
    s.insertBlock(b);
    return s;
}

QString titleOf(KdenliveDoc *doc, const QUuid &sequence)
{
    const KoraCutScript s = KoraCutScriptStore::current(doc, nullptr, sequence);
    const KoraCutScriptBlock *b = s.block(QStringLiteral("titre"));
    return b ? b->text : QString();
}
} // namespace

TEST_CASE("Script : le modèle en blocs", "[KoraCutScript]")
{
    KoraCutScript s;
    KoraCutScriptBlock title = KoraCutScript::newBlock(KoraCutScriptBlock::Kind::Heading);
    title.text = QStringLiteral("Mon film");
    title.level = 1;
    const QString titleId = s.insertBlock(title);
    KoraCutScriptBlock para = KoraCutScript::newBlock(KoraCutScriptBlock::Kind::Paragraph);
    para.text = QStringLiteral("Une ligne | avec une barre");
    s.insertBlock(para);
    KoraCutScriptBlock list = KoraCutScript::newBlock(KoraCutScriptBlock::Kind::List);
    list.items = {QStringLiteral("un"), QStringLiteral("deux")};
    s.insertBlock(list);
    const QString tableId = s.insertBlock(KoraCutScript::newScenesTable(QStringLiteral("Déroulé")));
    s.insertBlock(KoraCutScript::newBlock(KoraCutScriptBlock::Kind::Divider));
    REQUIRE(s.blocks.size() == 5);
    REQUIRE(s.tableCount() == 1);

    KoraCutScriptBlock *table = s.block(tableId);
    REQUIRE(table);
    CHECK(table->columns.size() == 7);
    CHECK(table->columnIndex(QStringLiteral("scene")) == 0);
    CHECK(table->columnIndex(QStringLiteral("fait")) == 6);

    SECTION("Lignes, colonnes, cases")
    {
        const QString r1 = table->addRow();
        const QString r2 = table->addRow();
        table->rows[0].cells.insert(QStringLiteral("scene"), QStringLiteral("Ouverture"));
        table->rows[0].cells.insert(QStringLiteral("fait"), KoraCutScriptBlock::normalizedValue(QStringLiteral("case"), QStringLiteral("oui")));
        CHECK(table->rows[0].cells.value(QStringLiteral("fait")) == QStringLiteral("1"));
        CHECK(KoraCutScriptBlock::normalizedValue(QStringLiteral("case"), QStringLiteral("non")).isEmpty());
        table->moveRow(0, 1);
        CHECK(table->rows.at(1).id == r1);
        CHECK(table->rows.at(0).id == r2);
        const QString copy = table->duplicateRow(1);
        CHECK(table->rows.size() == 3);
        CHECK(table->rows.at(2).id == copy);
        CHECK(table->rows.at(2).cells.value(QStringLiteral("scene")) == QStringLiteral("Ouverture"));
        const QString col = table->addColumn(QStringLiteral("Statut"), QStringLiteral("choix"), {QStringLiteral("A"), QStringLiteral("B")});
        CHECK(table->columnIndex(QStringLiteral("statut")) == 7);
        table->rows[0].cells.insert(col, QStringLiteral("A"));
        table->removeColumn(7);
        CHECK(table->columns.size() == 7);
        CHECK(!table->rows.at(0).cells.contains(col));
        CHECK(table->rowSummary(1) == QStringLiteral("Ouverture"));
    }

    SECTION("JSON aller et retour, avec le numéro de version du format")
    {
        table->addRow();
        table->rows[0].cells.insert(QStringLiteral("scene"), QStringLiteral("Plat fini"));
        table->rows[0].cells.insert(QStringLiteral("fait"), QStringLiteral("1"));
        table->rows[0].marker = 75;
        const QJsonObject json = s.toJson();
        CHECK(json.value(QStringLiteral("format")).toInt() == KoraCutScript::FormatVersion);
        bool ok = false;
        const KoraCutScript back = KoraCutScript::fromString(s.toCompactString(), &ok);
        REQUIRE(ok);
        CHECK(back.toCompactString() == s.toCompactString());
        CHECK(back.block(titleId)->text == QStringLiteral("Mon film"));
        CHECK(back.block(tableId)->rows.at(0).marker == 75);
        CHECK(back.block(tableId)->rows.at(0).cells.value(QStringLiteral("fait")) == QStringLiteral("1"));
    }

    SECTION("Blocs : déplacer, dupliquer, supprimer")
    {
        CHECK(s.moveBlock(titleId, 2));
        CHECK(s.indexOf(titleId) == 2);
        const QString copy = s.duplicateBlock(tableId);
        CHECK(!copy.isEmpty());
        CHECK(s.tableCount() == 2);
        CHECK(s.block(copy)->columns.first().id != table->columns.first().id);
        CHECK(s.removeBlock(copy));
        CHECK(s.tableCount() == 1);
    }

    SECTION("Export Markdown et CSV, import CSV")
    {
        table->addRow();
        table->rows[0].cells.insert(QStringLiteral("scene"), QStringLiteral("Accroche, forte"));
        table->rows[0].cells.insert(QStringLiteral("duree"), QStringLiteral("3 s"));
        table->rows[0].cells.insert(QStringLiteral("fait"), QStringLiteral("1"));
        const QString md = s.toMarkdown();
        CHECK(md.contains(QStringLiteral("# Mon film")));
        CHECK(md.contains(QStringLiteral("Une ligne | avec une barre")));
        CHECK(md.contains(QStringLiteral("- un\n- deux")));
        CHECK(md.contains(QStringLiteral("| Accroche, forte | 3 s |")));
        CHECK(md.contains(QStringLiteral("[x]")));
        CHECK(md.contains(QStringLiteral("\n---\n")));

        const QString csv = KoraCutScript::tableToCsv(*table);
        CHECK(csv.contains(QStringLiteral("\"Accroche, forte\",3 s")));
        const QList<QStringList> rows = KoraCutScript::parseCsv(csv);
        REQUIRE(rows.size() == 2);
        CHECK(rows.at(1).at(0) == QStringLiteral("Accroche, forte"));
        CHECK(rows.at(1).at(6) == QStringLiteral("x"));

        // Un CSV d'Excel en français : des points-virgules, des guillemets, une colonne neuve.
        const QString excel = QStringLiteral("\xFEFF") + table->columns.at(0).title + QStringLiteral(";Lieu;") + table->columns.at(6).title +
                              QStringLiteral("\r\nIntro;\"Cuisine; côté fenêtre\";oui\r\nFin;Salon;\r\n");
        QString error;
        CHECK(KoraCutScript::importCsv(*table, excel, &error) == 2);
        CHECK(table->columns.size() == 8);
        CHECK(table->rows.size() == 3);
        CHECK(table->rows.at(1).cells.value(table->columns.at(7).id) == QStringLiteral("Cuisine; côté fenêtre"));
        CHECK(table->rows.at(1).cells.value(QStringLiteral("fait")) == QStringLiteral("1"));
        CHECK(table->rows.at(2).cells.value(QStringLiteral("fait")).isEmpty());
    }
}

TEST_CASE("Script : les modèles par type de vidéo", "[KoraCutScript]")
{
    const KoraCutScript recipe = KoraCutScript::templateFor(QStringLiteral("F04-13"));
    REQUIRE(KoraCutScript::hasTemplate(QStringLiteral("F04-13")));
    CHECK(recipe.typeId == QStringLiteral("F04-13"));
    CHECK(recipe.blocks.first().text == QStringLiteral("Recette de cuisine"));
    const KoraCutScriptBlock *table = recipe.block(QStringLiteral("deroule"));
    REQUIRE(table);
    CHECK(table->columns.size() == 7);
    REQUIRE(table->rows.size() == 5);
    CHECK(table->rows.at(0).id == QStringLiteral("s1"));
    CHECK(table->rows.at(0).cells.value(QStringLiteral("scene")) == QStringLiteral("Plat fini"));
    CHECK(table->rows.at(0).cells.value(QStringLiteral("duree")) == QStringLiteral("3 s"));
    CHECK(recipe.block(QStringLiteral("accroche")) != nullptr);
    CHECK(recipe.block(QStringLiteral("fin")) != nullptr);

    const KoraCutScript ad = KoraCutScript::templateFor(QStringLiteral("F07-02"));
    const KoraCutScriptBlock *adTable = ad.block(QStringLiteral("deroule"));
    REQUIRE(adTable);
    CHECK(adTable->rows.at(0).cells.value(QStringLiteral("scene")) == QStringLiteral("Accroche"));
    CHECK(ad.toCompactString() != recipe.toCompactString());

    // Le modèle recalculé est identique : un agent peut viser « deroule » et « s2 » avant tout enregistrement.
    CHECK(KoraCutScript::templateFor(QStringLiteral("F04-13")).toCompactString() == recipe.toCompactString());

    // Sans type (ou type inconnu) : le modèle générique en quatre temps.
    const KoraCutScript generic = KoraCutScript::templateFor(QString());
    CHECK(generic.typeId.isEmpty());
    REQUIRE(generic.block(QStringLiteral("deroule")));
    CHECK(generic.block(QStringLiteral("deroule"))->rows.size() == 4);
    CHECK(KoraCutScript::templateFor(QStringLiteral("F99-99")).block(QStringLiteral("deroule"))->rows.size() == 4);
}

TEST_CASE("Script : dans le projet, l'annulation et les outils d'agent", "[KoraCutScript]")
{
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);
    document.setDocumentProperty(QStringLiteral("koracut:videotype"), QStringLiteral("F04-13"));

    SECTION("Rien n'est écrit tant qu'on regarde ; un changement = une étape")
    {
        bool fromTemplate = false;
        KoraCutScript s = KoraCutScriptStore::current(&document, &fromTemplate);
        CHECK(fromTemplate);
        CHECK(!KoraCutScriptStore::isStored(&document));
        CHECK(s.block(QStringLiteral("deroule"))->rows.size() == 5);
        const int before = undoStack->index();

        s.block(QStringLiteral("titre"))->text = QStringLiteral("Le yassa");
        REQUIRE(KoraCutScriptStore::apply(s, QStringLiteral("titre")));
        CHECK(undoStack->index() == before + 1);
        CHECK(KoraCutScriptStore::isStored(&document));
        // Enregistré dans le projet, sur la séquence : la propriété part avec celles de la séquence au
        // moment d'enregistrer. Plus rien au niveau du document.
        CHECK(!document.documentProperties().contains(KoraCutScript::PropertyName));
        CHECK(document.hasSequenceProperty(document.uuid(), KoraCutScript::PropertyName));
        bool ok = false;
        const KoraCutScript saved = KoraCutScript::fromString(document.getSequenceProperty(document.uuid(), KoraCutScript::PropertyName), &ok);
        REQUIRE(ok);
        CHECK(saved.block(QStringLiteral("titre"))->text == QStringLiteral("Le yassa"));

        undoStack->undo();
        CHECK(!KoraCutScriptStore::isStored(&document));
        CHECK(KoraCutScriptStore::current(&document).block(QStringLiteral("titre"))->text == QStringLiteral("Recette de cuisine"));
        undoStack->redo();
        CHECK(KoraCutScriptStore::current(&document).block(QStringLiteral("titre"))->text == QStringLiteral("Le yassa"));
    }

    SECTION("Une frappe continue dans la même cellule ne fait qu'une étape")
    {
        KoraCutScript s = KoraCutScriptStore::current(&document);
        const int before = undoStack->index();
        for (const QString &text : {QStringLiteral("L"), QStringLiteral("Le"), QStringLiteral("Le y")}) {
            s.block(QStringLiteral("titre"))->text = text;
            KoraCutScriptStore::apply(s, QStringLiteral("frappe"), QStringLiteral("texte:titre"));
        }
        CHECK(undoStack->index() == before + 1);
        s.block(QStringLiteral("titre"))->text = QStringLiteral("Autre");
        KoraCutScriptStore::apply(s, QStringLiteral("autre"), QStringLiteral("texte:autre"));
        CHECK(undoStack->index() == before + 2);
        undoStack->undo();
        CHECK(KoraCutScriptStore::current(&document).block(QStringLiteral("titre"))->text == QStringLiteral("Le y"));
        undoStack->undo();
        CHECK(!KoraCutScriptStore::isStored(&document));
    }

    SECTION("Les outils script_lire, script_ecrire, script_modele")
    {
        QJsonObject r = call(timeline, QStringLiteral("script_lire"), {});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(!r.value(QStringLiteral("enregistre")).toBool());
        CHECK(r.value(QStringLiteral("resume")).toObject().value(QStringLiteral("scenes")).toInt() == 5);

        const int before = undoStack->index();
        r = call(timeline, QStringLiteral("script_ecrire"),
                 {{QStringLiteral("operations"),
                   QJsonArray{QJsonObject{{QStringLiteral("faire"), QStringLiteral("modifier_cellule")},
                                          {QStringLiteral("bloc_id"), QStringLiteral("deroule")},
                                          {QStringLiteral("ligne_id"), QStringLiteral("s2")},
                                          {QStringLiteral("colonne"), QStringLiteral("voix")},
                                          {QStringLiteral("valeur"), QStringLiteral("Il te faut du poulet, des oignons, du citron.")}},
                              QJsonObject{{QStringLiteral("faire"), QStringLiteral("ajouter_ligne")},
                                          {QStringLiteral("bloc_id"), QStringLiteral("deroule")},
                                          {QStringLiteral("apres"), QStringLiteral("s2")},
                                          {QStringLiteral("cellules"), QJsonObject{{QStringLiteral("Scène"), QStringLiteral("Marinade")}, {QStringLiteral("fait"), true}}}},
                              QJsonObject{{QStringLiteral("faire"), QStringLiteral("ajouter_colonne")},
                                          {QStringLiteral("bloc_id"), QStringLiteral("deroule")},
                                          {QStringLiteral("titre"), QStringLiteral("Statut")},
                                          {QStringLiteral("type"), QStringLiteral("choix")},
                                          {QStringLiteral("choix"), QJsonArray{QStringLiteral("À tourner"), QStringLiteral("Tourné")}}},
                              QJsonObject{{QStringLiteral("faire"), QStringLiteral("ajouter_bloc")},
                                          {QStringLiteral("type"), QStringLiteral("paragraphe")},
                                          {QStringLiteral("texte"), QStringLiteral("Voix off chaleureuse.")},
                                          {QStringLiteral("apres"), QStringLiteral("titre")}}}}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(undoStack->index() == before + 1);
        KoraCutScript s = KoraCutScriptStore::current(&document);
        const KoraCutScriptBlock *table = s.block(QStringLiteral("deroule"));
        REQUIRE(table->rows.size() == 6);
        CHECK(table->rows.at(1).cells.value(QStringLiteral("voix")).startsWith(QStringLiteral("Il te faut")));
        CHECK(table->rows.at(2).cells.value(QStringLiteral("scene")) == QStringLiteral("Marinade"));
        CHECK(table->rows.at(2).cells.value(QStringLiteral("fait")) == QStringLiteral("1"));
        CHECK(table->columns.size() == 8);
        CHECK(s.blocks.at(1).text == QStringLiteral("Voix off chaleureuse."));

        // Une opération fausse : rien n'est gardé.
        r = call(timeline, QStringLiteral("script_ecrire"),
                 {{QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("faire"), QStringLiteral("supprimer_ligne")},
                                                                        {QStringLiteral("bloc_id"), QStringLiteral("deroule")},
                                                                        {QStringLiteral("ligne_id"), QStringLiteral("zz")}}}}});
        CHECK(errorCode(r) == QStringLiteral("id_inconnu"));
        CHECK(undoStack->index() == before + 1);

        // Le modèle ne remplace pas un script écrit, sauf demande.
        r = call(timeline, QStringLiteral("script_modele"), {});
        CHECK(errorCode(r) == QStringLiteral("existe_deja"));
        r = call(timeline, QStringLiteral("script_modele"), {{QStringLiteral("type_video"), QStringLiteral("F07-02")}, {QStringLiteral("remplacer"), true}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(KoraCutScriptStore::current(&document).block(QStringLiteral("deroule"))->rows.at(0).cells.value(QStringLiteral("scene")) == QStringLiteral("Accroche"));
        undoStack->undo();
        CHECK(KoraCutScriptStore::current(&document).block(QStringLiteral("deroule"))->rows.size() == 6);

        // Relier une scène et poser son marqueur : une seule étape pour les deux.
        const int beforeMarker = undoStack->index();
        r = call(timeline, QStringLiteral("script_ecrire"),
                 {{QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("faire"), QStringLiteral("relier_ligne")},
                                                                        {QStringLiteral("bloc_id"), QStringLiteral("deroule")},
                                                                        {QStringLiteral("ligne_id"), QStringLiteral("s1")},
                                                                        {QStringLiteral("marqueur"), 50},
                                                                        {QStringLiteral("poser_marqueur"), true}}}}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(undoStack->index() == beforeMarker + 1);
        CHECK(timeline->getGuideModel()->hasMarker(50));
        CHECK(KoraCutScriptStore::current(&document).block(QStringLiteral("deroule"))->rows.at(0).marker == 50);
        undoStack->undo();
        CHECK(!timeline->getGuideModel()->hasMarker(50));
        CHECK(KoraCutScriptStore::current(&document).block(QStringLiteral("deroule"))->rows.at(0).marker == -1);

        r = call(timeline, QStringLiteral("script_lire"), {{QStringLiteral("format"), QStringLiteral("markdown")}});
        CHECK(r.value(QStringLiteral("markdown")).toString().contains(QStringLiteral("Marinade")));
    }
    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}

// 1er octobre 2026 (demande d'Amadou Ba) : chaque séquence du projet a son script. Quatre publicités
// dans un projet, une par séquence, chacune avec le sien.
TEST_CASE("Script : un script par séquence", "[KoraCutScript]")
{
    auto binModel = pCore->projectItemModel();
    binModel->clean();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    pCore->setCurrentProfile(QStringLiteral("dv_pal"));
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);
    KdenliveTests::resetNextId();
    document.setDocumentProperty(QStringLiteral("koracut:videotype"), QStringLiteral("F07-02"));
    const QUuid first = document.uuid();

    const QString seq2 = ClipCreator::createPlaylistClip(QStringLiteral("Pub 2"), {2, 2}, QStringLiteral("-1"), binModel);
    const QUuid second = sequenceOf(seq2);
    REQUIRE(!second.isNull());
    auto timeline2 = document.getTimeline(second);
    REQUIRE(timeline2);
    pCore->projectManager()->testSetActiveTimeline(timeline);

    // L'ordre des séquences est celui de leur création ; rang, nom ou uuid les désignent.
    REQUIRE(KoraCutScriptStore::sequences(&document) == QList<QUuid>{first, second});
    CHECK(KoraCutScriptStore::activeSequence(&document) == first);
    CHECK(KoraCutScriptStore::findSequence(QString(), &document) == first);
    CHECK(KoraCutScriptStore::findSequence(QStringLiteral("2"), &document) == second);
    CHECK(KoraCutScriptStore::findSequence(QStringLiteral("pub 2"), &document) == second);
    CHECK(KoraCutScriptStore::findSequence(second.toString(QUuid::WithoutBraces), &document) == second);
    CHECK(KoraCutScriptStore::findSequence(QStringLiteral("Pub 9"), &document).isNull());
    CHECK(KoraCutScriptStore::sequenceName(second) == QStringLiteral("Pub 2"));

    SECTION("Deux séquences, deux scripts, et l'annulation revient sur la bonne séquence")
    {
        const int before = undoStack->index();
        REQUIRE(KoraCutScriptStore::apply(titled(QStringLiteral("Pub 1")), QStringLiteral("titre")));
        CHECK(KoraCutScriptStore::isStored(&document, first));
        // L'autre séquence n'a rien reçu : elle montre encore le modèle.
        CHECK(!KoraCutScriptStore::isStored(&document, second));
        bool fromTemplate = false;
        CHECK(KoraCutScriptStore::current(&document, &fromTemplate, second).block(QStringLiteral("deroule")) != nullptr);
        CHECK(fromTemplate);

        // On passe sur la deuxième séquence : le script par défaut est maintenant le sien.
        pCore->projectManager()->testSetActiveTimeline(timeline2);
        CHECK(!KoraCutScriptStore::isStored(&document));
        REQUIRE(KoraCutScriptStore::apply(titled(QStringLiteral("Pub 2")), QStringLiteral("titre")));
        CHECK(undoStack->index() == before + 2);
        CHECK(titleOf(&document, first) == QStringLiteral("Pub 1"));
        CHECK(titleOf(&document, second) == QStringLiteral("Pub 2"));
        CHECK(titleOf(&document, QUuid()) == QStringLiteral("Pub 2"));

        // Une écriture visant la première pendant que la deuxième est active.
        REQUIRE(KoraCutScriptStore::apply(titled(QStringLiteral("Pub 1, version 2")), QStringLiteral("titre"), QString(), Fun(), Fun(), first));
        CHECK(titleOf(&document, first) == QStringLiteral("Pub 1, version 2"));
        CHECK(titleOf(&document, second) == QStringLiteral("Pub 2"));

        // Annuler pendant que la deuxième est active : c'est le script de la première qui revient.
        undoStack->undo();
        CHECK(titleOf(&document, first) == QStringLiteral("Pub 1"));
        CHECK(titleOf(&document, second) == QStringLiteral("Pub 2"));
        // Puis l'étape d'avant, celle de la deuxième ; on est repassé sur la première entre-temps.
        pCore->projectManager()->testSetActiveTimeline(timeline);
        undoStack->undo();
        CHECK(!KoraCutScriptStore::isStored(&document, second));
        CHECK(titleOf(&document, first) == QStringLiteral("Pub 1"));
        undoStack->redo();
        CHECK(titleOf(&document, second) == QStringLiteral("Pub 2"));
        CHECK(titleOf(&document, first) == QStringLiteral("Pub 1"));

        // Deux frappes de suite dans deux séquences ne fusionnent pas, même avec la même clé.
        const int beforeTyping = undoStack->index();
        KoraCutScriptStore::apply(titled(QStringLiteral("a")), QStringLiteral("frappe"), QStringLiteral("texte:titre"), Fun(), Fun(), first);
        KoraCutScriptStore::apply(titled(QStringLiteral("b")), QStringLiteral("frappe"), QStringLiteral("texte:titre"), Fun(), Fun(), second);
        CHECK(undoStack->index() == beforeTyping + 2);
    }

    SECTION("Les outils : le paramètre sequence, les marqueurs dans la séquence du script")
    {
        // Sans sequence : la séquence de l'appel (l'active).
        QJsonObject r = call(timeline, QStringLiteral("script_ecrire"), {{QStringLiteral("script"), titled(QStringLiteral("Pub 1")).toJson()}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("sequence")).toObject().value(QStringLiteral("rang")).toInt() == 1);
        CHECK(r.value(QStringLiteral("sequence")).toObject().value(QStringLiteral("active")).toBool());

        // Par le rang : le modèle dans la deuxième, puis une scène reliée à un marqueur.
        r = call(timeline, QStringLiteral("script_modele"), {{QStringLiteral("sequence"), 2}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("sequence")).toObject().value(QStringLiteral("nom")).toString() == QStringLiteral("Pub 2"));
        CHECK(errorCode(call(timeline, QStringLiteral("script_modele"), {{QStringLiteral("sequence"), QStringLiteral("Pub 2")}})) == QStringLiteral("existe_deja"));
        const int beforeMarker = undoStack->index();
        r = call(timeline, QStringLiteral("script_ecrire"),
                 {{QStringLiteral("sequence"), QStringLiteral("Pub 2")},
                  {QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("faire"), QStringLiteral("relier_ligne")},
                                                                        {QStringLiteral("bloc_id"), QStringLiteral("deroule")},
                                                                        {QStringLiteral("ligne_id"), QStringLiteral("s1")},
                                                                        {QStringLiteral("marqueur"), 30},
                                                                        {QStringLiteral("poser_marqueur"), true}}}}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(undoStack->index() == beforeMarker + 1);
        // Le marqueur est dans la séquence du script, pas dans la séquence active.
        CHECK(timeline2->getGuideModel()->hasMarker(30));
        CHECK(!timeline->getGuideModel()->hasMarker(30));
        CHECK(KoraCutScriptStore::current(&document, nullptr, second).block(QStringLiteral("deroule"))->rows.at(0).marker == 30);
        CHECK(titleOf(&document, first) == QStringLiteral("Pub 1"));
        undoStack->undo();
        CHECK(!timeline2->getGuideModel()->hasMarker(30));
        CHECK(KoraCutScriptStore::current(&document, nullptr, second).block(QStringLiteral("deroule"))->rows.at(0).marker == -1);

        // Lire par l'uuid ; la réponse dit quelle séquence et lesquelles ont un script.
        r = call(timeline, QStringLiteral("script_lire"), {{QStringLiteral("sequence"), second.toString(QUuid::WithoutBraces)}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("enregistre")).toBool());
        CHECK(r.value(QStringLiteral("sequence")).toObject().value(QStringLiteral("rang")).toInt() == 2);
        CHECK(!r.value(QStringLiteral("sequence")).toObject().value(QStringLiteral("active")).toBool());
        CHECK(r.value(QStringLiteral("sequences")).toArray().size() == 2);
        CHECK(r.value(QStringLiteral("script")).toObject().value(QStringLiteral("blocs")).toArray().size() > 1);
        r = call(timeline, QStringLiteral("script_lire"), {});
        CHECK(r.value(QStringLiteral("script")).toObject().value(QStringLiteral("blocs")).toArray().size() == 1);
        CHECK(errorCode(call(timeline, QStringLiteral("script_lire"), {{QStringLiteral("sequence"), QStringLiteral("Pub 9")}})) == QStringLiteral("id_inconnu"));
        CHECK(errorCode(call(timeline, QStringLiteral("script_ecrire"), {{QStringLiteral("sequence"), 7}, {QStringLiteral("script"), titled(QStringLiteral("x")).toJson()}})) ==
              QStringLiteral("id_inconnu"));
    }

    SECTION("Enregistrer puis rouvrir : chaque séquence retrouve son script")
    {
        REQUIRE(KoraCutScriptStore::apply(titled(QStringLiteral("Pub 1")), QStringLiteral("titre")));
        REQUIRE(KoraCutScriptStore::apply(titled(QStringLiteral("Pub 2")), QStringLiteral("titre"), QString(), Fun(), Fun(), second));
        // Un script écrit puis annulé ne doit pas revenir à la réouverture.
        const QString seq3 = ClipCreator::createPlaylistClip(QStringLiteral("Pub 3"), {2, 2}, QStringLiteral("-1"), binModel);
        const QUuid third = sequenceOf(seq3);
        REQUIRE(!third.isNull());
        pCore->projectManager()->testSetActiveTimeline(timeline);
        REQUIRE(KoraCutScriptStore::apply(titled(QStringLiteral("Pub 3")), QStringLiteral("titre"), QString(), Fun(), Fun(), third));
        undoStack->undo();
        CHECK(!KoraCutScriptStore::isStored(&document, third));

        const QString saveFile = QDir::temp().absoluteFilePath(QStringLiteral("koracut-script-sequences.kdenlive"));
        REQUIRE(pCore->projectManager()->testSaveFileAs(saveFile));
        // Dans le fichier : une propriété par séquence, rien au niveau du document.
        QFile saved(saveFile);
        REQUIRE(saved.open(QIODevice::ReadOnly));
        const QString xml = QString::fromUtf8(saved.readAll());
        saved.close();
        CHECK(xml.count(QStringLiteral("kdenlive:sequenceproperties.koracut:script")) == 2);
        CHECK(!xml.contains(QStringLiteral("kdenlive:docproperties.koracut:script")));

        timeline.reset();
        timeline2.reset();
        pCore->projectManager()->closeCurrentDocument(false, false);

        QUndoGroup *undoGroup = new QUndoGroup(&document);
        undoGroup->addStack(undoStack.get());
        DocOpenResult openResults = KdenliveDoc::Open(QUrl::fromLocalFile(saveFile), QDir::temp().path(), undoGroup, false, nullptr);
        REQUIRE(openResults.isSuccessful() == true);
        std::unique_ptr<KdenliveDoc> openedDoc = openResults.getDocument();
        pCore->projectManager()->testSetDocument(openedDoc.get());
        documentDate = QFileInfo(saveFile).lastModified();
        KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
        const QMap<QUuid, QString> allSequences = binModel->getAllSequenceClips();
        REQUIRE(allSequences.contains(first));
        REQUIRE(allSequences.contains(second));
        pCore->projectManager()->openTimeline(allSequences.value(first), -1, first);
        timeline = pCore->currentDoc()->getTimeline(first);
        pCore->projectManager()->testSetActiveTimeline(timeline);

        CHECK(titleOf(openedDoc.get(), first) == QStringLiteral("Pub 1"));
        CHECK(titleOf(openedDoc.get(), second) == QStringLiteral("Pub 2"));
        CHECK(!KoraCutScriptStore::isStored(openedDoc.get(), third));
        CHECK(!openedDoc->hasDocumentProperty(KoraCutScript::PropertyName));
        timeline.reset();
        pCore->projectManager()->closeCurrentDocument(false, false);
        QFile::remove(saveFile);
        return;
    }

    SECTION("Un projet d'avant : le script du document va à la première séquence, rien ne se perd")
    {
        // Comme un fichier d'avant le 1er octobre 2026 : un seul script, dans la propriété de document.
        const QString legacy = titled(QStringLiteral("Le script d'avant")).toCompactString();
        document.setDocumentProperty(KoraCutScript::PropertyName, legacy);
        const QString saveFile = QDir::temp().absoluteFilePath(QStringLiteral("koracut-script-ancien.kdenlive"));
        REQUIRE(pCore->projectManager()->testSaveFileAs(saveFile));
        timeline.reset();
        timeline2.reset();
        pCore->projectManager()->closeCurrentDocument(false, false);

        QUndoGroup *undoGroup = new QUndoGroup(&document);
        undoGroup->addStack(undoStack.get());
        DocOpenResult openResults = KdenliveDoc::Open(QUrl::fromLocalFile(saveFile), QDir::temp().path(), undoGroup, false, nullptr);
        REQUIRE(openResults.isSuccessful() == true);
        std::unique_ptr<KdenliveDoc> openedDoc = openResults.getDocument();
        pCore->projectManager()->testSetDocument(openedDoc.get());
        // Le fichier porte bien l'ancien script, au niveau du document.
        REQUIRE(openedDoc->getDocumentProperty(KoraCutScript::PropertyName) == legacy);
        documentDate = QFileInfo(saveFile).lastModified();
        KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
        const QMap<QUuid, QString> allSequences = binModel->getAllSequenceClips();
        pCore->projectManager()->openTimeline(allSequences.value(first), -1, first);
        timeline = pCore->currentDoc()->getTimeline(first);
        pCore->projectManager()->testSetActiveTimeline(timeline);

        const int before = undoStack->index();
        CHECK(titleOf(openedDoc.get(), first) == QStringLiteral("Le script d'avant"));
        CHECK(KoraCutScriptStore::isStored(openedDoc.get(), first));
        CHECK(!KoraCutScriptStore::isStored(openedDoc.get(), second));
        // Reprendre l'ancien script n'est pas une étape d'annulation, et il ne reste qu'à un endroit.
        CHECK(undoStack->index() == before);
        CHECK(!openedDoc->hasDocumentProperty(KoraCutScript::PropertyName));

        bool fromTemplate = false;
        CHECK(KoraCutScriptStore::current(openedDoc.get(), &fromTemplate, second).block(QStringLiteral("deroule")) != nullptr);
        CHECK(fromTemplate);

        // Il repart dans le fichier sur sa séquence.
        REQUIRE(pCore->projectManager()->testSaveFileAs(saveFile));
        QFile saved(saveFile);
        REQUIRE(saved.open(QIODevice::ReadOnly));
        const QString xml = QString::fromUtf8(saved.readAll());
        saved.close();
        CHECK(xml.count(QStringLiteral("kdenlive:sequenceproperties.koracut:script")) == 1);
        CHECK(!xml.contains(QStringLiteral("kdenlive:docproperties.koracut:script")));

        timeline.reset();
        timeline2.reset();
        pCore->projectManager()->closeCurrentDocument(false, false);
        QFile::remove(saveFile);
        return;
    }

    SECTION("L'ancien script va à la première séquence même quand une autre est active")
    {
        document.setDocumentProperty(KoraCutScript::PropertyName, titled(QStringLiteral("Le script d'avant")).toCompactString());
        pCore->projectManager()->testSetActiveTimeline(timeline2);
        bool fromTemplate = false;
        CHECK(KoraCutScriptStore::current(&document, &fromTemplate).block(QStringLiteral("deroule")) != nullptr);
        CHECK(fromTemplate);
        CHECK(titleOf(&document, first) == QStringLiteral("Le script d'avant"));
        CHECK(!KoraCutScriptStore::isStored(&document, second));
        pCore->projectManager()->testSetActiveTimeline(timeline);
    }

    SECTION("Un script déjà sur la première séquence : l'ancien script du document va à la suivante")
    {
        REQUIRE(KoraCutScriptStore::apply(titled(QStringLiteral("Pub 1")), QStringLiteral("titre")));
        document.setDocumentProperty(KoraCutScript::PropertyName, titled(QStringLiteral("Le script d'avant")).toCompactString());
        CHECK(titleOf(&document, first) == QStringLiteral("Pub 1"));
        CHECK(titleOf(&document, second) == QStringLiteral("Le script d'avant"));
        CHECK(!document.hasDocumentProperty(KoraCutScript::PropertyName));
        // Toutes les séquences ont le leur : un script de document reste où il est, sans rien écraser.
        document.setDocumentProperty(KoraCutScript::PropertyName, titled(QStringLiteral("Encore un")).toCompactString());
        CHECK(titleOf(&document, first) == QStringLiteral("Pub 1"));
        CHECK(titleOf(&document, second) == QStringLiteral("Le script d'avant"));
        CHECK(document.hasDocumentProperty(KoraCutScript::PropertyName));
    }

    SECTION("Une séquence dupliquée emporte son script")
    {
        REQUIRE(KoraCutScriptStore::apply(titled(QStringLiteral("Pub 2")), QStringLiteral("titre"), QString(), Fun(), Fun(), second));
        QJsonObject r = call(timeline, QStringLiteral("dupliquer_sequence"),
                             {{QStringLiteral("sequence"), QStringLiteral("Pub 2")}, {QStringLiteral("nom"), QStringLiteral("Pub 2 - v2")}, {QStringLiteral("activer"), false}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const QUuid copy = KoraCutScriptStore::findSequence(QStringLiteral("Pub 2 - v2"), &document);
        REQUIRE(!copy.isNull());
        REQUIRE(copy != second);
        CHECK(titleOf(&document, copy) == QStringLiteral("Pub 2"));
        // Deux scripts distincts à partir de là : écrire dans la source ne change pas la copie.
        REQUIRE(KoraCutScriptStore::apply(titled(QStringLiteral("Pub 2, deuxième version")), QStringLiteral("titre"), QString(), Fun(), Fun(), second));
        CHECK(titleOf(&document, second) == QStringLiteral("Pub 2, deuxième version"));
        CHECK(titleOf(&document, copy) == QStringLiteral("Pub 2"));
    }
    timeline.reset();
    timeline2.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}
