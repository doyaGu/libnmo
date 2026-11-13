#ifndef NMO_DOCUMENT_EXPORT_H
#define NMO_DOCUMENT_EXPORT_H

#include "runtime/nmo_document.h"
#include "core/nmo_error.h"
#include "export/nmo_export_gltf.h"

#define NMO_DOCUMENT_EXPORT_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_DOCUMENT_EXPORT_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Write the 3D scene of a document as a glTF 2.0 binary (.glb) file.
 *
 * nmo_export_gltf() over the document's objects and types; a document loaded
 * with a partial profile has no object states and writes an empty scene.
 *
 * @param options NULL for nmo_gltf_export_options_default()
 * @param out_stats Optional
 */
NMO_API nmo_status_t nmo_document_export_gltf(
    nmo_document_t *document,
    const char *path,
    const nmo_gltf_export_options_t *options,
    nmo_gltf_export_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DOCUMENT_EXPORT_H */
