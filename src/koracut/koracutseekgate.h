/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Chantier 10 (document 6, journal du 4 octobre 2026) : le glisser de la tête de lecture.
// Une seule demande d'image en vol, la plus récente gagne. Tant que l'image demandée n'est pas affichée,
// les positions suivantes sont retenues (une seule : la dernière) ; à l'affichage, on part vers elle.
// Sans ça, chaque événement de souris purgeait le consommateur MLT et l'image en calcul était jetée.
// Cette classe ne fait que décider : elle ne touche ni à MLT ni à Qt, pour être essayée hors écran.

#pragma once

class KoraCutSeekGate
{
public:
    /** Ce que l'appelant doit faire maintenant. */
    struct Step
    {
        enum Kind {
            None,   // rien : on attend, ou tout est à jour
            Seek,   // poser la position et demander l'image
            Refresh // la position est déjà posée (flush), il reste à demander l'image
        };
        Kind kind{None};
        int position{-1};
        bool noAudioScrub{false};
    };

    /** Une demande de position. Vrai : rien en vol, la poser tout de suite (puis appeler sent()).
        Faux : une image est attendue, la demande est retenue et remplace la précédente. */
    bool request(int position, bool noAudioScrub)
    {
        if (!m_awaiting) {
            return true;
        }
        if (position == m_target && !m_owed) {
            // Retour sur la position déjà en vol : plus rien à retenir.
            m_pending = -1;
        } else {
            m_pending = position;
            m_pendingNoScrub = noAudioScrub;
        }
        ++m_held;
        return false;
    }

    /** La position a été posée et l'image demandée. @p position : celle du producteur (elle peut avoir été bornée). */
    void sent(int position)
    {
        m_target = position;
        m_awaiting = true;
        m_owed = false;
    }

    /** Une image vient d'être affichée. Une image d'une autre position que celle attendue ne compte pas. */
    Step frameShown(int framePosition)
    {
        if (!m_awaiting || framePosition != m_target) {
            return {};
        }
        return next();
    }

    /** Le délai de garde est passé sans l'image attendue : on n'attend pas plus. */
    Step guardExpired()
    {
        if (!m_awaiting) {
            return {};
        }
        return next();
    }

    /** Avant une opération qui lit la position du producteur (édition, lecture) : rend la position retenue,
        que l'appelant pose sans demander d'image, ou -1. L'image reste due : elle sera demandée à l'étape suivante. */
    int flush()
    {
        if (m_pending < 0) {
            return -1;
        }
        const int position = m_pending;
        m_pending = -1;
        m_target = position;
        m_owed = true;
        return position;
    }

    /** Une image vient d'être demandée par ailleurs (rafraîchissement) : plus rien n'est dû. */
    void refreshSent() { m_owed = false; }

    /** La lecture démarre, le producteur change : on oublie tout. */
    void reset()
    {
        m_awaiting = false;
        m_owed = false;
        m_pending = -1;
        m_held = 0;
    }

    bool awaiting() const { return m_awaiting; }
    bool owed() const { return m_owed; }
    int pending() const { return m_pending; }
    int target() const { return m_target; }
    /** Demandes retenues depuis le dernier moment de calme : plus de zéro, c'est une rafale (un glisser). */
    int held() const { return m_held; }

private:
    Step next()
    {
        if (m_pending >= 0) {
            Step step{Step::Seek, m_pending, m_pendingNoScrub};
            m_pending = -1;
            m_owed = false;
            return step;
        }
        if (m_owed) {
            m_owed = false;
            return {Step::Refresh, m_target, true};
        }
        m_awaiting = false;
        m_held = 0;
        return {};
    }

    bool m_awaiting{false};
    bool m_owed{false};
    bool m_pendingNoScrub{false};
    int m_target{-1};
    int m_pending{-1};
    int m_held{0};
};
