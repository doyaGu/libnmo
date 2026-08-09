#ifndef NMO_TYPES_H
#define NMO_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef alignof
#define alignof _Alignof
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Version information
#define NMO_VERSION_MAJOR 0
#define NMO_VERSION_MINOR 2
#define NMO_VERSION_PATCH 0

// Common types
typedef int32_t nmo_status_t;
typedef uint32_t nmo_object_id_t;
typedef uint32_t nmo_class_id_t;
typedef uint32_t nmo_manager_id_t;

// Special object ID values
#define NMO_OBJECT_ID_NONE ((nmo_object_id_t)0)
#define NMO_OBJECT_ID_INVALID ((nmo_object_id_t)0xFFFFFFFF)
#define NMO_OBJECT_REFERENCE_FLAG 0x80000000u

/** Resolution state for a serialized object reference. */
typedef enum nmo_ref_state {
    NMO_REF_NONE = 0,
    NMO_REF_RESOLVED,
    NMO_REF_UNRESOLVED,
    NMO_REF_AMBIGUOUS,
    NMO_REF_CLASS_MISMATCH
} nmo_ref_state_t;

/** Object reference retaining both serialized and runtime identities. */
typedef struct nmo_ref {
    nmo_object_id_t raw_id;
    nmo_object_id_t id;
    nmo_ref_state_t state;
} nmo_ref_t;

// Special class ID values
#define NMO_CLASS_ID_INVALID ((nmo_class_id_t)0xFFFFFFFF)

// File format versions
typedef enum nmo_file_version {
    NMO_FILE_VERSION_2 = 2,
    NMO_FILE_VERSION_3 = 3,
    NMO_FILE_VERSION_4 = 4,
    NMO_FILE_VERSION_5 = 5,
    NMO_FILE_VERSION_6 = 6,
    NMO_FILE_VERSION_7 = 7,
    NMO_FILE_VERSION_8 = 8,
    NMO_FILE_VERSION_9 = 9,
} nmo_file_version_t;

// File write modes
typedef enum nmo_file_write_mode {
    NMO_FILE_WRITE_UNCOMPRESSED          = 0,
    NMO_FILE_WRITE_CHUNK_COMPRESSED_OLD  = 1,
    NMO_FILE_WRITE_EXTERNAL_TEXTURES_OLD = 2,
    NMO_FILE_WRITE_FOR_VIEWER            = 4,
    NMO_FILE_WRITE_WHOLE_COMPRESSED      = 8,
} nmo_file_write_mode_t;

// Chunk versions (matching CKStateChunk CHUNK_VERSION constants exactly)
#define NMO_CHUNK_VERSIONBASE 0
#define NMO_CHUNK_VERSION1 4  // WriteObjectID => table
#define NMO_CHUNK_VERSION2 5  // add Manager Data
#define NMO_CHUNK_VERSION3 6  // New ConvertToBuffer / ReadFromBuffer
#define NMO_CHUNK_VERSION4 7  // New WriteObjectID when saving to a file (Current version)

// CKStateChunk data version written by Virtools 2.1.
#define NMO_CHUNK_DATA_VERSION_CURRENT 10

// Compression levels
typedef enum nmo_compression_level {
    NMO_COMPRESS_NONE    = 0,
    NMO_COMPRESS_FAST    = 1,
    NMO_COMPRESS_DEFAULT = 6,
    NMO_COMPRESS_BEST    = 9,
} nmo_compression_level_t;

// Plugin categories mirror Virtools CK_PLUGIN_TYPE ordering.
typedef enum nmo_plugin_category {
    NMO_PLUGIN_BITMAP_READER      = 0,
    NMO_PLUGIN_SOUND_READER       = 1,
    NMO_PLUGIN_MODEL_READER       = 2,
    NMO_PLUGIN_MANAGER_DLL        = 3,
    NMO_PLUGIN_BEHAVIOR_DLL       = 4,
    NMO_PLUGIN_RENDER_DLL         = 5,
    NMO_PLUGIN_MOVIE_READER       = 6,
    NMO_PLUGIN_EXTENSION_DLL      = 7,
    NMO_PLUGIN_CUSTOM_DLL         = 255
} nmo_plugin_category_t;

/*
 * Public API tier map used by binding-readiness work.
 *
 * Tier 1: Stable consumer API intended for long-lived callers and bindings.
 * Tier 2: Advanced C API kept public for lower-level orchestration/tooling.
 * Tier 3: Public protocol/authoring API kept public for fidelity or plugins.
 */
typedef enum nmo_api_tier {
    NMO_API_TIER_STABLE_CONSUMER = 1,
    NMO_API_TIER_ADVANCED_C = 2,
    NMO_API_TIER_PUBLIC_PROTOCOL = 3
} nmo_api_tier_t;

/*
 * Public header classification:
 * - single-tier headers expose one tier only
 * - mixed-tier headers contain multiple supported API families
 * - excluded headers are public but intentionally outside binding-ready scope
 */
typedef enum nmo_public_header_kind {
    NMO_PUBLIC_HEADER_KIND_SINGLE_TIER = 1,
    NMO_PUBLIC_HEADER_KIND_MIXED_TIER = 2,
    NMO_PUBLIC_HEADER_KIND_EXCLUDED = 3
} nmo_public_header_kind_t;

// Visibility macros
#ifdef _WIN32
#ifdef NMO_BUILD_SHARED
#ifdef NMO_EXPORTS
#define NMO_API __declspec(dllexport)
#else
#define NMO_API __declspec(dllimport)
#endif
#else
#define NMO_API
#endif
#else
#define NMO_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
}
#endif

#endif // NMO_TYPES_H
