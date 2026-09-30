/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "catch.hpp"
#include "test_utils.hpp"
// test specific headers
#include "jobs/cliploadrecovery.h"

#include <QTemporaryDir>
#include <QTemporaryFile>

using namespace ClipLoadRecovery;

TEST_CASE("Un clip dont le chargement s'arrête", "[ClipLoadRecovery]")
{
    // Les faits d'un rechargement interrompu sur un fichier qui est bien là.
    Facts interruptedButPresent;
    interruptedButPresent.canceled = true;
    interruptedButPresent.reloading = true;
    interruptedButPresent.sourceOnDisk = true;

    SECTION("Le fichier est là : on relance, on ne marque pas introuvable")
    {
        REQUIRE(decide(interruptedButPresent) == Decision::Retry);
    }

    SECTION("Le fichier a vraiment disparu : on marque introuvable")
    {
        Facts facts = interruptedButPresent;
        facts.sourceOnDisk = false;
        REQUIRE(decide(facts) == Decision::MarkMissing);
    }

    SECTION("Le média s'est refusé à s'ouvrir : on marque introuvable, même si le fichier est là")
    {
        Facts facts = interruptedButPresent;
        facts.loadFailed = true;
        REQUIRE(decide(facts) == Decision::MarkMissing);
    }

    SECTION("On ne relance pas indéfiniment")
    {
        Facts facts = interruptedButPresent;
        facts.retriesDone = facts.maxRetries;
        REQUIRE(decide(facts) == Decision::MarkMissing);
        facts.retriesDone = facts.maxRetries - 1;
        REQUIRE(decide(facts) == Decision::Retry);
    }

    SECTION("Une tâche plus récente décide : on ne touche à rien")
    {
        Facts pending = interruptedButPresent;
        pending.otherLoadJobPending = true;
        REQUIRE(decide(pending) == Decision::Ignore);

        Facts ready = interruptedButPresent;
        ready.clipAlreadyReady = true;
        REQUIRE(decide(ready) == Decision::Ignore);

        // Même quand le fichier a disparu : c'est l'autre tâche qui tranchera.
        ready.sourceOnDisk = false;
        REQUIRE(decide(ready) == Decision::Ignore);
    }

    SECTION("On ferme le projet, ou c'est une vignette : on ne juge pas le média")
    {
        Facts closing = interruptedButPresent;
        closing.managerBlocked = true;
        REQUIRE(decide(closing) == Decision::Ignore);

        Facts thumb = interruptedButPresent;
        thumb.thumbOnly = true;
        REQUIRE(decide(thumb) == Decision::Ignore);

        Facts removing = interruptedButPresent;
        removing.softDelete = true;
        REQUIRE(decide(removing) == Decision::Ignore);
    }

    SECTION("Un média illisible qu'on vient d'ajouter est retiré du chutier")
    {
        Facts justAdded;
        justAdded.loadFailed = true;
        justAdded.sourceOnDisk = true;
        REQUIRE(decide(justAdded) == Decision::Delete);
    }

    SECTION("Un premier chargement annulé ne relance pas, faute de producteur")
    {
        // Le clip n'a pas encore de producteur : reloading est faux, on garde l'ancien geste.
        Facts firstLoad;
        firstLoad.canceled = true;
        firstLoad.sourceOnDisk = true;
        REQUIRE(decide(firstLoad) == Decision::MarkMissing);
    }
}

TEST_CASE("Reconnaître une ressource présente", "[ClipLoadRecovery]")
{
    SECTION("Un chemin vide ou un tractor ne se vérifie pas")
    {
        REQUIRE_FALSE(sourceIsThere(QString()));
        REQUIRE_FALSE(sourceIsThere(QStringLiteral("<tractor>")));
    }

    SECTION("Un fichier qui existe est reconnu, un chemin absent non")
    {
        QTemporaryFile file;
        REQUIRE(file.open());
        file.write("koracut");
        file.flush();
        REQUIRE(sourceIsThere(file.fileName()));
        const QString path = file.fileName();
        file.close();
        file.remove();
        REQUIRE_FALSE(sourceIsThere(path));
    }

    SECTION("Un dossier n'est pas un média")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        REQUIRE_FALSE(sourceIsThere(dir.path()));
    }
}
