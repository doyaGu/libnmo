/**
 * @file nmo_bb_virtools.h
 * @brief The Virtools building block pack: the blocks of the stock
 *        building block DLLs (Logics, Narratives, 3D Transformations,
 *        Interface, Visuals, ...) and the managers they bring along, such as
 *        the font manager.
 */

#ifndef NMO_BB_VIRTOOLS_H
#define NMO_BB_VIRTOOLS_H

#include "engine/nmo_engine.h"

#define NMO_BB_VIRTOOLS_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_BB_VIRTOOLS_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

/** Register the stock building blocks and add their managers to `engine`. */
NMO_API nmo_status_t nmo_bb_virtools_register(nmo_engine_t *engine);

/*
 * The fonts the font manager holds, made by Create Font and Create System
 * Font. A Font parameter holds a font's position from 1; 0 is no font.
 */

NMO_API int32_t nmo_engine_font_count(const nmo_engine_t *engine);
/** The name of font `font`; NULL when there is no such font. */
NMO_API const char *nmo_engine_font_name(const nmo_engine_t *engine, int32_t font);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BB_VIRTOOLS_H */
