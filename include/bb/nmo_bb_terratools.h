/**
 * @file nmo_bb_terratools.h
 * @brief The Terratools building block pack: the blocks of the TT_*
 *        DLLs (TT Toolbox, TT Gravity, TT Database, TT Interface Manager,
 *        the particle systems) and the Terratools managers they keep.
 */

#ifndef NMO_BB_TERRATOOLS_H
#define NMO_BB_TERRATOOLS_H

#include "engine/nmo_engine.h"

#define NMO_BB_TERRATOOLS_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_BB_TERRATOOLS_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

/** Register the Terratools blocks and add their managers to `engine`. */
NMO_API nmo_status_t nmo_bb_terratools_register(nmo_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BB_TERRATOOLS_H */
