#ifndef NMO_TYPES_H
#define NMO_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Version information
#define NMO_VERSION_MAJOR 1
#define NMO_VERSION_MINOR 0
#define NMO_VERSION_PATCH 0

// Common types
typedef uint32_t nmo_object_id_t;
typedef uint32_t nmo_class_id_t;
typedef uint32_t nmo_manager_id_t;

// Special object ID values
#define NMO_OBJECT_ID_NONE ((nmo_object_id_t)0)
#define NMO_OBJECT_ID_INVALID ((nmo_object_id_t)0xFFFFFFFF)
#define NMO_OBJECT_REFERENCE_FLAG (1u << 23)

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
    NMO_FILE_WRITE_NORMAL             = 0,
    NMO_FILE_WRITE_INCLUDE_REFERENCES = 1,
    NMO_FILE_WRITE_EXCLUDE_REFERENCES = 2,
} nmo_file_write_mode_t;

// Chunk versions (matching CKStateChunk CHUNK_VERSION constants exactly)
#define NMO_CHUNK_VERSIONBASE 0
#define NMO_CHUNK_VERSION1 4  // WriteObjectID => table
#define NMO_CHUNK_VERSION2 5  // add Manager Data
#define NMO_CHUNK_VERSION3 6  // New ConvertToBuffer / ReadFromBuffer
#define NMO_CHUNK_VERSION4 7  // New WriteObjectID when saving to a file (Current version)

// Legacy aliases for compatibility
#define NMO_CHUNK_VERSION_1 NMO_CHUNK_VERSION1
#define NMO_CHUNK_VERSION_2 NMO_CHUNK_VERSION2
#define NMO_CHUNK_VERSION_3 NMO_CHUNK_VERSION3
#define NMO_CHUNK_VERSION_4 NMO_CHUNK_VERSION4

// Compression levels
typedef enum nmo_compression_level {
    NMO_COMPRESS_NONE    = 0,
    NMO_COMPRESS_FAST    = 1,
    NMO_COMPRESS_DEFAULT = 6,
    NMO_COMPRESS_BEST    = 9,
} nmo_compression_level_t;

// Plugin categories (mirrors CKPluginManager ordering for compatibility)
typedef enum nmo_plugin_category {
    NMO_PLUGIN_MANAGER_DLL        = 0,
    NMO_PLUGIN_BEHAVIOR_DLL       = 1,
    NMO_PLUGIN_RENDER_DLL         = 2,
    NMO_PLUGIN_SOUND_DLL          = 3,
    NMO_PLUGIN_INPUT_DLL          = 4,
    NMO_PLUGIN_OBJECT_READER_DLL  = 5,
    NMO_PLUGIN_CUSTOM_DLL         = 255
} nmo_plugin_category_t;

/* Backward-compatible aliases kept for existing code */
#define NMO_PLUGIN_MANAGER_DLL       NMO_PLUGIN_MANAGER_DLL
#define NMO_PLUGIN_BEHAVIOR_DLL      NMO_PLUGIN_BEHAVIOR_DLL
#define NMO_PLUGIN_RENDER_DLL        NMO_PLUGIN_RENDER_DLL
#define NMO_PLUGIN_SOUND_DLL         NMO_PLUGIN_SOUND_DLL
#define NMO_PLUGIN_INPUT_DLL         NMO_PLUGIN_INPUT_DLL
#define NMO_PLUGIN_OBJECT_READER_DLL NMO_PLUGIN_OBJECT_READER_DLL
#define NMO_PLUGIN_CUSTOM_DLL        NMO_PLUGIN_CUSTOM_DLL

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
