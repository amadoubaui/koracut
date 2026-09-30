# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK

# Chantier 5 : la voix off de synthèse (KoraCutVoiceOver).
INSTALL(FILES
    koracut_voix.py
    koracut_voix_torch.py
    koracut_voix_ecoute.py

DESTINATION ${KDE_INSTALL_DATADIR}${DATA_INSTALL_PREFIX}/scripts/voix)
