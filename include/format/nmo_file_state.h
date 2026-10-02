/**
 * @file nmo_file_state.h
 * @brief What a load records about the file: version, checksum, manager data,
 *        plugin dependencies, included files, load statistics and plugin
 *        diagnostics
 *
 * Plain data shared by the session, which fills it while loading, and the
 * document API, which reports it (document/nmo_document_load.h).
 */

#ifndef NMO_FILE_STATE_H
#define NMO_FILE_STATE_H

#include "nmo_types.h"
#include "core/nmo_arena_array.h"
#include "core/nmo_guid.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_plugin_dep nmo_plugin_dep_t;
typedef struct nmo_manager_data nmo_manager_data_t;

/** Result of comparing the stored file checksum with the one computed from the file bytes. */
typedef enum nmo_crc_status {
    NMO_CRC_NOT_CHECKED = 0,   /**< The sections the checksum covers were not read */
    NMO_CRC_OK,                /**< Stored and computed checksums agree */
    NMO_CRC_MISMATCH           /**< They differ; CK2 refuses such a file (CKERR_FILECRCERROR) */
} nmo_crc_status_t;

typedef struct nmo_file_info {
    uint32_t file_version;
    uint32_t file_version2;
    uint32_t ck_version;
    uint32_t product_version;
    uint32_t product_build;
    size_t file_size;
    uint32_t object_count;
    uint32_t manager_count;
    uint32_t write_mode;
    nmo_crc_status_t crc_status;   /**< Set once the data section has been read */
    uint32_t crc_stored;           /**< Checksum in the file header */
    uint32_t crc_computed;         /**< Checksum computed as the file version defines it */
} nmo_file_info_t;

typedef struct nmo_file_state {
    nmo_file_info_t info;
    nmo_manager_data_t *manager_data;
    uint32_t manager_data_count;
    nmo_plugin_dep_t *plugin_deps;
    uint32_t plugin_dep_count;
} nmo_file_state_t;

typedef struct nmo_runtime_load_stats {
    size_t total_objects;
    uint32_t flags;
    struct {
        uint32_t total;
        uint32_t resolved;
        uint32_t unresolved;
        uint32_t ambiguous;
        uint32_t unresolved_preview_count;
        struct {
            nmo_object_id_t id;
            nmo_class_id_t class_id;
        } unresolved_preview[8];
    } references;
    struct {
        size_t class_entries;
        size_t name_entries;
        size_t guid_entries;
        size_t memory_usage;
    } indexes;
    struct {
        uint32_t invoked;
        uint32_t errors;
    } object_postload;
    uint32_t manager_errors;
} nmo_runtime_load_stats_t;

typedef struct nmo_session_plugin_dependency_status {
    nmo_guid_t guid;
    nmo_plugin_category_t category;
    uint32_t required_version;
    uint32_t resolved_version;
    const char *resolved_name;
    uint32_t status_flags;
} nmo_session_plugin_dependency_status_t;

#define NMO_SESSION_PLUGIN_DEP_STATUS_MISSING             0x00000001u
#define NMO_SESSION_PLUGIN_DEP_STATUS_VERSION_TOO_OLD     0x00000002u
#define NMO_SESSION_PLUGIN_DEP_STATUS_MANAGER_UNAVAILABLE 0x00000004u

typedef struct nmo_session_plugin_diagnostics {
    const nmo_session_plugin_dependency_status_t *entries;
    size_t entry_count;
    size_t missing_count;
    size_t outdated_count;
    int extension_registry_available;
} nmo_session_plugin_diagnostics_t;

typedef struct nmo_included_file {
    const char *name;
    const void *data;
    uint32_t size;
    nmo_arena_array_t owner_ids;
    uint32_t attributes;
} nmo_included_file_t;

#define NMO_INCLUDED_FILE_ATTR_BORROWED      0x00000001u
#define NMO_INCLUDED_FILE_ATTR_METADATA_ONLY 0x00000002u

typedef struct nmo_included_file_metadata {
    const nmo_object_id_t *owner_ids;
    uint32_t owner_count;
    uint32_t attributes;
} nmo_included_file_metadata_t;

#ifdef __cplusplus
}
#endif

#endif /* NMO_FILE_STATE_H */
