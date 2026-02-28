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
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    scene,
    nmo_scene_state_t,
    do {
        nmo_status_t result = nmo_array_init(&state->object_descs, sizeof(nmo_scene_object_desc_t), 0, NULL);
        if (result != NMO_OK) return result;
    } while (0),
    ((void)0))

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_scene_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_scene_state_t, base),
                         sizeof(nmo_beobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_REF(nmo_scene_state_t, level_id),
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
    NMO_FIELD_REF(nmo_scene_state_t, background_texture_id),
    NMO_FIELD_REF(nmo_scene_state_t, starting_camera_id)
};

/* Scene object flags (CKEnums.h) */
#define CK_SCENEOBJECT_START_ACTIVATE   0x0001
#define CK_SCENEOBJECT_ACTIVE           0x0008
#define CK_SCENEOBJECT_START_DEACTIVATE 0x0010
#define CK_SCENEOBJECT_START_RESET      0x0040

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
nmo_status_t nmo_scene_deserialize(
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
        /* Read level ID */
        result = nmo_chunk_read_object_id(chunk, &out_state->level_id);
        if (result != NMO_OK) return result;

        /* Read object count */
        int32_t desc_count;
        result = nmo_chunk_read_int(chunk, &desc_count);
        if (result != NMO_OK) return result;
        if (desc_count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Scene object count is negative");
        }

        nmo_array_clear(&out_state->object_descs);
        if (desc_count > 0) {
            const uint32_t MAX_SCENE_OBJECTS = 100000;
            if ((uint32_t)desc_count > MAX_SCENE_OBJECTS) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Scene object count exceeds maximum");
            }

            result = nmo_array_reserve(&out_state->object_descs, (size_t)desc_count);
            if (result != NMO_OK) return result;

            nmo_scene_object_desc_t *descs = NULL;
            result = nmo_array_extend(&out_state->object_descs, (size_t)desc_count, (void **)&descs);
            if (result != NMO_OK) return result;

            /* Initialize descriptors */
            for (int32_t i = 0; i < desc_count; i++) {
                descs[i].object_id = 0;
                descs[i].initial_value = NULL;
                descs[i].flags = 0;
            }

            /* Read object ID sequence */
            size_t sequence_count = 0;
            result = nmo_chunk_read_object_sequence_start(chunk, &sequence_count);
            if (result != NMO_OK) return result;

            if (sequence_count != (size_t)desc_count) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Scene object sequence count mismatch");
            }

            if (sequence_count > MAX_SCENE_OBJECTS) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                                 "Scene object sequence count exceeds maximum");
            }

            for (size_t i = 0; i < (size_t)desc_count; i++) {
                result = nmo_chunk_read_object_sequence_item(chunk,
                    &descs[i].object_id);
                if (result != NMO_OK) {
                    out_state->object_descs.count = i;
                    break;
                }
            }

            /* Read sub-chunk sequence (initial values + reserved) */
            size_t sub_chunk_count;
            result = nmo_chunk_start_read_sub_chunk_sequence(chunk, &sub_chunk_count);
            if (result != NMO_OK) return result;
            if (sub_chunk_count != (size_t)desc_count * 2u) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Scene initial value sub-chunk count mismatch");
            }

            /* Read pairs of chunks: initial value + reserved (NULL) */
            for (int32_t i = 0; i < desc_count; i++) {
                /* Read initial value chunk */
                result = nmo_chunk_read_sub_chunk(chunk, &descs[i].initial_value);
                if (result != NMO_OK) return result;

                /* Read reserved chunk (always NULL, discard) */
                nmo_chunk_t *reserved_chunk = NULL;
                result = nmo_chunk_read_sub_chunk(chunk, &reserved_chunk);
                if (result != NMO_OK) return result;
                (void)reserved_chunk;
            }

            /* Read object flags */
            for (int32_t i = 0; i < desc_count; i++) {
                uint32_t flags;
                result = nmo_chunk_read_dword(chunk, &flags);
                if (result != NMO_OK) {
                    break;
                }
                if (data_version >= 8) {
                    descs[i].flags = flags;
                } else {
                    uint32_t converted = flags & CK_SCENEOBJECT_ACTIVE;
                    if (flags & 2) {
                        converted |= CK_SCENEOBJECT_START_RESET;
                    }
                    if (flags & 1) {
                        converted |= CK_SCENEOBJECT_START_ACTIVATE;
                    } else {
                        converted |= CK_SCENEOBJECT_START_DEACTIVATE;
                    }
                    descs[i].flags = converted;
                }
            }
        }
    }

    /* Section 2: SCENELAUNCHED - Environment settings */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCENELAUNCHED);
    if (result == NMO_OK) {
        result = nmo_chunk_read_dword(chunk, &out_state->environment_settings);
        if (result != NMO_OK) {
            out_state->environment_settings = 0;
        }
    }

    /* Section 3: SCENERENDERSETTINGS - Rendering configuration */
    result = nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SCENERENDERSETTINGS);
    if (result == NMO_OK) {
        /* Background and ambient */
        result = nmo_chunk_read_dword(chunk, &out_state->background_color);
        if (result != NMO_OK) return result;

        result = nmo_chunk_read_dword(chunk, &out_state->ambient_light_color);
        if (result != NMO_OK) return result;

        /* Fog settings */
        result = nmo_chunk_read_dword(chunk, &out_state->fog_mode);
        if (result != NMO_OK) return result;

        result = nmo_chunk_read_dword(chunk, &out_state->fog_color);
        if (result != NMO_OK) return result;

        result = nmo_chunk_read_float(chunk, &out_state->fog_start);
        if (result != NMO_OK) return result;

        result = nmo_chunk_read_float(chunk, &out_state->fog_end);
        if (result != NMO_OK) return result;

        result = nmo_chunk_read_float(chunk, &out_state->fog_density);
        if (result != NMO_OK) return result;

        /* Scene references */
        result = nmo_chunk_read_object_id(chunk, &out_state->background_texture_id);
        if (result != NMO_OK) return result;

        result = nmo_chunk_read_object_id(chunk, &out_state->starting_camera_id);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
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
nmo_status_t nmo_scene_serialize(
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

    /* Write base class (CKBeObject) data */
    nmo_status_t result = nmo_beobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    /* Section 1: SCENENEWDATA */
    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SCENENEWDATA);
    if (result != NMO_OK) return result;

    /* Write level ID */
    result = nmo_chunk_write_object_id(out_chunk, in_state->level_id);
    if (result != NMO_OK) return result;

    /* Write object count */
    result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->object_descs.count);
    if (result != NMO_OK) return result;

    if (in_state->object_descs.count > 0) {
        /* Write object ID sequence */
        result = nmo_chunk_write_object_sequence_start(out_chunk, (uint32_t)in_state->object_descs.count);
        if (result != NMO_OK) return result;

        const nmo_scene_object_desc_t *descs = NMO_ARRAY_DATA(nmo_scene_object_desc_t,
                                                              &in_state->object_descs);
        for (uint32_t i = 0; i < in_state->object_descs.count; i++) {
            result = nmo_chunk_write_object_sequence_item(out_chunk,
                descs[i].object_id);
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

            /* Write reserved NULL chunk */
            result = nmo_chunk_write_sub_chunk_sequence(out_chunk, NULL);
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
    result = nmo_chunk_write_object_id(out_chunk, in_state->background_texture_id);
    if (result != NMO_OK) return result;

    result = nmo_chunk_write_object_id(out_chunk, in_state->starting_camera_id);
    if (result != NMO_OK) return result;

    NMO_RETURN_OK();
}

static nmo_status_t nmo_scene_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_scene_state_t *s = src;
    nmo_scene_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(arena, (void **)&d->base.base.raw_tail,
                                              s->base.base.raw_tail, s->base.base.raw_tail_size));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.script_ids, &d->base.script_ids,
                                        &s->base.script_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.attribute_parameter_ids,
                                        &d->base.attribute_parameter_ids,
                                        &s->base.attribute_parameter_ids.allocator));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.attribute_types,
                                        &d->base.attribute_types,
                                        &s->base.attribute_types.allocator));
    NMO_RETURN_IF_ERROR(nmo_object_clone_chunk_array(arena, &d->base.attribute_chunks,
                                                     &s->base.attribute_chunks));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->base.legacy_attributes_raw,
                                        &d->base.legacy_attributes_raw,
                                        &s->base.legacy_attributes_raw.allocator));

    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->object_descs, &d->object_descs,
                                        &s->object_descs.allocator));
    if (s->object_descs.count > 0) {
        const nmo_scene_object_desc_t *src_descs = NMO_ARRAY_DATA(nmo_scene_object_desc_t,
                                                                  &s->object_descs);
        nmo_scene_object_desc_t *dst_descs = NMO_ARRAY_DATA(nmo_scene_object_desc_t,
                                                            &d->object_descs);
        for (uint32_t i = 0; i < s->object_descs.count; ++i) {
            nmo_chunk_t *clone = NULL;
            NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &clone, src_descs[i].initial_value));
            dst_descs[i].initial_value = clone;
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_scene_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_scene_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->object_descs.data, s->object_descs.count, "object_descs");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    scene,
    nmo_scene_state_t,
    nmo_scene_serialize,
    nmo_scene_deserialize,
    nmo_scene_fields,
    CKPGUID_SCENE,
    "CKScene",
    NMO_CID_SCENE,
    CKPGUID_BEOBJECT
)


