#ifndef NMO_ENGINE_UMBRELLA_H
#define NMO_ENGINE_UMBRELLA_H

/**
 * @file nmo_engine.h
 * @brief Umbrella header of the engine component (library nmo_engine).
 *
 * The behavior engine runs the behavior graphs of a document frame by frame.
 * It lives outside the core library. Link nmo_engine in addition to nmo
 * (CMake: nmo::engine, pkg-config: libnmo-engine).
 */

#include "nmo.h"

#include "engine/nmo_engine.h"
#include "engine/nmo_engine_block.h"

#endif /* NMO_ENGINE_UMBRELLA_H */
