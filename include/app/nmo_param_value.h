/**
 * @file nmo_param_value.h
 * @brief Parameter value decoding — bridges type system string converters
 *        with parameter buffer data for human-readable output.
 *
 * Given a CKParameter (or any derived parameter type) that stores raw bytes
 * plus a type GUID, this module resolves the type descriptor and formats
 * the buffer contents as a string.
 */

#ifndef NMO_PARAM_VALUE_H
#define NMO_PARAM_VALUE_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "type/nmo_type_system.h"
#include "object/builtin/nmo_parameter_schemas.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_session nmo_session_t;
typedef struct nmo_object_repository nmo_object_repository_t;

/**
 * @brief Convert a parameter's stored value to a human-readable string.
 *
 * Resolves the parameter's type_guid against the registry, then dispatches
 * to the appropriate string converter based on storage mode:
 * - BUFFER:   type vtable to_string on buffer_data
 * - OBJECT:   "#<id>" or object name (if session provided)
 * - MANAGER:  "{manager_guid} = <value>"
 * - SUBCHUNK: "<subchunk, N bytes>"
 * - NONE:     "(no value)"
 *
 * Falls back to hex dump for unrecognised types.
 *
 * @param param     Parameter state (must not be NULL)
 * @param registry  Type registry for GUID lookup (must not be NULL)
 * @param session   Session for object name resolution (may be NULL)
 * @param buffer    Output string buffer (caller-owned)
 * @param buffer_size  Buffer capacity in bytes
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_param_value_to_string(
    const nmo_parameter_state_t *param,
    const nmo_type_registry_t *registry,
    const nmo_session_t *session,
    char *buffer,
    size_t buffer_size);

/**
 * @brief Get the resolved type name for a parameter's type_guid.
 *
 * @param param     Parameter state
 * @param registry  Type registry
 * @return Type name string (registry-owned), or NULL if unknown
 */
NMO_API const char *nmo_param_value_type_name(
    const nmo_parameter_state_t *param,
    const nmo_type_registry_t *registry);

/**
 * @brief Get the storage mode as a display string.
 *
 * @param mode  Parameter storage mode
 * @return Static string ("buffer", "object", "subchunk", "manager", "none")
 */
NMO_API const char *nmo_param_mode_to_string(nmo_parameter_mode_t mode);

/**
 * @brief Format a parameter summary line: "type_name = value (mode)".
 *
 * Convenience wrapper combining type name + value + mode into one line.
 *
 * @param param       Parameter state
 * @param registry    Type registry
 * @param session     Session for name resolution (may be NULL)
 * @param buffer      Output string buffer (caller-owned)
 * @param buffer_size Buffer capacity
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_param_value_format_summary(
    const nmo_parameter_state_t *param,
    const nmo_type_registry_t *registry,
    const nmo_session_t *session,
    char *buffer,
    size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PARAM_VALUE_H */
