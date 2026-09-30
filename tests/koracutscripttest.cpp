/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Document 21 (24 septembre 2026) : l'éditeur de script. Le modèle (blocs, tableau, colonnes),
// l'enregistrement dans le projet et l'annulation, les modèles par type de vidéo, l'export
// Markdown et CSV, et les trois outils d'agent script_lire, script_ecrire, script_modele.

#include "catch.hpp"
#include "test_utils.hpp"

#include "bin/model/markerlistmodel.hpp"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutagenttools.h"
#include "koracut/script/koracutscript.h"

#include <QJsonArray>
#include <QJsonDocument>

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
        // Enregistré dans le projet : la propriété part avec les autres au moment d'enregistrer.
        CHECK(document.documentProperties().contains(KoraCutScript::PropertyName));
        bool ok = false;
        const KoraCutScript saved = KoraCutScript::fromString(document.documentProperties().value(KoraCutScript::PropertyName), &ok);
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
