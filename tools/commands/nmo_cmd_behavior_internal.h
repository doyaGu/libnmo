/**
 * @file nmo_cmd_behavior_internal.h
 * @brief Shared helpers for behavior command split files
 */

#ifndef NMO_CMD_BEHAVIOR_INTERNAL_H
#define NMO_CMD_BEHAVIOR_INTERNAL_H

#include "../nmo_cmd_ctx.h"
#include "format/nmo_interface_chunk.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_types.h"
#include "type/nmo_type_system.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Behavior flag constants (enum values from nmo_object_enum_defs.h) */
#include "object/nmo_object_enum_defs.h"

/**
 * @brief Check if a class ID is CKBehavior or derived from it.
 */
int is_behavior_class(const nmo_type_registry_t *registry, nmo_class_id_t class_id);

/**
 * @brief Resolve an object ID to its name.
 * Returns "(none)" for 0, "(missing)" for unknown, "(unnamed)" for empty.
 */
const char *resolve_name(nmo_object_repository_t *repo, nmo_object_id_t id);

/**
 * @brief Resolve a parameter type GUID to a human-readable name.
 */
const char *resolve_type(const nmo_type_registry_t *reg, nmo_guid_t guid);

/**
 * @brief Get the parameter type GUID from any parameter object.
 */
nmo_guid_t get_param_type_guid(nmo_object_t *obj);

/**
 * @brief Find a sub-behavior entry in interface data by behavior ID.
 */
const nmo_interface_behavior_t *find_interface_sub(
    const nmo_interface_data_t *idata, nmo_object_id_t behavior_id);

/**
 * @brief Find the position of a behavior in interface data.
 */
bool find_interface_position(const nmo_interface_data_t *idata,
                             nmo_object_id_t behavior_id,
                             float *out_x, float *out_y);

/**
 * @brief Find the position of an operation in interface data.
 */
bool find_operation_position(const nmo_interface_data_t *idata,
                             nmo_object_id_t op_id,
                             float *out_x, float *out_y);

/**
 * @brief Find a link entry in interface data by link ID.
 */
const nmo_interface_link_t *find_interface_link(
    const nmo_interface_data_t *idata, nmo_object_id_t link_id);

/**
 * @brief Convert interface color uint32 to "#RRGGBB" hex string.
 */
const char *interface_color_to_hex(uint32_t color, char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_BEHAVIOR_INTERNAL_H */
