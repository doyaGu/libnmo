/**
 * @file nmo_virtools_loader.h
 * @brief Load Virtools data (param types, operations, BBs) from JSON files
 *
 * Reads JSON files produced by VirtoolsDataExporter and registers the data
 * into the appropriate registries. Requires yyjson (via nmo_json target).
 *
 * This is the runtime equivalent of CK2's plugin initialization:
 * - Parameter types   → nmo_type_registry_t
 * - Operation types   → nmo_type_registry_t (NMO_TYPE_CATEGORY_OPERATION)
 * - BB prototypes     → nmo_bb_registry_t
 */

#ifndef NMO_VIRTOOLS_LOADER_H
#define NMO_VIRTOOLS_LOADER_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_operation_registry nmo_operation_registry_t;
typedef struct nmo_bb_registry nmo_bb_registry_t;
typedef struct nmo_extension_registry nmo_extension_registry_t;

/**
 * @brief Load parameter types from JSON file.
 *
 * Reads virtools_parameter_types.json and registers types into the registry.
 * Types already registered (e.g. core builtin types) are skipped.
 * Enum/flags/struct metadata is also registered.
 *
 * @param registry  Type registry
 * @param path      JSON file path
 * @return NMO_OK on success, error status on parse failure
 */
NMO_API nmo_status_t nmo_virtools_load_param_types(
    nmo_type_registry_t *registry, const char *path);

/**
 * @brief Load operation types from JSON file.
 *
 * Reads virtools_operation_types.json and registers:
 * - Operation GUID→name as NMO_TYPE_CATEGORY_OPERATION in type_registry
 * - Full signatures (p1/p2/result type) in operation_registry (function=NULL)
 *
 * @param type_registry  Type registry (for GUID→name)
 * @param op_registry    Operation registry (for signatures, may be NULL to skip)
 * @param path           JSON file path
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_virtools_load_operations(
    nmo_type_registry_t *type_registry,
    nmo_operation_registry_t *op_registry,
    const char *path);

/**
 * @brief Load building block prototypes from JSON file.
 *
 * Reads virtools_building_blocks.json (or _ext.json) and registers
 * BB prototypes into the BB registry.
 *
 * @param bb_registry  BB registry
 * @param path         JSON file path
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_virtools_load_building_blocks(
    nmo_bb_registry_t *bb_registry, const char *path);

/**
 * @brief Load Virtools plugin metadata from JSON file.
 *
 * Reads virtools_plugins.json and registers plugin-level metadata into the
 * extension registry. These entries are metadata-only: they have no init()
 * callback and do not load external DLL code.
 *
 * @param ext_registry  Extension registry
 * @param path          JSON file path
 * @return NMO_OK on success
 */
NMO_API nmo_status_t nmo_virtools_load_plugins(
    nmo_extension_registry_t *ext_registry, const char *path);

/**
 * @brief Load all Virtools data from a directory.
 *
 * Looks for virtools_parameter_types.json, virtools_operation_types.json,
 * and virtools_building_blocks.json (+ _ext.json) in the given directory.
 * Missing files are silently skipped.
 *
 * @param type_registry  Type registry (for param types + operation names)
 * @param op_registry    Operation registry (for operation signatures, may be NULL)
 * @param bb_registry    BB registry (for building blocks)
 * @param data_dir       Directory containing JSON files
 * @return NMO_OK if at least one file loaded successfully
 */
NMO_API nmo_status_t nmo_virtools_load_data_dir(
    nmo_type_registry_t *type_registry,
    nmo_operation_registry_t *op_registry,
    nmo_bb_registry_t *bb_registry,
    const char *data_dir);

#ifdef __cplusplus
}
#endif

#endif /* NMO_VIRTOOLS_LOADER_H */
