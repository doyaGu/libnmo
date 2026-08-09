/**
 * @file ckscene_schemas.c
 * @brief CKScene schema definitions with serialize/deserialize implementations
 *
 * Implements schema-driven deserialization for CKScene (scene container).
 * CKScene extends CKBeObject and manages scene objects with initial states.
 * 
 * Based on official Virtools SDK (reference/src/CKScene.cpp:692-890):
 * - CKScene::Save writes object descriptors with initial value chunks
 * - Each object has activation/reset flags
 * - Rendering settings stored in separate identifier section
 */

#include "object/builtin/nmo_scene_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_object_struct_guids.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "object/nmo_object_repository.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include "type/nmo_type_system.h"
#include "object/nmo_ref_graph.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

static void nmo_scene_object_desc_dispose(void *element, void *user_data)
{
    (void)user_data;
    nmo_scene_object_desc_t *desc = (nmo_scene_object_desc_t *)element;
    if (desc == NULL) return;
    if (desc->initial_value != NULL) {
        nmo_chunk_destroy(desc->initial_value);
        desc->initial_value = NULL;
    }
    if (desc->reserved != NULL) {
        nmo_chunk_destroy(desc->reserved);
        desc->reserved = NULL;
    }
}

static void nmo_scene_object_descs_set_lifecycle(nmo_array_t *descs)
{
    nmo_container_lifecycle_t lifecycle = NMO_CONTAINER_LIFECYCLE_INIT;
    lifecycle.dispose = nmo_scene_object_desc_dispose;
    nmo_array_set_lifecycle(descs, &lifecycle);
}

static void nmo_scene_dispose_state_arrays(nmo_scene_state_t *state);

NMO_DEFINE_OBJECT_LIFECYCLE(
    scene,
    nmo_scene_state_t,
    do {
        nmo_status_t result = nmo_beobject_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
        result = nmo_array_init(
            &state->object_descs, sizeof(nmo_scene_object_desc_t), 0, NULL);
        if (result != NMO_OK) {
            nmo_scene_dispose_state_arrays(state);
            return result;
        }
        nmo_scene_object_descs_set_lifecycle(&state->object_descs);
    } while (0),
    nmo_scene_dispose_state_arrays(state))

static void nmo_scene_dispose_state_arrays(nmo_scene_state_t *state)
{
    if (state == NULL) return;
    nmo_array_dispose(&state->object_descs);
    nmo_beobject_vtable.destroy(&state->base, NULL, NULL);
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_scene_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_scene_state_t, base),
                         sizeof(nmo_beobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF_VALUE(nmo_scene_state_t, level),
    NMO_FIELD_ARRAY(nmo_scene_state_t, object_descs, NMO_GUID_STRUCT_CKSCENEOBJECTDESC),
    NMO_FIELD(nmo_scene_state_t, environment_settings, NMO_GUID_ENUM_CK_SCENE_FLAGS),
    NMO_FIELD_NAMED("background_color", offsetof(nmo_scene_state_t, background_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_NAMED("ambient_light_color", offsetof(nmo_scene_state_t, ambient_light_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_scene_state_t, fog_mode, NMO_GUID_ENUM_VXFOG_MODE),
    NMO_FIELD_NAMED("fog_color", offsetof(nmo_scene_state_t, fog_color),
                    sizeof(uint32_t), CKPGUID_COLOR, NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_scene_state_t, fog_start, CKPGUID_FLOAT),
    NMO_FIELD(nmo_scene_state_t, fog_end, CKPGUID_FLOAT),
    NMO_FIELD(nmo_scene_state_t, fog_density, CKPGUID_FLOAT),
    NMO_FIELD_REF_VALUE(nmo_scene_state_t, background_texture),
    NMO_FIELD_REF_VALUE(nmo_scene_state_t, starting_camera)
};

/* Scene object flags (CKEnums.h) */
#define CK_SCENEOBJECT_START_ACTIVATE   0x0001
#define CK_SCENEOBJECT_ACTIVE           0x0008
#define CK_SCENEOBJECT_START_DEACTIVATE 0x0010
#define CK_SCENEOBJECT_START_RESET      0x0040

static nmo_status_t nmo_scene_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static size_t nmo_scene_identifier_remaining_dwords(
    const nmo_chunk_t *chunk)
{
    if (!chunk || !chunk->parser_state) return 0;

    const nmo_chunk_parser_state_t *state =
        (const nmo_chunk_parser_state_t *)chunk->parser_state;
    const uint32_t *data =
        NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    size_t next_pos = chunk->data.count;
    if (state->prev_identifier_pos + 1u < chunk->data.count) {
        const uint32_t candidate = data[state->prev_identifier_pos + 1u];
        if (candidate != 0 && candidate <= chunk->data.count) {
            next_pos = candidate;
        }
    }
    if (next_pos < state->current_pos) return 0;
    return next_pos - state->current_pos;
}

static nmo_status_t nmo_scene_read_new_data(
    nmo_scene_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context,
    uint32_t data_version)
{
    nmo_ref_t level = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    int32_t desc_count = 0;
    nmo_array_t decoded = {0};
    nmo_status_t result = nmo_ref_read(chunk, &level);
    if (result != NMO_OK) return result;
    result = nmo_chunk_read_int(chunk, &desc_count);
    if (result != NMO_OK) return result;
    if (desc_count < 0) {
        NMO_RETURN_ERROR(
            NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
            "Scene object count is negative");
    }
    if ((uint32_t)desc_count > (uint32_t)INT32_MAX / 2u) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if ((size_t)desc_count >
        nmo_scene_identifier_remaining_dwords(chunk) / 2u) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    const nmo_allocator_t *allocator =
        out_state->object_descs.element_size != 0
            ? &out_state->object_descs.allocator : NULL;
    result = nmo_array_init(
        &decoded, sizeof(nmo_scene_object_desc_t),
        (size_t)desc_count, allocator);
    if (result != NMO_OK) return result;
    nmo_scene_object_descs_set_lifecycle(&decoded);

    nmo_scene_object_desc_t *descs = NULL;
    result = nmo_array_extend(
        &decoded, (size_t)desc_count, (void **)&descs);
    if (result != NMO_OK) goto fail;

    if (desc_count > 0) {
        size_t sequence_count = 0;
        result = nmo_chunk_read_object_sequence_start(
            chunk, &sequence_count);
        if (result != NMO_OK) goto fail;
        if (sequence_count != (size_t)desc_count) {
            result = NMO_ERR_INVALID_FORMAT;
            goto fail;
        }
        for (size_t i = 0; i < sequence_count; ++i) {
            result = nmo_ref_read(chunk, &descs[i].ref);
            if (result != NMO_OK) goto fail;
        }

        size_t sub_chunk_count = 0;
        result = nmo_chunk_start_read_sub_chunk_sequence(
            chunk, &sub_chunk_count);
        if (result != NMO_OK) goto fail;
        if (sub_chunk_count != (size_t)desc_count * 2u) {
            result = NMO_ERR_INVALID_FORMAT;
            goto fail;
        }
        for (int32_t i = 0; i < desc_count; ++i) {
            result = nmo_chunk_read_sub_chunk(
                chunk, &descs[i].initial_value);
            if (result != NMO_OK) goto fail;
            result = nmo_chunk_read_sub_chunk(
                chunk, &descs[i].reserved);
            if (result != NMO_OK) goto fail;
        }

        for (int32_t i = 0; i < desc_count; ++i) {
            uint32_t flags = 0;
            result = nmo_chunk_read_dword(chunk, &flags);
            if (result != NMO_OK) goto fail;
            if (data_version >= 8) {
                descs[i].flags = flags;
            } else {
                uint32_t converted = flags & CK_SCENEOBJECT_ACTIVE;
                if (flags & 2u) converted |= CK_SCENEOBJECT_START_RESET;
                if (flags & 1u) {
                    converted |= CK_SCENEOBJECT_START_ACTIVATE;
                } else {
                    converted |= CK_SCENEOBJECT_START_DEACTIVATE;
                }
                descs[i].flags = converted;
            }
        }
    }

    nmo_ref_check_class(
        &level,
        (const nmo_object_repository_t *)
            nmo_deserialize_context_get_repository(context),
        nmo_deserialize_context_get_type_registry(context),
        NMO_CID_LEVEL);
    nmo_array_dispose(&out_state->object_descs);
    out_state->object_descs = decoded;
    out_state->level = level;
    return NMO_OK;

fail:
    nmo_array_dispose(&decoded);
    return result;
}

static nmo_status_t nmo_scene_read_render_settings(
    nmo_scene_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    uint32_t background_color = 0;
    uint32_t ambient_light_color = 0;
    uint32_t fog_mode = 0;
    uint32_t fog_color = 0;
    float fog_start = 0.0f;
    float fog_end = 0.0f;
    float fog_density = 0.0f;
    nmo_ref_t background_texture = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    nmo_ref_t starting_camera = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &background_color));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &ambient_light_color));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &fog_mode));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &fog_color));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &fog_start));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &fog_end));
    NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &fog_density));
    NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &background_texture));
    NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &starting_camera));

    const nmo_object_repository_t *repository =
        (const nmo_object_repository_t *)
            nmo_deserialize_context_get_repository(context);
    const nmo_type_registry_t *types =
        nmo_deserialize_context_get_type_registry(context);
    nmo_ref_check_class(
        &background_texture, repository, types, NMO_CID_TEXTURE);
    nmo_ref_check_class(
        &starting_camera, repository, types, NMO_CID_CAMERA);

    out_state->background_color = background_color;
    out_state->ambient_light_color = ambient_light_color;
    out_state->fog_mode = fog_mode;
    out_state->fog_color = fog_color;
    out_state->fog_start = fog_start;
    out_state->fog_end = fog_end;
    out_state->fog_density = fog_density;
    out_state->background_texture = background_texture;
    out_state->starting_camera = starting_camera;
    return NMO_OK;
}

/* =============================================================================
 * CKScene DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKScene state from chunk
 * 
 * Implements the symmetric read operation for CKScene::Load.
 * Reads scene objects with initial states and rendering settings.
 * 
 * Reference: reference/src/CKScene.cpp:776-890
 * 
 * @param chunk Chunk containing CKScene data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
static nmo_status_t nmo_scene_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_scene_state_t *out_state = (nmo_scene_state_t *)instance;
    if (chunk == NULL || out_state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_scene_deserialize");
    }

    const uint32_t data_version = nmo_chunk_get_data_version(chunk);
    if (data_version < 1) {
        NMO_RETURN_ERROR(NMO_ERR_UNSUPPORTED_VERSION, NMO_SEVERITY_ERROR,
                         "CKScene data_version < 1 is not supported");
    }

    /* Deserialize base CKBeObject state first */
    nmo_status_t result = nmo_beobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Section 1: SCENENEWDATA - Level + scene objects */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCENENEWDATA);
    if (result == NMO_OK) {
        NMO_RETURN_IF_ERROR(nmo_scene_read_new_data(
            out_state, chunk, context, data_version));
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    /* Section 2: SCENELAUNCHED - Environment settings */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCENELAUNCHED);
    if (result == NMO_OK) {
        uint32_t environment_settings = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(
            chunk, &environment_settings));
        out_state->environment_settings = environment_settings;
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    /* Section 3: SCENERENDERSETTINGS - Rendering configuration */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCENERENDERSETTINGS);
    if (result == NMO_OK) {
        NMO_RETURN_IF_ERROR(nmo_scene_read_render_settings(
            out_state, chunk, context));
    } else if (result != NMO_ERR_NOT_FOUND) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_scene_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_scene_state_t *out_state = (nmo_scene_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_scene_state_t decoded = {0};
    if (out_state->base.scripts.allocator.alloc != NULL) {
        decoded.base.scripts.allocator = out_state->base.scripts.allocator;
    }
    if (out_state->base.attributes.allocator.alloc != NULL) {
        decoded.base.attributes.allocator = out_state->base.attributes.allocator;
    }
    if (out_state->base.legacy_attributes.allocator.alloc != NULL) {
        decoded.base.legacy_attributes.allocator =
            out_state->base.legacy_attributes.allocator;
    }
    const nmo_allocator_t *allocator =
        out_state->object_descs.allocator.alloc != NULL
            ? &out_state->object_descs.allocator : NULL;
    nmo_status_t result = nmo_array_init(
        &decoded.object_descs, sizeof(nmo_scene_object_desc_t), 0, allocator);
    if (result != NMO_OK) return result;
    nmo_scene_object_descs_set_lifecycle(&decoded.object_descs);
    result = nmo_scene_deserialize_internal(
        &decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_scene_dispose_state_arrays(&decoded);
        return result;
    }
    nmo_scene_dispose_state_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
}

/* =============================================================================
 * CKScene SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKScene state to chunk
 * 
 * Implements the symmetric write operation for CKScene::Save.
 * Writes scene objects with initial states and rendering settings.
 * 
 * Reference: reference/src/CKScene.cpp:692-775
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
static nmo_status_t nmo_scene_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_scene_state_t *in_state = (const nmo_scene_state_t *)instance;

    if (in_state == NULL || out_chunk == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_scene_serialize");
    }
    NMO_RETURN_IF_ERROR(nmo_scene_validate(in_state, type, context));

    /* Write base class (CKBeObject) data */
    nmo_status_t result = nmo_beobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Section 1: SCENENEWDATA */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SCENENEWDATA);
    if (result != NMO_OK) return result;

    /* Write level ID */
    result = nmo_ref_write(out_chunk, &in_state->level);
    if (result != NMO_OK) return result;

    /* Write object count */
    result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->object_descs.count);
    if (result != NMO_OK) return result;

    if (in_state->object_descs.count > 0) {
        /* Write object ID sequence */
        result = nmo_chunk_write_object_sequence_start(
            out_chunk, in_state->object_descs.count);
        if (result != NMO_OK) return result;

        const nmo_scene_object_desc_t *descs = NMO_ARRAY_DATA(nmo_scene_object_desc_t,
                                                              &in_state->object_descs);
        for (uint32_t i = 0; i < in_state->object_descs.count; i++) {
            result = nmo_ref_write_sequence_item(
                out_chunk, &descs[i].ref);
            if (result != NMO_OK) return result;
        }

        /* Write sub-chunk sequence (initial values + reserved NULLs) */
        result = nmo_chunk_start_sub_chunk_sequence(out_chunk,
                                                    (uint32_t)in_state->object_descs.count * 2);
        if (result != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->object_descs.count; i++) {
            /* Write initial value chunk */
            if (descs[i].initial_value) {
                result = nmo_chunk_write_sub_chunk_sequence(
                    out_chunk,
                    descs[i].initial_value);
                if (result != NMO_OK) return result;
            } else {
                /* Write NULL chunk */
                result = nmo_chunk_write_sub_chunk_sequence(out_chunk, NULL);
                if (result != NMO_OK) return result;
            }

            /* Preserve the reserved companion chunk when present. */
            result = nmo_chunk_write_sub_chunk_sequence(
                out_chunk, descs[i].reserved);
            if (result != NMO_OK) return result;
        }

        /* Write object flags */
        for (uint32_t i = 0; i < in_state->object_descs.count; i++) {
            result = nmo_chunk_write_dword(out_chunk, descs[i].flags);
            if (result != NMO_OK) return result;
        }
    }

    /* Section 2: SCENELAUNCHED */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SCENELAUNCHED);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->environment_settings);
    if (result != NMO_OK) return result;

    /* Section 3: SCENERENDERSETTINGS */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SCENERENDERSETTINGS);
    if (result != NMO_OK) return result;

    /* Background and ambient */
    result = nmo_chunk_write_dword(out_chunk, in_state->background_color);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->ambient_light_color);
    if (result != NMO_OK) return result;

    /* Fog settings */
    result = nmo_chunk_write_dword(out_chunk, in_state->fog_mode);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->fog_color);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_float(out_chunk, in_state->fog_start);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_float(out_chunk, in_state->fog_end);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_float(out_chunk, in_state->fog_density);
    if (result != NMO_OK) return result;

    /* Scene references */
    result = nmo_ref_write(out_chunk, &in_state->background_texture);
    if (result != NMO_OK) return result;

    result = nmo_ref_write(out_chunk, &in_state->starting_camera);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

nmo_status_t nmo_scene_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    if (instance == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_scene_serialize_internal(
        instance, staged, type, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

static nmo_status_t nmo_scene_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    const nmo_scene_state_t *s = src;
    nmo_scene_state_t *d = dst;
    if (s == NULL || d == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_scene_validate(s, NULL, NULL));

    nmo_scene_state_t copied;
    nmo_status_t result = nmo_scene_create(&copied, NULL, NULL);
    if (result != NMO_OK) return result;
    result = nmo_beobject_vtable.copy(
        &s->base, &copied.base, NULL, arena);
    if (result != NMO_OK) goto fail;

    copied.level = s->level;
    copied.environment_settings = s->environment_settings;
    copied.background_color = s->background_color;
    copied.ambient_light_color = s->ambient_light_color;
    copied.fog_mode = s->fog_mode;
    copied.fog_color = s->fog_color;
    copied.fog_start = s->fog_start;
    copied.fog_end = s->fog_end;
    copied.fog_density = s->fog_density;
    copied.background_texture = s->background_texture;
    copied.starting_camera = s->starting_camera;

    nmo_array_dispose(&copied.object_descs);
    result = nmo_array_init(
        &copied.object_descs, sizeof(nmo_scene_object_desc_t),
        s->object_descs.count, &s->object_descs.allocator);
    if (result != NMO_OK) goto fail;
    nmo_scene_object_descs_set_lifecycle(&copied.object_descs);
    nmo_scene_object_desc_t *dst_descs = NULL;
    result = nmo_array_extend(
        &copied.object_descs, s->object_descs.count, (void **)&dst_descs);
    if (result != NMO_OK) goto fail;
    const nmo_scene_object_desc_t *src_descs = NMO_ARRAY_DATA(
        nmo_scene_object_desc_t, &s->object_descs);
    for (size_t i = 0; i < s->object_descs.count; ++i) {
        dst_descs[i].ref = src_descs[i].ref;
        dst_descs[i].flags = src_descs[i].flags;
        result = nmo_object_copy_chunk(
            arena, &dst_descs[i].initial_value, src_descs[i].initial_value);
        if (result != NMO_OK) goto fail;
        result = nmo_object_copy_chunk(
            arena, &dst_descs[i].reserved, src_descs[i].reserved);
        if (result != NMO_OK) goto fail;
    }

#define NMO_SCENE_DETACH_SHARED_ARRAY(field) \
    do { \
        if (d->field.data == s->field.data) { \
            memset(&d->field, 0, sizeof(d->field)); \
        } \
    } while (0)
    NMO_SCENE_DETACH_SHARED_ARRAY(base.scripts);
    NMO_SCENE_DETACH_SHARED_ARRAY(base.attributes);
    NMO_SCENE_DETACH_SHARED_ARRAY(base.legacy_attributes);
    NMO_SCENE_DETACH_SHARED_ARRAY(object_descs);
#undef NMO_SCENE_DETACH_SHARED_ARRAY
    nmo_scene_destroy(d, NULL, NULL);
    *d = copied;
    return NMO_OK;

fail:
    nmo_scene_destroy(&copied, NULL, NULL);
    return result;
}

static nmo_status_t nmo_scene_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_scene_state_t *s = instance;
    if (s == NULL) return NMO_ERR_INVALID_ARGUMENT;
    NMO_RETURN_IF_ERROR(nmo_beobject_vtable.validate(
        &s->base, NULL, context));
    NMO_VALIDATE_COUNT(s->object_descs.data, s->object_descs.count, "object_descs");
    if (s->object_descs.element_size != sizeof(nmo_scene_object_desc_t) ||
        s->object_descs.count > INT32_MAX / 2u) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_RETURN_OK();
}

nmo_status_t nmo_scene_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_scene_remap_dependencies");
    }

    nmo_scene_state_t *state = (nmo_scene_state_t *)instance;
    (void)context;

    if (state->object_descs.count > 0 && state->object_descs.data == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Scene object_descs missing");
    }

    /* Preserve unresolved descriptors and scalar references. */
    return nmo_scene_validate(state, NULL, NULL);
}

nmo_status_t nmo_scene_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_scene_validate(instance, type, context);
}

static nmo_status_t nmo_scene_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_scene_pre_delete");
    }

    nmo_scene_state_t *state = (nmo_scene_state_t *)instance;
    state->level = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->background_texture = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->starting_camera = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->object_descs.count = 0;
    NMO_RETURN_OK();
}

static void nmo_scene_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

/* ============================================================================
 * Reference enumeration
 * ============================================================================ */

/**
 * @brief Enumerate all object references from CKScene state
 *
 * The default field-walk enumerator cannot recurse into struct arrays,
 * so scene member object IDs inside object_descs are missed. This custom
 * enumerator reports them explicitly.
 */
static nmo_status_t nmo_scene_enumerate_refs(
    const void *instance,
    const nmo_type_descriptor_t *type,
    nmo_type_ref_visitor_fn visitor,
    void *user_data)
{
    (void)type;
    const nmo_scene_state_t *s = (const nmo_scene_state_t *)instance;
    if (!s || !visitor) {
        NMO_RETURN_OK();
    }
    NMO_RETURN_IF_ERROR(nmo_scene_validate(s, NULL, NULL));

    /* Parent level. */
    const nmo_object_id_t level_id = nmo_ref_runtime_id(&s->level);
    if (level_id != NMO_OBJECT_ID_NONE) {
        if (!visitor(user_data, level_id, NMO_REF_KIND_SCENE,
                     "level", 0)) {
            NMO_RETURN_OK();
        }
    }

    /* object_descs array -> each object_id as NMO_REF_KIND_SCENE */
    if (s->object_descs.count > 0 && s->object_descs.data != NULL) {
        const nmo_scene_object_desc_t *descs =
            NMO_ARRAY_DATA(nmo_scene_object_desc_t, &s->object_descs);
        for (uint32_t i = 0; i < s->object_descs.count; ++i) {
            const nmo_object_id_t id = nmo_ref_runtime_id(&descs[i].ref);
            if (id != NMO_OBJECT_ID_NONE) {
                if (!visitor(user_data, id,
                             NMO_REF_KIND_SCENE, "object_descs", i)) {
                    NMO_RETURN_OK();
                }
            }
        }
    }

    const nmo_object_id_t background_texture_id =
        nmo_ref_runtime_id(&s->background_texture);
    if (background_texture_id != NMO_OBJECT_ID_NONE) {
        if (!visitor(user_data, background_texture_id,
                     NMO_REF_KIND_TEXTURE, "background_texture", 0)) {
            NMO_RETURN_OK();
        }
    }

    const nmo_object_id_t starting_camera_id =
        nmo_ref_runtime_id(&s->starting_camera);
    if (starting_camera_id != NMO_OBJECT_ID_NONE) {
        if (!visitor(user_data, starting_camera_id,
                     NMO_REF_KIND_UNKNOWN, "starting_camera", 0)) {
            NMO_RETURN_OK();
        }
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

static const nmo_object_serialize_pass_t nmo_scene_compare_pass = {
    .class_id = NMO_CID_SCENE,
    .data_version = 8,
    .chunk_options = NMO_CHUNK_OPTION_FILE,
};

static bool nmo_scene_equals(const void *a, const void *b)
{
    return nmo_object_serialized_state_equals(
        a, b, nmo_scene_serialize, &nmo_scene_compare_pass, 1, 4096);
}

static uint32_t nmo_scene_hash(const void *instance)
{
    return nmo_object_serialized_state_hash(
        instance, nmo_scene_serialize, &nmo_scene_compare_pass, 1, 4096);
}

nmo_type_vtable_t nmo_scene_vtable = {
    .prepare_dependencies = nmo_scene_prepare_dependencies,
    .remap_dependencies = nmo_scene_remap_dependencies,
    .pre_delete = nmo_scene_pre_delete,
    .post_delete = nmo_scene_post_delete,
    NMO_OBJECT_VTABLE_EX(
        nmo_scene_create,
        nmo_scene_destroy,
        nmo_scene_serialize,
        nmo_scene_deserialize,
        nmo_scene_copy,
        nmo_scene_validate,
        nmo_scene_equals,
        nmo_scene_hash,
        nmo_scene_enumerate_refs)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_scene_type,
    CKPGUID_SCENE,
    "CKScene",
    NMO_CID_SCENE,
    CKPGUID_BEOBJECT,
    nmo_scene_state_t,
    &nmo_scene_vtable,
    nmo_scene_fields)





