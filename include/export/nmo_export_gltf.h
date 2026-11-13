/**
 * @file nmo_export_gltf.h
 * @brief The 3D scene of a file as glTF 2.0 binary (.glb)
 */

#ifndef NMO_EXPORT_GLTF_H
#define NMO_EXPORT_GLTF_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#include <stdbool.h>
#include <stddef.h>

#define NMO_EXPORT_GLTF_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_EXPORT_GLTF_API_TIER NMO_API_TIER_ADVANCED_C

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_type_registry nmo_type_registry_t;

/** @brief What the export writes. */
typedef struct nmo_gltf_export_options {
    /** Put the pixels of each texture in the file as PNG (default). Otherwise,
        and for a texture that holds no pixels, the image names the file the
        texture refers to. */
    bool embed_textures;
    /** Make the key colour of a colour-keyed texture transparent, as the
        engine does when it loads the texture (default). */
    bool apply_color_key;
} nmo_gltf_export_options_t;

/** @brief What the export wrote. */
typedef struct nmo_gltf_export_stats {
    size_t nodes;           /**< 3D entities */
    size_t meshes;          /**< meshes the entities draw */
    size_t primitives;      /**< one per material group with faces */
    size_t materials;
    size_t textures;
    size_t images_embedded; /**< images whose pixels are in the file */
    size_t images_external; /**< images that name a file */
    size_t images_missing;  /**< textures with no pixels and no file name, left out */
    size_t vertices;
    size_t triangles;
} nmo_gltf_export_stats_t;

/** @brief The default options: textures embedded, colour keys applied. */
NMO_API nmo_gltf_export_options_t nmo_gltf_export_options_default(void);

/**
 * @brief Write the 3D entities of a repository and what they draw as a .glb.
 *
 * Every object derived from CK3dEntity becomes a node, in repository order,
 * under its parent entity with the matrix relative to it. A node shows the
 * entity's current mesh; a mesh has one primitive per material group that
 * has faces, with positions, normals (rebuilt from the faces when the file
 * left them out), the first UV set and, for a prelit mesh, vertex colours.
 * A material keeps its diffuse colour and alpha, emissive colour, sidedness,
 * alpha blending or testing, a roughness from its specular power, and its
 * texture with the wrap and filter modes it sets.
 *
 * Virtools is left-handed and glTF right-handed: positions, normals and
 * matrices have their Z negated and triangles their winding reversed.
 * Lights, cameras, animations, skins, extra material channels and lines are
 * not written.
 *
 * @param options NULL for the defaults
 * @param out_stats Optional
 * @return NMO_OK; NMO_ERR_INVALID_ARGUMENT; NMO_ERR_NOMEM; or the error of
 *         writing the file, which is replaced only when the whole file is written
 */
NMO_API nmo_status_t nmo_export_gltf(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *types,
    const char *path,
    const nmo_gltf_export_options_t *options,
    nmo_gltf_export_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* NMO_EXPORT_GLTF_H */
