/**
 * @file document_objects.c
 * @brief The objects of a document by id
 */

#include "document/nmo_document_objects.h"
#include "../runtime/runtime_internal.h"
#include "format/nmo_object.h"
#include "object/nmo_context.h"
#include "object/nmo_object_edit.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_summary.h"
#include "runtime/nmo_workspace.h"
#include "type/nmo_type_query.h"
#include "yyjson.h"

#include <stdlib.h>
#include <string.h>

static nmo_object_t *document_object(const nmo_document_t *document, nmo_object_id_t id)
{
    nmo_object_repository_t *repo = document != NULL ? nmo_document_get_repository(document) : NULL;
    return repo != NULL && id != NMO_OBJECT_ID_NONE ? nmo_object_repository_find_by_id(repo, id) : NULL;
}

static const nmo_type_registry_t *document_types(const nmo_document_t *document)
{
    nmo_context_t *ctx = document != NULL ? nmo_document_get_context(document) : NULL;
    return ctx != NULL ? nmo_context_get_type_registry(ctx) : NULL;
}

size_t nmo_document_object_count(const nmo_document_t *document)
{
    nmo_object_repository_t *repo = document != NULL ? nmo_document_get_repository(document) : NULL;
    return repo != NULL ? nmo_object_repository_get_count(repo) : 0u;
}

nmo_object_id_t nmo_document_object_at(const nmo_document_t *document, size_t index)
{
    nmo_object_repository_t *repo = document != NULL ? nmo_document_get_repository(document) : NULL;
    nmo_object_t *object = repo != NULL && index < nmo_object_repository_get_count(repo)
        ? nmo_object_repository_get_by_index(repo, index) : NULL;
    return object != NULL ? nmo_object_get_id(object) : NMO_OBJECT_ID_NONE;
}

const char *nmo_document_object_name(const nmo_document_t *document, nmo_object_id_t id)
{
    nmo_object_t *object = document_object(document, id);
    return object != NULL ? nmo_object_get_name(object) : NULL;
}

nmo_class_id_t nmo_document_object_class(const nmo_document_t *document, nmo_object_id_t id)
{
    nmo_object_t *object = document_object(document, id);
    return object != NULL ? nmo_object_get_class_id(object) : 0;
}

const char *nmo_document_class_name(const nmo_document_t *document, nmo_class_id_t class_id)
{
    const nmo_type_registry_t *types = document_types(document);
    return types != NULL ? nmo_type_query_class_name_from_id(types, class_id) : NULL;
}

nmo_class_id_t nmo_document_class_id(const nmo_document_t *document, const char *name)
{
    const nmo_type_registry_t *types = document_types(document);
    return types != NULL && name != NULL ? nmo_type_query_class_id_from_name(types, name) : 0;
}

bool nmo_document_class_is_derived(
    const nmo_document_t *document,
    nmo_class_id_t class_id,
    nmo_class_id_t base_id)
{
    const nmo_type_registry_t *types = document_types(document);
    return types != NULL && nmo_type_query_class_is_derived_from(types, class_id, base_id);
}

nmo_status_t nmo_document_object_json(
    nmo_document_t *document,
    nmo_object_id_t id,
    char **out_json,
    size_t *out_size)
{
    if (out_json != NULL) *out_json = NULL;
    if (out_size != NULL) *out_size = 0;
    if (document == NULL || out_json == NULL) return NMO_ERR_INVALID_ARGUMENT;
    nmo_object_t *object = document_object(document, id);
    if (object == NULL) return NMO_ERR_NOT_FOUND;

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    yyjson_mut_val *holder = doc != NULL ? yyjson_mut_obj(doc) : NULL;
    if (root == NULL || holder == NULL) {
        yyjson_mut_doc_free(doc);
        return NMO_ERR_NOMEM;
    }
    yyjson_mut_doc_set_root(doc, root);
    const nmo_class_id_t class_id = nmo_object_get_class_id(object);
    const char *class_name = nmo_document_class_name(document, class_id);
    const char *name = nmo_object_get_name(object);
    yyjson_mut_obj_add_uint(doc, root, "id", id);
    yyjson_mut_obj_add_uint(doc, root, "class_id", class_id);
    if (class_name != NULL) yyjson_mut_obj_add_strcpy(doc, root, "class_name", class_name);
    if (name != NULL) yyjson_mut_obj_add_strcpy(doc, root, "name", name);

    /* The fields as the object export writes them */
    nmo_summary_config_t config = nmo_summary_config_default();
    config.resolve_object_refs = true;
    config.format_enum_names = true;
    config.format_flags_names = true;
    nmo_summary_output_t output = {
        .stream = NULL,
        .json_doc = doc,
        .json_data = holder,
        .is_json = true,
        .colorize = false,
        .ctx = nmo_document_get_context(document),
        .session = nmo_document_internal_session(document),
    };
    yyjson_mut_val *fields = NULL;
    if (nmo_object_summary_with_config(object, &output, &config)) {
        fields = yyjson_mut_obj_get(holder, "fields");
    }
    yyjson_mut_obj_add_val(doc, root, "fields", fields != NULL ? fields : yyjson_mut_arr(doc));

    size_t size = 0;
    char *json = yyjson_mut_write(doc, 0, &size);
    yyjson_mut_doc_free(doc);
    if (json == NULL) return NMO_ERR_NOMEM;
    *out_json = json;
    if (out_size != NULL) *out_size = size;
    return NMO_OK;
}

nmo_status_t nmo_document_import_json(
    nmo_document_t *document,
    const char *json,
    size_t size,
    size_t *out_fields_written)
{
    if (out_fields_written != NULL) *out_fields_written = 0;
    if (document == NULL || json == NULL) return NMO_ERR_INVALID_ARGUMENT;
    if (nmo_document_internal_is_partial_load(document)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "A document loaded with a partial profile cannot be edited");
    }
    nmo_workspace_t *workspace = NULL;
    nmo_status_t status = nmo_workspace_create(nmo_document_get_context(document), document, &workspace);
    if (status != NMO_OK) return status;
    nmo_import_result_t result;
    memset(&result, 0, sizeof(result));
    status = nmo_object_edit_import_json(workspace, json, size, 0u, &result);
    nmo_workspace_destroy(workspace);
    if (status == NMO_OK && result.errors > 0) status = NMO_ERR_VALIDATION_FAILED;
    if (out_fields_written != NULL) *out_fields_written = result.fields_written;
    return status;
}

void nmo_document_free_json(char *json)
{
    free(json);
}
