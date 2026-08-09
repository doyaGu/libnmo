/**
 * @file ckbeobject_schemas.c
 * @brief CKBeObject schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKBeObject (behavioral objects).
 * CKBeObject extends CKSceneObject and adds scripts, priority, and attributes.
 * 
 * Based on official Virtools SDK (reference/src/CKBeObject.cpp:400-700):
 * - CKBeObject implements Load/Save with scripts, priority, and attribute data
 * - Many derived classes (CKRenderObject, CKMesh, CKTexture, etc.) do NOT override
 *   Load/Save and inherit this behavior directly
 * - CKRenderObject is an abstract base class with no serialization code
 * 
 * This is the serialization workhorse for the entire BeObject hierarchy.
 */

#include "object/builtin/nmo_beobject_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_guids.h"
#include "object/nmo_manager_guids.h"
#include "object/nmo_param_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_query.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdalign.h>
#include <string.h>

static void nmo_beobject_attribute_dispose(void *element, void *user_data)
{
    (void)user_data;
    nmo_beobject_attribute_t *attribute =
        (nmo_beobject_attribute_t *)element;
    if (attribute != NULL && attribute->chunk != NULL) {
        nmo_chunk_destroy(attribute->chunk);
        attribute->chunk = NULL;
    }
}

static void nmo_beobject_attribute_array_set_lifecycle(nmo_array_t *attributes)
{
    nmo_container_lifecycle_t lifecycle = NMO_CONTAINER_LIFECYCLE_INIT;
    lifecycle.dispose = nmo_beobject_attribute_dispose;
    nmo_array_set_lifecycle(attributes, &lifecycle);
}

static void nmo_beobject_dispose_arrays(nmo_beobject_state_t *state)
{
    if (state == NULL) return;
    nmo_array_dispose(&state->scripts);
    nmo_array_dispose(&state->attributes);
    nmo_array_dispose(&state->legacy_attributes);
}

static nmo_status_t nmo_beobject_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

nmo_status_t nmo_beobject_script_array_append(
    nmo_array_t *scripts,
    nmo_object_id_t script_id)
{
    if (scripts == NULL || scripts->element_size != sizeof(nmo_ref_t)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_ref_t ref = nmo_ref_from_id(script_id);
    return nmo_array_append(scripts, &ref);
}

int nmo_beobject_script_array_find(
    const nmo_array_t *scripts,
    nmo_object_id_t script_id,
    size_t *out_index)
{
    if (scripts == NULL || scripts->element_size != sizeof(nmo_ref_t) ||
        scripts->data == NULL) {
        return 0;
    }
    const nmo_ref_t *refs = NMO_ARRAY_DATA(nmo_ref_t, scripts);
    for (size_t i = 0; i < scripts->count; ++i) {
        if (nmo_ref_runtime_id(&refs[i]) == script_id) {
            if (out_index != NULL) *out_index = i;
            return 1;
        }
    }
    return 0;
}

nmo_object_id_t nmo_beobject_script_array_get_id(
    const nmo_array_t *scripts,
    size_t index)
{
    if (scripts == NULL || scripts->element_size != sizeof(nmo_ref_t) ||
        scripts->data == NULL || index >= scripts->count) {
        return NMO_OBJECT_ID_NONE;
    }
    const nmo_ref_t *refs = NMO_ARRAY_DATA(nmo_ref_t, scripts);
    return nmo_ref_runtime_id(&refs[index]);
}

nmo_status_t nmo_beobject_attribute_array_append(
    nmo_array_t *attributes,
    nmo_object_id_t parameter_id,
    uint32_t type_id,
    nmo_chunk_t *chunk)
{
    if (attributes == NULL ||
        attributes->element_size != sizeof(nmo_beobject_attribute_t)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_beobject_attribute_t attribute = {
        .parameter = nmo_ref_from_id(parameter_id),
        .type_id = type_id,
        .chunk = chunk,
    };
    return nmo_array_append(attributes, &attribute);
}

nmo_status_t nmo_beobject_clone_attributes(
    nmo_arena_t *arena,
    nmo_array_t *destination,
    const nmo_array_t *source)
{
    if (arena == NULL || destination == NULL || source == NULL ||
        (source->element_size != 0 &&
         source->element_size != sizeof(nmo_beobject_attribute_t)) ||
        (source->count > 0 &&
         source->element_size != sizeof(nmo_beobject_attribute_t)) ||
        (source->count > 0 && source->data == NULL)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (destination->data == source->data) {
        memset(destination, 0, sizeof(*destination));
    } else {
        nmo_container_lifecycle_t no_lifecycle = NMO_CONTAINER_LIFECYCLE_INIT;
        nmo_array_set_lifecycle(destination, &no_lifecycle);
        nmo_array_dispose(destination);
    }
    NMO_RETURN_IF_ERROR(nmo_array_init(
        destination,
        sizeof(nmo_beobject_attribute_t),
        source->count,
        &source->allocator));
    nmo_beobject_attribute_array_set_lifecycle(destination);

    nmo_beobject_attribute_t *dst = NULL;
    nmo_status_t result = nmo_array_extend(
        destination, source->count, (void **)&dst);
    if (result != NMO_OK) {
        nmo_array_dispose(destination);
        return result;
    }
    const nmo_beobject_attribute_t *src = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, source);
    for (size_t i = 0; i < source->count; ++i) {
        dst[i].parameter = src[i].parameter;
        dst[i].type_id = src[i].type_id;
        if (src[i].chunk != NULL) {
            dst[i].chunk = nmo_chunk_clone(src[i].chunk, arena);
            if (dst[i].chunk == NULL) {
                nmo_array_dispose(destination);
                return NMO_ERR_NOMEM;
            }
        }
    }
    return NMO_OK;
}

NMO_DEFINE_OBJECT_LIFECYCLE(
    beobject,
    nmo_beobject_state_t,
    do {
        nmo_status_t result = nmo_sceneobject_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
        result = nmo_array_init(
            &state->scripts, sizeof(nmo_ref_t), 0, NULL);
        if (result != NMO_OK) {
            nmo_sceneobject_vtable.destroy(&state->base, NULL, context);
            return result;
        }
        result = nmo_array_init(&state->attributes, sizeof(nmo_beobject_attribute_t), 0, NULL);
        if (result != NMO_OK) {
            nmo_array_dispose(&state->scripts);
            nmo_sceneobject_vtable.destroy(&state->base, NULL, context);
            return result;
        }
        nmo_beobject_attribute_array_set_lifecycle(&state->attributes);
        result = nmo_array_init(
            &state->legacy_attributes,
            sizeof(nmo_beobject_legacy_attribute_t), 0, NULL);
        if (result != NMO_OK) {
            nmo_array_dispose(&state->attributes);
            nmo_array_dispose(&state->scripts);
            nmo_sceneobject_vtable.destroy(&state->base, NULL, context);
            return result;
        }
        state->has_legacy_attributes = 0;
        state->legacy_attr_old_version = 0;
    } while (0),
    do {
        nmo_beobject_dispose_arrays(state);
        nmo_sceneobject_vtable.destroy(&state->base, NULL, context);
    } while (0))

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_beobject_fields[] = {
    /* Base class */
    NMO_FIELD_NAMED("base", offsetof(nmo_beobject_state_t, base),
                    sizeof(nmo_sceneobject_state_t), CKPGUID_SCENEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    /* Scripts */
    NMO_FIELD_ARRAY(nmo_beobject_state_t, scripts, CKPGUID_NONE),
    /* Priority */
    NMO_FIELD(nmo_beobject_state_t, priority, CKPGUID_INT),
    /* Attributes */
    NMO_FIELD_ARRAY(nmo_beobject_state_t, attributes, CKPGUID_NONE),
    /* Legacy attributes */
    NMO_FIELD_ARRAY(nmo_beobject_state_t, legacy_attributes, CKPGUID_NONE),
    NMO_FIELD(nmo_beobject_state_t, has_legacy_attributes, CKPGUID_BOOL),
    NMO_FIELD(nmo_beobject_state_t, legacy_attr_old_version, CKPGUID_BOOL),
    /* Single activity */
    NMO_FIELD(nmo_beobject_state_t, has_single_activity, CKPGUID_BOOL),
    NMO_FIELD(nmo_beobject_state_t, single_activity_flags, NMO_GUID_ENUM_CK_SCENEOBJECTACTIVITY_FLAGS)
};


/* DATAS version flag */
#define CK_DATAS_VERSION_FLAG        0x10000000

/* =============================================================================
 * IDENTIFIER HELPERS
 * ============================================================================= */

static size_t nmo_beobject_identifier_remaining_dwords(nmo_chunk_t *chunk)
{
    if (!chunk || !chunk->parser_state) {
        return 0;
    }

    nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *)chunk->parser_state;
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);

    size_t next_pos = 0;
    if (state->prev_identifier_pos + 1 < chunk->data.count) {
        next_pos = data[state->prev_identifier_pos + 1];
    }
    if (next_pos == 0 || next_pos > chunk->data.count) {
        next_pos = chunk->data.count;
    }
    if (next_pos < state->current_pos) {
        return 0;
    }

    return next_pos - state->current_pos;
}

static nmo_status_t nmo_beobject_read_object_sequence(
    nmo_chunk_t *chunk,
    nmo_array_t *out_refs)
{
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result != NMO_OK) {
        return result;
    }

    if (count > UINT32_MAX) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid object sequence count");
    }
    if (count > nmo_beobject_identifier_remaining_dwords(chunk)) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    nmo_array_t decoded;
    result = nmo_array_init(
        &decoded, sizeof(nmo_ref_t), count, &out_refs->allocator);
    if (result != NMO_OK) return result;

    nmo_ref_t *refs = NULL;
    result = nmo_array_extend(&decoded, count, (void **)&refs);
    if (result != NMO_OK) {
        nmo_array_dispose(&decoded);
        return result;
    }

    for (uint32_t i = 0; i < (uint32_t)count; ++i) {
        result = nmo_ref_read(chunk, &refs[i]);
        if (result != NMO_OK) {
            nmo_array_dispose(&decoded);
            return result;
        }
    }

    NMO_RETURN_IF_ERROR(nmo_array_swap(out_refs, &decoded));
    nmo_array_dispose(&decoded);
    NMO_RETURN_OK();
}

static bool nmo_beobject_is_parameter_object(
    const nmo_object_t *object,
    const nmo_type_registry_t *types)
{
    const nmo_class_id_t classes[] = {
        NMO_CID_PARAMETER,
        NMO_CID_PARAMETERIN,
        NMO_CID_PARAMETEROUT,
        NMO_CID_PARAMETERLOCAL,
    };
    for (size_t i = 0; i < sizeof(classes) / sizeof(classes[0]); ++i) {
        if (nmo_type_query_object_is_derived_from_class(
                types, object, classes[i])) {
            return true;
        }
    }
    return false;
}

static void nmo_beobject_check_parameter_ref(
    nmo_ref_t *ref,
    const nmo_object_repository_t *repository,
    const nmo_type_registry_t *types)
{
    if (ref == NULL || ref->state != NMO_REF_RESOLVED ||
        repository == NULL) {
        return;
    }
    const nmo_object_t *target = nmo_object_repository_find_by_id(
        repository, ref->id);
    if (target != NULL && !nmo_beobject_is_parameter_object(target, types)) {
        ref->state = NMO_REF_CLASS_MISMATCH;
    }
}

static void nmo_beobject_check_ref_classes(
    nmo_beobject_state_t *state,
    void *context)
{
    const nmo_deserialize_context_t *deser =
        nmo_deserialize_context_get(context);
    if (deser == NULL) return;
    const nmo_object_repository_t *repo =
        (const nmo_object_repository_t *)deser->repository;

    if (state->scripts.data != NULL &&
        state->scripts.element_size == sizeof(nmo_ref_t)) {
        nmo_ref_t *scripts = NMO_ARRAY_DATA(nmo_ref_t, &state->scripts);
        for (size_t i = 0; i < state->scripts.count; ++i) {
            nmo_ref_check_class(
                &scripts[i], repo, deser->type_registry, NMO_CID_BEHAVIOR);
        }
    }
    if (state->attributes.data != NULL &&
        state->attributes.element_size == sizeof(nmo_beobject_attribute_t)) {
        nmo_beobject_attribute_t *attributes = NMO_ARRAY_DATA(
            nmo_beobject_attribute_t, &state->attributes);
        for (size_t i = 0; i < state->attributes.count; ++i) {
            nmo_beobject_check_parameter_ref(
                &attributes[i].parameter, repo, deser->type_registry);
        }
    }
    if (state->legacy_attributes.data != NULL &&
        state->legacy_attributes.element_size ==
            sizeof(nmo_beobject_legacy_attribute_t)) {
        nmo_beobject_legacy_attribute_t *attributes = NMO_ARRAY_DATA(
            nmo_beobject_legacy_attribute_t, &state->legacy_attributes);
        for (size_t i = 0; i < state->legacy_attributes.count; ++i) {
            nmo_beobject_check_parameter_ref(
                &attributes[i].parameter, repo, deser->type_registry);
        }
    }
}

static nmo_status_t nmo_beobject_read_legacy_attributes(
    nmo_chunk_t *chunk,
    nmo_beobject_state_t *out_state)
{
    int32_t count_check = 0;
    nmo_status_t result = nmo_chunk_read_int(chunk, &count_check);
    if (result != NMO_OK) return result;
    if (count_check < 0 || count_check > 100000) {
        return NMO_ERR_INVALID_FORMAT;
    }

    uint8_t old_version = 0;
    if (count_check > 0) {
        int32_t compatible_class_id = 0;
        int32_t next_value = 0;
        result = nmo_chunk_read_int(chunk, &compatible_class_id);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_int(chunk, &next_value);
        if (result != NMO_OK) return result;
        if (compatible_class_id < 0 || compatible_class_id >= 0x36 ||
            next_value <= 0 || next_value > 0x41) {
            old_version = 1;
        }
    }

    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ATTRIBUTES);
    if (result != NMO_OK) return result;
    int32_t attr_count = 0;
    result = nmo_chunk_read_int(chunk, &attr_count);
    if (result != NMO_OK) return result;
    if (attr_count != count_check) return NMO_ERR_INVALID_FORMAT;
    const size_t minimum_dwords_per_attribute = old_version ? 5u : 6u;
    if ((size_t)attr_count >
            SIZE_MAX / minimum_dwords_per_attribute ||
        (size_t)attr_count * minimum_dwords_per_attribute >
            nmo_beobject_identifier_remaining_dwords(chunk)) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    nmo_array_t decoded;
    result = nmo_array_init(
        &decoded, sizeof(nmo_beobject_legacy_attribute_t),
        (size_t)attr_count, &out_state->legacy_attributes.allocator);
    if (result != NMO_OK) return result;

    nmo_beobject_legacy_attribute_t *attributes = NULL;
    result = nmo_array_extend(
        &decoded, (size_t)attr_count, (void **)&attributes);
    if (result != NMO_OK) {
        nmo_array_dispose(&decoded);
        return result;
    }

    for (int32_t i = 0; i < attr_count; ++i) {
        if (!old_version) {
            result = nmo_chunk_read_int(
                chunk, &attributes[i].compatible_class_id);
            if (result != NMO_OK) {
                nmo_array_dispose(&decoded);
                return result;
            }
        }
        result = nmo_chunk_read_string_checked(
            chunk, &attributes[i].name, NULL);
        if (result != NMO_OK) {
            nmo_array_dispose(&decoded);
            return result;
        }
        result = nmo_chunk_read_string_checked(
            chunk, &attributes[i].category, NULL);
        if (result != NMO_OK) {
            nmo_array_dispose(&decoded);
            return result;
        }
        result = nmo_chunk_read_guid(
            chunk, &attributes[i].parameter_guid);
        if (result != NMO_OK) {
            nmo_array_dispose(&decoded);
            return result;
        }
        result = nmo_ref_read(chunk, &attributes[i].parameter);
        if (result != NMO_OK) {
            nmo_array_dispose(&decoded);
            return result;
        }
    }

    result = nmo_array_swap(&out_state->legacy_attributes, &decoded);
    nmo_array_dispose(&decoded);
    if (result != NMO_OK) return result;
    out_state->has_legacy_attributes = 1;
    out_state->legacy_attr_old_version = old_version;
    return NMO_OK;
}

/* =============================================================================
 * CKBeObject DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKBeObject state from chunk
 * 
 * Implements the symmetric read operation for CKBeObject::Load.
 * Reads scripts, priority, and attributes using identifier-based approach.
 * 
 * Reference: reference/src/CKBeObject.cpp:550-700
 * 
 * @param chunk Chunk containing CKBeObject data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
static nmo_status_t nmo_beobject_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_beobject_state_t *out_state = (nmo_beobject_state_t *)instance;

    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_beobject_deserialize");
    }

    /* Deserialize base CKSceneObject state first */
    nmo_status_t result = nmo_sceneobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;
    
    const bool is_file = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    const uint32_t data_version = nmo_chunk_get_data_version(chunk);

    /* Load scripts array - optional section (legacy + modern) */
    nmo_last_error_clear();
    result = NMO_OK;
    if (is_file && data_version < 5) {
        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BEHAVIORS);
        if (result == NMO_OK) {
            result = nmo_beobject_read_object_sequence(chunk, &out_state->scripts);
            if (result != NMO_OK) return result;
        } else if (result != NMO_ERR_NOT_FOUND) return result;
    }

    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCRIPTS);
    if (result == NMO_OK) {
        result = nmo_beobject_read_object_sequence(chunk, &out_state->scripts);
        if (result != NMO_OK) return result;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    /* Load priority data - optional section */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_DATAS);
    if (result == NMO_OK) {
        if (!is_file) {
            int32_t ignored = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &ignored));
        } else {
            uint32_t version_flag = 0;
            result = nmo_chunk_read_dword(chunk, &version_flag);
            if (result != NMO_OK) return result;

            if (data_version < 5) {
                int32_t ignored = 0;
                NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &ignored));
                NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &ignored));
                NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &ignored));
                NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->priority));
            } else if (version_flag & CK_DATAS_VERSION_FLAG) {
                NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->priority));
            } else {
                if (data_version >= 5 && nmo_beobject_identifier_remaining_dwords(chunk) > 0) {
                    NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "CKBeObject: DATAS section missing version flag but contains data");
                }
                out_state->priority = 0;
            }
        }
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    /* Load attributes - optional section */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_NEWATTRIBUTES);
    if (result == NMO_OK) {
        /* Read attribute object sequence using proper sequence API
         * Reference: CKBeObject.cpp line 537: const int attrCount = chunk->StartReadSequence(); */
        size_t attr_count = 0;
        result = nmo_chunk_read_object_sequence_start(chunk, &attr_count);
        if (result != NMO_OK) {
            return result;
        }

        if (attr_count > 100000u) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "CKBeObject: attribute count exceeds limit");
        }
        if (attr_count > nmo_beobject_identifier_remaining_dwords(chunk)) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }

        nmo_array_t decoded;
        result = nmo_array_init(
            &decoded,
            sizeof(nmo_beobject_attribute_t),
            attr_count,
            &out_state->attributes.allocator);
        if (result != NMO_OK) return result;
        nmo_beobject_attribute_array_set_lifecycle(&decoded);

        nmo_beobject_attribute_t *attributes = NULL;
        result = nmo_array_extend(&decoded, attr_count, (void **)&attributes);
        if (result != NMO_OK) {
            nmo_array_dispose(&decoded);
            return result;
        }

        for (size_t i = 0; i < attr_count; ++i) {
            result = nmo_ref_read(chunk, &attributes[i].parameter);
            if (result != NMO_OK) {
                nmo_array_dispose(&decoded);
                return result;
            }
        }

        if (attr_count > 0 && !is_file) {
            size_t sub_count = 0;
            result = nmo_chunk_start_read_sub_chunk_sequence(chunk, &sub_count);
            if (result != NMO_OK || sub_count != attr_count) {
                nmo_array_dispose(&decoded);
                return result != NMO_OK ? result : NMO_ERR_VALIDATION_FAILED;
            }
            for (size_t i = 0; i < attr_count; ++i) {
                result = nmo_chunk_read_sub_chunk(chunk, &attributes[i].chunk);
                if (result != NMO_OK) {
                    nmo_array_dispose(&decoded);
                    return result;
                }
            }
        }

        if (attr_count > 0) {
            nmo_guid_t manager_guid;
            size_t seq_count = 0;
            result = nmo_chunk_start_manager_read_sequence(
                chunk, &manager_guid, &seq_count);
            if (result != NMO_OK || seq_count != attr_count ||
                !nmo_guid_equals(manager_guid, NMO_MANAGER_GUID_ATTRIBUTE)) {
                nmo_array_dispose(&decoded);
                return result != NMO_OK ? result : NMO_ERR_VALIDATION_FAILED;
            }
            for (size_t i = 0; i < attr_count; ++i) {
                result = nmo_chunk_read_dword(chunk, &attributes[i].type_id);
                if (result != NMO_OK) {
                    nmo_array_dispose(&decoded);
                    return result;
                }
            }
        }

        NMO_RETURN_IF_ERROR(nmo_array_swap(&out_state->attributes, &decoded));
        nmo_array_dispose(&decoded);
    } else if (result == NMO_ERR_NOT_FOUND) {
        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ATTRIBUTES);
        if (result == NMO_OK) {
            result = nmo_beobject_read_legacy_attributes(chunk, out_state);
            if (result != NMO_OK) return result;
        } else if (result != NMO_ERR_NOT_FOUND) return result;
    } else return result;
    /* If identifier not found, attributes section is optional - continue */

    if (is_file) {
        result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SINGLEACTIVITY);
        if (result == NMO_OK) {
            out_state->has_single_activity = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
                chunk, &out_state->single_activity_flags));
        } else if (result != NMO_ERR_NOT_FOUND) return result;
    }

    nmo_beobject_check_ref_classes(out_state, context);
    /* Deserialization completed - all sections are optional */
    NMO_RETURN_OK();
}

nmo_status_t nmo_beobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_beobject_state_t *out_state = (nmo_beobject_state_t *)instance;
    if (out_state == NULL || chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_beobject_state_t decoded;
    nmo_status_t result = nmo_beobject_create(&decoded, type, context);
    if (result != NMO_OK) {
        nmo_beobject_dispose_arrays(&decoded);
        return result;
    }
    if (out_state->scripts.allocator.alloc != NULL) {
        decoded.scripts.allocator = out_state->scripts.allocator;
    }
    if (out_state->attributes.allocator.alloc != NULL) {
        decoded.attributes.allocator = out_state->attributes.allocator;
    }
    if (out_state->legacy_attributes.allocator.alloc != NULL) {
        decoded.legacy_attributes.allocator =
            out_state->legacy_attributes.allocator;
    }

    result = nmo_beobject_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_beobject_dispose_arrays(&decoded);
        return result;
    }

    nmo_beobject_dispose_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
}

/* =============================================================================
 * CKBeObject SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKBeObject state to chunk
 * 
 * Implements the symmetric write operation for CKBeObject::Save.
 * Writes scripts, priority, and attributes using identifier-based approach.
 * 
 * Reference: reference/src/CKBeObject.cpp:400-550
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
static nmo_status_t nmo_beobject_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_beobject_state_t *in_state = (const nmo_beobject_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_beobject_serialize");
    }

    const bool is_file =
        (out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    const bool write_legacy_attributes =
        in_state->attributes.count == 0 &&
        (in_state->has_legacy_attributes ||
         in_state->legacy_attributes.count > 0);
    if (is_file && in_state->scripts.count > 0 &&
        (in_state->scripts.data == NULL ||
         in_state->scripts.element_size != sizeof(nmo_ref_t) ||
         in_state->scripts.count > UINT32_MAX)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (in_state->attributes.count > 0 &&
        (in_state->attributes.data == NULL ||
         in_state->attributes.element_size !=
             sizeof(nmo_beobject_attribute_t) ||
         in_state->attributes.count > UINT32_MAX)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (write_legacy_attributes &&
        ((in_state->legacy_attributes.count > 0 &&
          in_state->legacy_attributes.data == NULL) ||
         in_state->legacy_attributes.element_size !=
             sizeof(nmo_beobject_legacy_attribute_t) ||
         in_state->legacy_attributes.count > INT32_MAX)) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    /* Write base class (CKSceneObject) data */
    nmo_status_t result = nmo_sceneobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    if (!is_file && (save_flags & CK_STATESAVE_BEOBJECTONLY) == 0) {
        NMO_RETURN_OK();
    }

    /* Write scripts if present (file mode only) */
    if (is_file && in_state->scripts.count > 0 && in_state->scripts.data) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SCRIPTS);
        if (result != NMO_OK) return result;

        /* Write script object sequence */
        result = nmo_chunk_write_object_sequence_start(out_chunk, (uint32_t)in_state->scripts.count);
        if (result != NMO_OK) return result;
        const nmo_ref_t *scripts = NMO_ARRAY_DATA(nmo_ref_t, &in_state->scripts);
        for (uint32_t i = 0; i < in_state->scripts.count; i++) {
            result = nmo_ref_write_sequence_item(out_chunk, &scripts[i]);
            if (result != NMO_OK) return result;
        }
    }

    /* Write priority data if non-zero (file mode only) */
    if (is_file && in_state->priority != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_DATAS);
        if (result != NMO_OK) return result;

        /* Write version flag (modern format) */
        result = nmo_chunk_write_dword(out_chunk, CK_DATAS_VERSION_FLAG);
        if (result != NMO_OK) return result;

        /* Write priority value */
        result = nmo_chunk_write_int(out_chunk, in_state->priority);
        if (result != NMO_OK) return result;
    }

    /* Write legacy attributes if no modern attributes were decoded */
    if (write_legacy_attributes) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ATTRIBUTES);
        if (result != NMO_OK) return result;

        /* Write count */
        result = nmo_chunk_write_int(
            out_chunk, (int32_t)in_state->legacy_attributes.count);
        if (result != NMO_OK) return result;

        const nmo_beobject_legacy_attribute_t *legacy_attributes =
            NMO_ARRAY_DATA(
                nmo_beobject_legacy_attribute_t,
                &in_state->legacy_attributes);
        /* Write each attribute */
        for (size_t i = 0; i < in_state->legacy_attributes.count; ++i) {
            const nmo_beobject_legacy_attribute_t *attribute =
                &legacy_attributes[i];
            /* Write compatibleCid if new version */
            if (!in_state->legacy_attr_old_version) {
                result = nmo_chunk_write_int(
                    out_chunk, attribute->compatible_class_id);
                if (result != NMO_OK) return result;
            }

            /* Write name */
            result = nmo_chunk_write_string(
                out_chunk, attribute->name ? attribute->name : "");
            if (result != NMO_OK) return result;

            /* Write category */
            result = nmo_chunk_write_string(
                out_chunk,
                attribute->category ? attribute->category : "");
            if (result != NMO_OK) return result;

            /* Write parameter GUID */
            result = nmo_chunk_write_guid(
                out_chunk, attribute->parameter_guid);
            if (result != NMO_OK) return result;

            /* Write parameter object ID */
            result = nmo_ref_write(out_chunk, &attribute->parameter);
            if (result != NMO_OK) return result;
        }
    }

    /* Write attributes if present */
    if (in_state->attributes.count > 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_NEWATTRIBUTES);
        if (result != NMO_OK) return result;

        uint32_t attr_count = (uint32_t)in_state->attributes.count;
        const nmo_beobject_attribute_t *attributes = NMO_ARRAY_DATA(
            nmo_beobject_attribute_t, &in_state->attributes);

        /* Start object ID sequence */
        result = nmo_chunk_write_object_sequence_start(out_chunk, attr_count);
        if (result != NMO_OK) return result;

        /* Write attribute parameter object IDs */
        for (uint32_t i = 0; i < attr_count; i++) {
            result = nmo_ref_write_sequence_item(
                out_chunk, &attributes[i].parameter);
            if (result != NMO_OK) return result;
        }

        if (!is_file) {
            result = nmo_chunk_start_sub_chunk_sequence(out_chunk, attr_count);
            if (result != NMO_OK) return result;
            for (uint32_t i = 0; i < attr_count; i++) {
                result = nmo_chunk_write_sub_chunk_sequence(
                    out_chunk, attributes[i].chunk);
                if (result != NMO_OK) return result;
            }
        }

        /* Write manager sequence for attribute types */
        nmo_guid_t attr_mgr_guid = NMO_MANAGER_GUID_ATTRIBUTE;
        result = nmo_chunk_start_manager_sequence(out_chunk, attr_mgr_guid, attr_count);
        if (result != NMO_OK) return result;

        /* Write attribute types */
        for (uint32_t i = 0; i < attr_count; i++) {
            result = nmo_chunk_write_dword(out_chunk, attributes[i].type_id);
            if (result != NMO_OK) return result;
        }
    }

    /* Write single activity flags if present (file mode only) */
    if (is_file && in_state->has_single_activity) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SINGLEACTIVITY);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->single_activity_flags);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_beobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_beobject_validate(instance, type, context));

    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;

    nmo_status_t result = nmo_beobject_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

nmo_status_t nmo_beobject_clone_legacy_attributes(
    nmo_arena_t *arena,
    nmo_array_t *destination,
    const nmo_array_t *source)
{
    if (arena == NULL || destination == NULL || source == NULL ||
        (source->element_size != 0 &&
         source->element_size != sizeof(nmo_beobject_legacy_attribute_t)) ||
        (source->count > 0 &&
         (source->data == NULL || source->element_size !=
             sizeof(nmo_beobject_legacy_attribute_t)))) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (destination->data == source->data) {
        memset(destination, 0, sizeof(*destination));
    } else {
        nmo_array_dispose(destination);
    }
    nmo_status_t result = nmo_array_init(
        destination, sizeof(nmo_beobject_legacy_attribute_t),
        source->count, &source->allocator);
    if (result != NMO_OK) return result;

    nmo_beobject_legacy_attribute_t *dst = NULL;
    result = nmo_array_extend(
        destination, source->count, (void **)&dst);
    if (result != NMO_OK) {
        nmo_array_dispose(destination);
        return result;
    }
    const nmo_beobject_legacy_attribute_t *src = NMO_ARRAY_DATA(
        nmo_beobject_legacy_attribute_t, source);
    for (size_t i = 0; i < source->count; ++i) {
        dst[i].compatible_class_id = src[i].compatible_class_id;
        dst[i].parameter_guid = src[i].parameter_guid;
        dst[i].parameter = src[i].parameter;
        if (src[i].name != NULL) {
            size_t length = strlen(src[i].name) + 1;
            dst[i].name = (char *)nmo_arena_alloc(arena, length, 1);
            if (dst[i].name == NULL) {
                nmo_array_dispose(destination);
                return NMO_ERR_NOMEM;
            }
            memcpy(dst[i].name, src[i].name, length);
        }
        if (src[i].category != NULL) {
            size_t length = strlen(src[i].category) + 1;
            dst[i].category = (char *)nmo_arena_alloc(arena, length, 1);
            if (dst[i].category == NULL) {
                nmo_array_dispose(destination);
                return NMO_ERR_NOMEM;
            }
            memcpy(dst[i].category, src[i].category, length);
        }
    }
    return NMO_OK;
}

static nmo_status_t nmo_beobject_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_beobject_state_t *s = src;
    nmo_beobject_state_t *d = dst;
    (void)type;
    if (s == NULL || d == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_beobject_validate(s, NULL, NULL));

    nmo_beobject_state_t copied;
    nmo_status_t result = nmo_beobject_create(&copied, NULL, NULL);
    if (result != NMO_OK) return result;

    result = nmo_sceneobject_vtable.copy(
        &s->base, &copied.base, NULL, arena);
    if (result != NMO_OK) goto fail;
    copied.priority = s->priority;
    copied.has_single_activity = s->has_single_activity;
    copied.single_activity_flags = s->single_activity_flags;
    copied.has_legacy_attributes = s->has_legacy_attributes;
    copied.legacy_attr_old_version = s->legacy_attr_old_version;

    nmo_array_dispose(&copied.scripts);
    result = nmo_array_clone(
        &s->scripts, &copied.scripts, &s->scripts.allocator);
    if (result != NMO_OK) goto fail;
    result = nmo_beobject_clone_attributes(
        arena, &copied.attributes, &s->attributes);
    if (result != NMO_OK) goto fail;
    result = nmo_beobject_clone_legacy_attributes(
        arena, &copied.legacy_attributes, &s->legacy_attributes);
    if (result != NMO_OK) goto fail;

    if (d->scripts.data == s->scripts.data) {
        memset(&d->scripts, 0, sizeof(d->scripts));
    }
    if (d->attributes.data == s->attributes.data) {
        memset(&d->attributes, 0, sizeof(d->attributes));
    }
    if (d->legacy_attributes.data == s->legacy_attributes.data) {
        memset(&d->legacy_attributes, 0, sizeof(d->legacy_attributes));
    }
    nmo_beobject_destroy(d, NULL, NULL);
    *d = copied;
    return NMO_OK;

fail:
    nmo_beobject_destroy(&copied, NULL, NULL);
    return result;
}

static nmo_status_t nmo_beobject_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_beobject_state_t *s = instance;
    if (s == NULL) return NMO_ERR_INVALID_ARGUMENT;
    NMO_RETURN_IF_ERROR(nmo_sceneobject_vtable.validate(
        &s->base, NULL, context));
    NMO_VALIDATE_COUNT(s->scripts.data, s->scripts.count, "scripts");
    if (s->scripts.count > (size_t)INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (s->scripts.element_size != 0 &&
        s->scripts.element_size != sizeof(nmo_ref_t)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (s->scripts.count > 0 &&
        s->scripts.element_size != sizeof(nmo_ref_t)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_VALIDATE_COUNT(s->attributes.data, s->attributes.count, "attributes");
    if (s->attributes.count > (size_t)INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (s->attributes.element_size != 0 &&
        s->attributes.element_size != sizeof(nmo_beobject_attribute_t)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (s->attributes.count > 0 &&
        s->attributes.element_size != sizeof(nmo_beobject_attribute_t)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    NMO_VALIDATE_COUNT(
        s->legacy_attributes.data, s->legacy_attributes.count,
        "legacy_attributes");
    if (s->legacy_attributes.count > (size_t)INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (s->legacy_attributes.element_size != 0 &&
        s->legacy_attributes.element_size !=
            sizeof(nmo_beobject_legacy_attribute_t)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (s->legacy_attributes.count > 0 &&
        s->legacy_attributes.element_size !=
            sizeof(nmo_beobject_legacy_attribute_t)) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_beobject_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_beobject_prepare_dependencies");
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_beobject_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_beobject_remap_dependencies");
    }

    nmo_beobject_state_t *state = (nmo_beobject_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_sceneobject_remap_dependencies(&state->base, NULL, context));

    if ((state->scripts.count > 0 && state->scripts.data == NULL) ||
        (state->scripts.element_size != 0 &&
         state->scripts.element_size != sizeof(nmo_ref_t)) ||
        (state->scripts.count > 0 &&
         state->scripts.element_size != sizeof(nmo_ref_t))) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "BeObject scripts invalid");
    }

    if ((state->attributes.element_size != 0 &&
         state->attributes.element_size != sizeof(nmo_beobject_attribute_t)) ||
        (state->attributes.count > 0 &&
         state->attributes.element_size != sizeof(nmo_beobject_attribute_t)) ||
        (state->attributes.count > 0 && state->attributes.data == NULL)) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "BeObject attributes are invalid");
    }
    if ((state->legacy_attributes.element_size != 0 &&
         state->legacy_attributes.element_size !=
             sizeof(nmo_beobject_legacy_attribute_t)) ||
        (state->legacy_attributes.count > 0 &&
         (state->legacy_attributes.data == NULL ||
          state->legacy_attributes.element_size !=
              sizeof(nmo_beobject_legacy_attribute_t)))) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "BeObject legacy attributes are invalid");
    }

    /* Preserve invalid references and legacy/modern section presence. */

    return nmo_beobject_validate(state, NULL, NULL);
}

static nmo_status_t nmo_beobject_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_beobject_pre_delete");
    }
    nmo_beobject_state_t *state = (nmo_beobject_state_t *)instance;
    nmo_array_clear(&state->scripts);
    nmo_array_clear(&state->attributes);
    nmo_array_clear(&state->legacy_attributes);
    state->has_legacy_attributes = 0;
    state->legacy_attr_old_version = 0;
    NMO_RETURN_OK();
}

static void nmo_beobject_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

static nmo_status_t nmo_beobject_enumerate_refs(
    const void *instance,
    const nmo_type_descriptor_t *type,
    nmo_type_ref_visitor_fn visitor,
    void *user_data)
{
    (void)type;
    const nmo_beobject_state_t *state =
        (const nmo_beobject_state_t *)instance;
    if (state == NULL || visitor == NULL) return NMO_OK;
    if ((state->scripts.count > 0 && state->scripts.data == NULL) ||
        (state->scripts.element_size != 0 &&
         state->scripts.element_size != sizeof(nmo_ref_t)) ||
        (state->scripts.count > 0 &&
         state->scripts.element_size != sizeof(nmo_ref_t)) ||
        (state->attributes.count > 0 && state->attributes.data == NULL) ||
        (state->attributes.element_size != 0 &&
         state->attributes.element_size != sizeof(nmo_beobject_attribute_t)) ||
        (state->attributes.count > 0 &&
         state->attributes.element_size != sizeof(nmo_beobject_attribute_t)) ||
        (state->legacy_attributes.count > 0 &&
         state->legacy_attributes.data == NULL) ||
        (state->legacy_attributes.element_size != 0 &&
         state->legacy_attributes.element_size !=
             sizeof(nmo_beobject_legacy_attribute_t)) ||
        (state->legacy_attributes.count > 0 &&
         state->legacy_attributes.element_size !=
             sizeof(nmo_beobject_legacy_attribute_t))) {
        return NMO_ERR_VALIDATION_FAILED;
    }

    const nmo_ref_t *scripts = NMO_ARRAY_DATA(nmo_ref_t, &state->scripts);
    for (size_t i = 0; i < state->scripts.count; ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(&scripts[i]);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, NMO_REF_KIND_SCRIPT,
                     "scripts", (uint32_t)i)) {
            return NMO_OK;
        }
    }

    const nmo_beobject_attribute_t *attributes = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &state->attributes);
    for (size_t i = 0; i < state->attributes.count; ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(
            &attributes[i].parameter);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, NMO_REF_KIND_PARAMETER,
                     "attributes", (uint32_t)i)) {
            return NMO_OK;
        }
    }
    const nmo_beobject_legacy_attribute_t *legacy_attributes =
        NMO_ARRAY_DATA(
            nmo_beobject_legacy_attribute_t, &state->legacy_attributes);
    for (size_t i = 0; i < state->legacy_attributes.count; ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(
            &legacy_attributes[i].parameter);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, NMO_REF_KIND_PARAMETER,
                     "legacy_attributes", (uint32_t)i)) {
            return NMO_OK;
        }
    }
    return NMO_OK;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

static const nmo_object_serialize_pass_t nmo_beobject_compare_passes[] = {
    {
        .class_id = NMO_CID_BEOBJECT,
        .data_version = 7,
        .chunk_options = NMO_CHUNK_OPTION_FILE,
    },
    {
        .class_id = NMO_CID_BEOBJECT,
        .data_version = 7,
        .save_flags = CK_STATESAVE_BEOBJECTONLY,
        .use_context = 1,
    },
};

static bool nmo_beobject_equals(const void *a, const void *b)
{
    return nmo_object_serialized_state_equals(
        a, b, nmo_beobject_serialize,
        nmo_beobject_compare_passes,
        sizeof(nmo_beobject_compare_passes) /
            sizeof(nmo_beobject_compare_passes[0]),
        4096);
}

static uint32_t nmo_beobject_hash(const void *instance)
{
    return nmo_object_serialized_state_hash(
        instance, nmo_beobject_serialize,
        nmo_beobject_compare_passes,
        sizeof(nmo_beobject_compare_passes) /
            sizeof(nmo_beobject_compare_passes[0]),
        4096);
}

nmo_type_vtable_t nmo_beobject_vtable = {
    .prepare_dependencies = nmo_beobject_prepare_dependencies,
    .remap_dependencies = nmo_beobject_remap_dependencies,
    .pre_delete = nmo_beobject_pre_delete,
    .post_delete = nmo_beobject_post_delete,
    NMO_OBJECT_VTABLE_EX(
        nmo_beobject_create,
        nmo_beobject_destroy,
        nmo_beobject_serialize,
        nmo_beobject_deserialize,
        nmo_beobject_copy,
        nmo_beobject_validate,
        nmo_beobject_equals,
        nmo_beobject_hash,
        nmo_beobject_enumerate_refs)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_beobject_type,
    CKPGUID_BEOBJECT,
    "CKBeObject",
    NMO_CID_BEOBJECT,
    CKPGUID_SCENEOBJECT,
    nmo_beobject_state_t,
    &nmo_beobject_vtable,
    nmo_beobject_fields)






