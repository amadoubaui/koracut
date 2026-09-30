# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: Jean-Baptiste Mardelle <jb@kdenlive.org>
# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK

# KoraCut : une disposition par module. Photo et Illustration n'en ont pas encore :
# leur onglet montre la page « module en construction ».
INSTALL (FILES
    montage.json
    montage_vertical.json
    montage_compact.json
    montage_vertical_compact.json
    etalonnage.json
    etalonnage_vertical.json
    audio.json
    audio_vertical.json
    vfx.json
    vfx_vertical.json
    export.json
    export_vertical.json
DESTINATION ${KDE_INSTALL_DATADIR}${DATA_INSTALL_PREFIX}/layouts)
