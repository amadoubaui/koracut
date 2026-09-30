/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Le module MLT de KoraCut (libmltkoracut) : le filtre d'étalonnage « koracut_grade » (document 22).

#include <framework/mlt.h>

#include <limits.h>
#include <string.h>

extern mlt_filter filter_koracut_grade_init(mlt_profile profile, mlt_service_type type, const char *id, char *arg);

static mlt_properties metadata(mlt_service_type type, const char *id, void *data)
{
    (void) type;
    (void) id;
    char file[PATH_MAX];
    snprintf(file, PATH_MAX, "%s/koracut/%s", mlt_environment("MLT_DATA"), (char *) data);
    return mlt_properties_parse_yaml(file);
}

MLT_REPOSITORY
{
    MLT_REGISTER(mlt_service_filter_type, "koracut_grade", filter_koracut_grade_init);
    MLT_REGISTER_METADATA(mlt_service_filter_type, "koracut_grade", metadata, "filter_koracut_grade.yml");
}
