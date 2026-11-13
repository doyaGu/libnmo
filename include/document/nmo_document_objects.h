#ifndef NMO_DOCUMENT_OBJECTS_H
#define NMO_DOCUMENT_OBJECTS_H

#include "runtime/nmo_document.h"
#include "core/nmo_error.h"

#include <stdbool.h>
#include <stddef.h>

#define NMO_DOCUMENT_OBJECTS_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_DOCUMENT_OBJECTS_API_TIER NMO_API_TIER_STABLE_CONSUMER

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The objects of a document by id, for callers that do not hold object
 * pointers (a binding to another language). An id that names no object gives
 * NULL, 0 or NMO_ERR_NOT_FOUND.
 */

/** @brief Number of objects in the document. */
NMO_API size_t nmo_document_object_count(const nmo_document_t *document);

/** @brief Id of the object at index (file order); 0 past the end. */
NMO_API nmo_object_id_t nmo_document_object_at(const nmo_document_t *document, size_t index);

/** @brief Name of an object; NULL for none. Valid until the object is renamed or removed. */
NMO_API const char *nmo_document_object_name(const nmo_document_t *document, nmo_object_id_t id);

/** @brief Class id of an object; 0 for none. */
NMO_API nmo_class_id_t nmo_document_object_class(const nmo_document_t *document, nmo_object_id_t id);

/** @brief Name of a class ("CK3dObject"); NULL when the document's types do not know it. */
NMO_API const char *nmo_document_class_name(const nmo_document_t *document, nmo_class_id_t class_id);

/** @brief Id of a class by name; 0 when the document's types do not know it. */
NMO_API nmo_class_id_t nmo_document_class_id(const nmo_document_t *document, const char *name);

/** @brief Whether class_id is base_id or derives from it. */
NMO_API bool nmo_document_class_is_derived(
    const nmo_document_t *document,
    nmo_class_id_t class_id,
    nmo_class_id_t base_id);

/**
 * @brief An object and its fields as JSON, as `nmo object export` writes one.
 *
 * {"id", "class_id", "class_name", "name", "fields": [...]}; each field has
 * its name, the GUID of the class that declares it, and its value. An object
 * without a state (a document loaded with NMO_LOAD_PROFILE_OBJECTS, before
 * nmo_document_load_object_state()) has no fields.
 *
 * @param out_json Receives the text; free it with nmo_document_free_json()
 * @param out_size Optional: its length
 */
NMO_API nmo_status_t nmo_document_object_json(
    nmo_document_t *document,
    nmo_object_id_t id,
    char **out_json,
    size_t *out_size);

/**
 * @brief Write object fields given as JSON, as `nmo object import` reads it.
 *
 * {"objects": [{"id", "fields": [{"name", "owner_type_guid", "value"}]}]},
 * the form nmo_document_object_json() gives an object in. The objects must
 * exist; the edit is one workspace edit, undone as a whole on failure. A
 * document loaded with a partial profile gives NMO_ERR_INVALID_STATE.
 *
 * @param out_fields_written Optional
 */
NMO_API nmo_status_t nmo_document_import_json(
    nmo_document_t *document,
    const char *json,
    size_t size,
    size_t *out_fields_written);

/** @brief Free text from nmo_document_object_json(). */
NMO_API void nmo_document_free_json(char *json);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DOCUMENT_OBJECTS_H */
