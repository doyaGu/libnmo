#ifndef NMO_TYPES_H
#define NMO_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Version information
#define NMO_VERSION_MAJOR 1
#define NMO_VERSION_MINOR 0
#define NMO_VERSION_PATCH 0

// Common types
typedef uint32_t nmo_object_id;
typedef uint32_t nmo_class_id;

// Special object ID values
#define NMO_OBJECT_ID_NONE ((nmo_object_id)0)
#define NMO_OBJECT_ID_INVALID ((nmo_object_id)0xFFFFFFFF)
#define NMO_OBJECT_REFERENCE_FLAG (1u << 23)

// File format versions
typedef enum {
    NMO_FILE_VERSION_2 = 2,
    NMO_FILE_VERSION_3 = 3,
    NMO_FILE_VERSION_4 = 4,
    NMO_FILE_VERSION_5 = 5,
    NMO_FILE_VERSION_6 = 6,
    NMO_FILE_VERSION_7 = 7,
    NMO_FILE_VERSION_8 = 8,
    NMO_FILE_VERSION_9 = 9,
} nmo_file_version;

// File write modes
typedef enum {
    NMO_FILE_WRITE_NORMAL = 0,
    NMO_FILE_WRITE_INCLUDE_REFERENCES = 1,
    NMO_FILE_WRITE_EXCLUDE_REFERENCES = 2,
} nmo_file_write_mode;

// Chunk version
#define NMO_CHUNK_VERSION_4 7

// Compression levels
typedef enum {
    NMO_COMPRESS_NONE = 0,
    NMO_COMPRESS_FAST = 1,
    NMO_COMPRESS_DEFAULT = 6,
    NMO_COMPRESS_BEST = 9,
} nmo_compression_level;

// Plugin categories
typedef enum {
    NMO_PLUGIN_MANAGER_DLL = 0,
    NMO_PLUGIN_BEHAVIOR_DLL = 1,
    NMO_PLUGIN_RENDER_DLL = 2,
    NMO_PLUGIN_SOUND_DLL = 3,
    NMO_PLUGIN_INPUT_DLL = 4,
} nmo_plugin_category;

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
