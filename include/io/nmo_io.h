/**
 * @file nmo_io.h
 * @brief Base IO interface for NMO
 */

#ifndef NMO_IO_H
#define NMO_IO_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Base IO interface
 */
typedef struct nmo_io nmo_io_t;

/**
 * Initialize IO interface
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_io_init(void);

/**
 * Cleanup IO interface
 * @return NMO_OK on success
 */
NMO_API nmo_result_t nmo_io_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_IO_H */
