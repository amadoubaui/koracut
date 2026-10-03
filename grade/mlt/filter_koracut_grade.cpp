/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Le filtre MLT « koracut_grade » : applique un graphe d'étalonnage (document 22). Chargé par
// KoraCut, koracut_render et melt : le même grade à la lecture, dans les rendus d'aperçu et à l'export.
//
// Propriétés :
//   graphe            le graphe JSON (voir kcgrade.h). Vide : le filtre ne fait rien.
//   _koracut_comparer comparaison avant/après pour le module Étalonnage : « » (rien), « split »
//                     (la moitié gauche sans grade) ; le « _ » l'exclut de l'enregistrement du projet.
//   _koracut_position position du partage, 0..1 (défaut 0,5).
//
// Propriétés globales de MLT (mlt_global_properties), posées par l'application seulement :
//   koracut_etalonnage_pause  1 : l'étalonnage est en pause dans l'aperçu (le filtre laisse l'image telle quelle)
//   koracut_etalonnage_force  1 : un calcul d'image de l'application garde l'étalonnage malgré la pause

#include "../kcgrade.h"

#include <framework/mlt.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct Cache
{
    std::mutex mutex;
    std::string json;
    std::shared_ptr<const kcgrade::Grade> grade;
    std::string error;
};

std::shared_ptr<const kcgrade::Grade> gradeFor(mlt_filter filter, Cache *cache)
{
    const char *text = mlt_properties_get(MLT_FILTER_PROPERTIES(filter), "graphe");
    const std::string json = text ? text : "";
    std::lock_guard<std::mutex> lock(cache->mutex);
    if (json != cache->json) {
        cache->json = json;
        cache->grade.reset();
        cache->error.clear();
        if (!json.empty()) {
            // La compilation cuit la LUT : une seule fois par changement du graphe, jamais par image.
            cache->grade = kcgrade::Grade::compile(json, &cache->error);
            if (!cache->grade) {
                mlt_log_warning(MLT_FILTER_SERVICE(filter), "graphe refusé : %s\n", cache->error.c_str());
            }
        }
    }
    return cache->grade;
}

// Répartit les lignes de l'image sur les fils de MLT (mlt_slices), qui existent déjà : aucun fil de plus.
struct SliceJob
{
    int count;
    const std::function<void(int)> *job;
};

int sliceProc(int /*id*/, int idx, int jobs, void *cookie)
{
    auto *s = static_cast<SliceJob *>(cookie);
    const int from = s->count * idx / jobs, to = s->count * (idx + 1) / jobs;
    for (int i = from; i < to; ++i) {
        (*s->job)(i);
    }
    return 0;
}

kcgrade::Runner mltRunner()
{
    return [](int count, const std::function<void(int)> &job) {
        const int jobs = std::max(1, std::min(count, mlt_slices_count_normal()));
        SliceJob s{count, &job};
        mlt_slices_run_normal(jobs, sliceProc, &s);
    };
}

// L'étalonnage en pause dans l'aperçu (bouton du moniteur, 2 octobre 2026). Ces propriétés globales ne sont
// posées que par l'application : koracut_render (export, rendu d'aperçu) et melt ne les voient jamais.
// « koracut_etalonnage_force » : un calcul d'image de l'application qui doit rester étalonné (voir_image…).
bool previewPaused()
{
    mlt_properties globals = mlt_global_properties();
    return globals && mlt_properties_get_int(globals, "koracut_etalonnage_pause") > 0 && mlt_properties_get_int(globals, "koracut_etalonnage_force") <= 0;
}

int filter_get_image(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int writable)
{
    (void)writable;
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_service(frame));
    auto *cache = static_cast<Cache *>(filter->child);
    mlt_properties props = MLT_FILTER_PROPERTIES(filter);
    if (previewPaused()) {
        return mlt_frame_get_image(frame, image, format, width, height, writable);
    }
    std::shared_ptr<const kcgrade::Grade> grade = gradeFor(filter, cache);
    if (!grade || grade->isIdentity()) {
        return mlt_frame_get_image(frame, image, format, width, height, writable);
    }
    *format = *format == mlt_image_rgba64 ? mlt_image_rgba64 : mlt_image_rgba;
    int error = mlt_frame_get_image(frame, image, format, width, height, 1);
    if (error || !*image) {
        return error;
    }
    const char *compare = mlt_properties_get(props, "_koracut_comparer");
    const bool split = compare && std::strcmp(compare, "split") == 0;
    std::vector<uint8_t> before;
    const int bpp = *format == mlt_image_rgba64 ? 8 : 4;
    const size_t size = size_t(*width) * size_t(*height) * size_t(bpp);
    if (split) {
        before.assign(*image, *image + size);
    }
    const kcgrade::Runner run = mltRunner();
    // Mesure (chantier 10) : KORACUT_GRADE_TRACE=1 écrit le temps moyen et la taille toutes les 100 images.
    static const bool trace = std::getenv("KORACUT_GRADE_TRACE") != nullptr;
    const auto t0 = trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    if (*format == mlt_image_rgba64) {
        grade->processRgba16(reinterpret_cast<uint16_t *>(*image), *width, *height, *width * 8, run);
    } else {
        grade->processRgba8(*image, *width, *height, *width * 4, run);
    }
    if (trace) {
        static std::atomic<long> calls{0}, micros{0};
        micros += long(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
        if (++calls % 100 == 0) {
            fprintf(stderr, "KORACUT-GRADE %ld images, %.1f ms en moyenne, derniere %dx%d %s%s\n", calls.load(), double(micros.exchange(0)) / 100000.,
                    *width, *height, *format == mlt_image_rgba64 ? "16 bits" : "8 bits", grade->isPerPixel() ? " LUT" : " image");
        }
    }
    if (split) {
        // Avant / après : la partie gauche garde l'image d'origine, un trait fin marque la coupure.
        double pos = mlt_properties_get(props, "_koracut_position") ? mlt_properties_get_double(props, "_koracut_position") : 0.5;
        const int cut = std::max(0, std::min(*width, int(pos * *width)));
        for (int y = 0; y < *height; ++y) {
            const size_t row = size_t(y) * size_t(*width) * size_t(bpp);
            std::memcpy(*image + row, before.data() + row, size_t(cut) * size_t(bpp));
            if (cut > 0 && cut < *width) {
                std::memset(*image + row + size_t(cut) * size_t(bpp), 0xff, size_t(bpp));
            }
        }
    }
    return 0;
}

mlt_frame filter_process(mlt_filter filter, mlt_frame frame)
{
    mlt_frame_push_service(frame, filter);
    mlt_frame_push_get_image(frame, filter_get_image);
    return frame;
}

void filter_close(mlt_filter filter)
{
    delete static_cast<Cache *>(filter->child);
    filter->child = nullptr;
    filter->close = nullptr;
    filter->parent.close = nullptr;
    mlt_service_close(&filter->parent);
}

} // namespace

extern "C" mlt_filter filter_koracut_grade_init(mlt_profile profile, mlt_service_type type, const char *id, char *arg)
{
    (void)profile;
    (void)type;
    (void)id;
    mlt_filter filter = mlt_filter_new();
    if (!filter) {
        return nullptr;
    }
    filter->child = new Cache();
    filter->process = filter_process;
    filter->close = filter_close;
    if (arg) {
        mlt_properties_set(MLT_FILTER_PROPERTIES(filter), "graphe", arg);
    }
    return filter;
}
