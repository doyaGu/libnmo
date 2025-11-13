#ifndef NMO_GUID_H
#define NMO_GUID_H

#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file nmo_guid.h
 * @brief GUID (Globally Unique Identifier) operations
 *
 * Virtools uses 8-byte GUIDs (two 32-bit values) for identifying
 * managers, plugins, and other components.
 *
 * Format: {D1D1D1D1-D2D2D2D2}
 */

/**
 * @brief 8-byte GUID structure
 */
typedef struct nmo_guid {
    uint32_t d1; /**< First 32 bits */
    uint32_t d2; /**< Second 32 bits */
} nmo_guid_t;

/**
 * @brief Null GUID constant
 */
#define NMO_GUID_NULL ((nmo_guid_t){0, 0})

/**
 * @brief Check if two GUIDs are equal
 *
 * @param a First GUID
 * @param b Second GUID
 * @return 1 if equal, 0 otherwise
 */
NMO_API int nmo_guid_equals(nmo_guid_t a, nmo_guid_t b);

/**
 * @brief Check if GUID is null
 *
 * @param guid GUID to check
 * @return 1 if null, 0 otherwise
 */
NMO_API int nmo_guid_is_null(nmo_guid_t guid);

/**
 * @brief Compute hash of GUID for hash tables
 *
 * @param guid GUID to hash
 * @return Hash value
 */
NMO_API uint32_t nmo_guid_hash(nmo_guid_t guid);

/**
 * @brief Parse GUID from string
 *
 * Formats supported:
 * - {D1D1D1D1-D2D2D2D2}
 * - D1D1D1D1-D2D2D2D2
 * - D1D1D1D1D2D2D2D2
 *
 * @param str String to parse
 * @return Parsed GUID or NMO_GUID_NULL on error
 */
NMO_API nmo_guid_t nmo_guid_parse(const char *str);

/**
 * @brief Format GUID to string
 *
 * Format: {D1D1D1D1-D2D2D2D2}
 *
 * @param guid GUID to format
 * @param buffer Output buffer (must be at least 21 bytes)
 * @param size Size of output buffer
 * @return Number of characters written (excluding null terminator)
 */
NMO_API int nmo_guid_format(nmo_guid_t guid, char *buffer, size_t size);

/**
 * @brief Create GUID from two 32-bit values
 *
 * @param d1 First 32 bits
 * @param d2 Second 32 bits
 * @return GUID
 */
static inline nmo_guid_t nmo_guid_create(uint32_t d1, uint32_t d2) {
    nmo_guid_t guid = {d1, d2};
    return guid;
}

#ifdef __cplusplus
}
#endif

#endif // NMO_GUID_H
