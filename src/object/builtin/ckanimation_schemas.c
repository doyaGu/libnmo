/**
 * @file ckanimation_schemas.c
 * @brief CKAnimation, CKKeyedAnimation, CKObjectAnimation schema implementation
 */

#include "object/builtin/nmo_animation_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_param_guids.h"
#include "object/nmo_object_enum_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "core/nmo_arena_array.h"
#include "core/nmo_utils.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_struct_guids.h"
#include <string.h>
#include <stddef.h>

static nmo_status_t nmo_animation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);
static nmo_status_t nmo_keyedanimation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);
static nmo_status_t nmo_objectanimation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static void nmo_animation_set_defaults(void *instance)
{
    nmo_animation_state_t *state = instance;
    state->flags = CKANIMATION_LINKTOFRAMERATE | CKANIMATION_CANBEBREAK;
    state->frame_rate = 30.0f;
    state->length = 100.0f;
    state->current_step = 0.0f;
}

static const nmo_object_state_member_t nmo_animation_members[] = {
    NMO_STATE_VALUE(nmo_animation_state_t, has_data),
    NMO_STATE_VALUE(nmo_animation_state_t, data_is_legacy),
    NMO_STATE_VALUE(nmo_animation_state_t, flags),
    NMO_STATE_VALUE(nmo_animation_state_t, frame_rate),
    NMO_STATE_VALUE(nmo_animation_state_t, has_length),
    NMO_STATE_VALUE(nmo_animation_state_t, length),
    NMO_STATE_VALUE(nmo_animation_state_t, has_root_entity),
    NMO_STATE_VALUE(nmo_animation_state_t, legacy_body_part_count),
    NMO_STATE_COUNTED(nmo_animation_state_t, legacy_body_parts,
                      legacy_body_part_count, nmo_ref_t),
    NMO_STATE_VALUE(nmo_animation_state_t, root_entity),
    NMO_STATE_VALUE(nmo_animation_state_t, has_character),
    NMO_STATE_VALUE(nmo_animation_state_t, character),
    NMO_STATE_VALUE(nmo_animation_state_t, has_current_step),
    NMO_STATE_VALUE(nmo_animation_state_t, current_step)
};

static const nmo_object_state_layout_t nmo_animation_layout = {
    .size = sizeof(nmo_animation_state_t),
    .base_vtable = &nmo_sceneobject_vtable,
    .base_size = sizeof(nmo_sceneobject_state_t),
    .members = nmo_animation_members,
    .member_count = sizeof(nmo_animation_members) /
        sizeof(nmo_animation_members[0]),
    .set_defaults = nmo_animation_set_defaults,
    .validate = nmo_animation_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(animation, nmo_animation_layout)

static void nmo_keyedanimation_set_defaults(void *instance)
{
    nmo_keyedanimation_state_t *state = instance;
    state->merge_factor = 0.5f;
}

static const nmo_object_state_member_t nmo_keyedanimation_subanim_members[] = {
    NMO_STATE_VALUE(nmo_keyedanimation_subanim_t, ref),
    NMO_STATE_CHUNK(nmo_keyedanimation_subanim_t, chunk)
};

static const nmo_object_state_layout_t nmo_keyedanimation_subanim_layout = {
    .size = sizeof(nmo_keyedanimation_subanim_t),
    .members = nmo_keyedanimation_subanim_members,
    .member_count = sizeof(nmo_keyedanimation_subanim_members) /
        sizeof(nmo_keyedanimation_subanim_members[0]),
};

static const nmo_object_state_member_t nmo_keyedanimation_members[] = {
    NMO_STATE_VALUE(nmo_keyedanimation_state_t, animation_count),
    NMO_STATE_COUNTED(nmo_keyedanimation_state_t, animation_ids,
                      animation_count, nmo_ref_t),
    NMO_STATE_VALUE(nmo_keyedanimation_state_t, has_merge),
    NMO_STATE_VALUE(nmo_keyedanimation_state_t, merged),
    NMO_STATE_VALUE(nmo_keyedanimation_state_t, merge_factor),
    NMO_STATE_VALUE(nmo_keyedanimation_state_t, subanim_count),
    NMO_STATE_COUNTED_RECORDS(nmo_keyedanimation_state_t, subanims,
                              subanim_count, nmo_keyedanimation_subanim_layout)
};

static const nmo_object_state_layout_t nmo_keyedanimation_layout = {
    .size = sizeof(nmo_keyedanimation_state_t),
    .base_vtable = &nmo_animation_vtable,
    .base_size = sizeof(nmo_animation_state_t),
    .members = nmo_keyedanimation_members,
    .member_count = sizeof(nmo_keyedanimation_members) /
        sizeof(nmo_keyedanimation_members[0]),
    .set_defaults = nmo_keyedanimation_set_defaults,
    .validate = nmo_keyedanimation_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(keyedanimation, nmo_keyedanimation_layout)

/* The morph sections hold a count, the size of each buffer and the buffers.
 * The buffers follow the sizes next to them, so they are members of their own
 * that read the sizes from the state. */
static nmo_status_t nmo_objectanimation_morph_data_copy(
    nmo_arena_t *arena, void *dst, void *const *data,
    const uint32_t *sizes, uint32_t count)
{
    void **copy = NULL;
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(
        arena, (void **)&copy, data, sizeof(void *), count));
    for (uint32_t i = 0; i < count; ++i) {
        copy[i] = NULL;
        NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(
            arena, &copy[i], data[i], sizes[i]));
    }
    memcpy(dst, &copy, sizeof(copy));
    return NMO_OK;
}

static bool nmo_objectanimation_morph_data_equal(
    uint32_t count, const uint32_t *sizes, void *const *lhs, void *const *rhs)
{
    if (count == 0) return true;
    if (sizes == NULL || lhs == NULL || rhs == NULL) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (sizes[i] == 0) continue;
        if (lhs[i] == NULL || rhs[i] == NULL ||
            memcmp(lhs[i], rhs[i], sizes[i]) != 0) {
            return false;
        }
    }
    return true;
}

static uint32_t nmo_objectanimation_morph_data_hash(
    uint32_t hash, uint32_t count, const uint32_t *sizes, void *const *data)
{
    for (uint32_t i = 0; sizes != NULL && data != NULL && i < count; ++i) {
        if (data[i] != NULL && sizes[i] > 0) {
            hash = nmo_hash_fnv1a32_update(hash, data[i], sizes[i]);
        }
    }
    return hash;
}

static nmo_status_t nmo_objectanimation_comp_copy(
    nmo_arena_t *arena, void *dst, const void *src, const void *src_owner)
{
    const nmo_objectanimation_state_t *owner = src_owner;
    (void)src;
    return nmo_objectanimation_morph_data_copy(
        arena, dst, owner->morph_comp_data, owner->morph_comp_sizes,
        owner->morph_comp_count);
}

static bool nmo_objectanimation_comp_equal(
    const void *a, const void *b, const void *a_owner, const void *b_owner)
{
    const nmo_objectanimation_state_t *lhs = a_owner;
    const nmo_objectanimation_state_t *rhs = b_owner;
    (void)a;
    (void)b;
    return nmo_objectanimation_morph_data_equal(
        lhs->morph_comp_count, lhs->morph_comp_sizes,
        lhs->morph_comp_data, rhs->morph_comp_data);
}

static uint32_t nmo_objectanimation_comp_hash(
    uint32_t hash, const void *value, const void *owner)
{
    const nmo_objectanimation_state_t *state = owner;
    (void)value;
    return nmo_objectanimation_morph_data_hash(
        hash, state->morph_comp_count, state->morph_comp_sizes,
        state->morph_comp_data);
}

static const nmo_object_state_custom_ops_t nmo_objectanimation_comp_ops = {
    .copy = nmo_objectanimation_comp_copy,
    .equals = nmo_objectanimation_comp_equal,
    .hash = nmo_objectanimation_comp_hash,
};

static nmo_status_t nmo_objectanimation_normals_copy(
    nmo_arena_t *arena, void *dst, const void *src, const void *src_owner)
{
    const nmo_objectanimation_state_t *owner = src_owner;
    (void)src;
    return nmo_objectanimation_morph_data_copy(
        arena, dst, owner->morph_normals_data, owner->morph_normals_sizes,
        owner->morph_normals_count);
}

static bool nmo_objectanimation_normals_equal(
    const void *a, const void *b, const void *a_owner, const void *b_owner)
{
    const nmo_objectanimation_state_t *lhs = a_owner;
    const nmo_objectanimation_state_t *rhs = b_owner;
    (void)a;
    (void)b;
    return nmo_objectanimation_morph_data_equal(
        lhs->morph_normals_count, lhs->morph_normals_sizes,
        lhs->morph_normals_data, rhs->morph_normals_data);
}

static uint32_t nmo_objectanimation_normals_hash(
    uint32_t hash, const void *value, const void *owner)
{
    const nmo_objectanimation_state_t *state = owner;
    (void)value;
    return nmo_objectanimation_morph_data_hash(
        hash, state->morph_normals_count, state->morph_normals_sizes,
        state->morph_normals_data);
}

static const nmo_object_state_custom_ops_t nmo_objectanimation_normals_ops = {
    .copy = nmo_objectanimation_normals_copy,
    .equals = nmo_objectanimation_normals_equal,
    .hash = nmo_objectanimation_normals_hash,
};

static void nmo_objectanimation_set_defaults(void *instance)
{
    nmo_objectanimation_state_t *state = instance;
    state->format = CKOBJANIM_FORMAT_NONE;
    state->merge_factor = 0.5f;
    /* Fresh keyframe data of RCKObjectAnimation is 100 frames long. */
    state->length = 100.0f;
}

static const nmo_object_state_member_t nmo_objanim_controller_members[] = {
    NMO_STATE_VALUE(nmo_objanim_controller_t, type),
    NMO_STATE_VALUE(nmo_objanim_controller_t, key_count),
    NMO_STATE_VALUE(nmo_objanim_controller_t, data_size),
    NMO_STATE_COUNTED(nmo_objanim_controller_t, data, data_size, uint8_t)
};

static const nmo_object_state_layout_t nmo_objanim_controller_layout = {
    .size = sizeof(nmo_objanim_controller_t),
    .members = nmo_objanim_controller_members,
    .member_count = sizeof(nmo_objanim_controller_members) /
        sizeof(nmo_objanim_controller_members[0]),
};

static const nmo_object_state_member_t nmo_objanim_morph_key_members[] = {
    NMO_STATE_VALUE(nmo_objanim_morph_key_t, time_step),
    NMO_STATE_VALUE(nmo_objanim_morph_key_t, data_size),
    NMO_STATE_COUNTED(nmo_objanim_morph_key_t, data, data_size, uint8_t)
};

static const nmo_object_state_layout_t nmo_objanim_morph_key_layout = {
    .size = sizeof(nmo_objanim_morph_key_t),
    .members = nmo_objanim_morph_key_members,
    .member_count = sizeof(nmo_objanim_morph_key_members) /
        sizeof(nmo_objanim_morph_key_members[0]),
};

static const nmo_object_state_member_t nmo_objectanimation_members[] = {
    NMO_STATE_VALUE(nmo_objectanimation_state_t, format),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, root_pos),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, has_root_pos),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, root_extra),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, flags),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, entity),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, has_length),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, length),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, has_merge),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, merge_factor),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, anim1),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, anim2),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, has_shared_anim),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, shared_anim),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, has_morph_counts),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, morph_vertex_count),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, morph_key_count),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, controller_count),
    NMO_STATE_COUNTED_RECORDS(nmo_objectanimation_state_t, controllers,
                              controller_count, nmo_objanim_controller_layout),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, has_legacy_position_section),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, has_legacy_rotation_section),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, has_legacy_scale_section),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, has_legacy_flags_section),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, has_legacy_entity_section),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, morph_key_parsed_count),
    NMO_STATE_COUNTED_RECORDS(nmo_objectanimation_state_t, morph_keys,
                              morph_key_parsed_count, nmo_objanim_morph_key_layout),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, morph_comp_count),
    NMO_STATE_COUNTED(nmo_objectanimation_state_t, morph_comp_sizes,
                      morph_comp_count, uint32_t),
    NMO_STATE_CUSTOM(nmo_objectanimation_state_t, morph_comp_data,
                     nmo_objectanimation_comp_ops),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, morph_normals_count),
    NMO_STATE_COUNTED(nmo_objectanimation_state_t, morph_normals_sizes,
                      morph_normals_count, uint32_t),
    NMO_STATE_CUSTOM(nmo_objectanimation_state_t, morph_normals_data,
                     nmo_objectanimation_normals_ops),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, has_legacy_morphkeys),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, legacy_morphkeys_size),
    NMO_STATE_BYTES(nmo_objectanimation_state_t, legacy_morphkeys,
                    legacy_morphkeys_size),
    NMO_STATE_VALUE(nmo_objectanimation_state_t, raw_tail_size),
    NMO_STATE_BYTES(nmo_objectanimation_state_t, raw_tail, raw_tail_size)
};

static const nmo_object_state_layout_t nmo_objectanimation_layout = {
    .size = sizeof(nmo_objectanimation_state_t),
    .base_vtable = &nmo_sceneobject_vtable,
    .base_size = sizeof(nmo_sceneobject_state_t),
    .members = nmo_objectanimation_members,
    .member_count = sizeof(nmo_objectanimation_members) /
        sizeof(nmo_objectanimation_members[0]),
    .set_defaults = nmo_objectanimation_set_defaults,
    .validate = nmo_objectanimation_validate,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(objectanimation, nmo_objectanimation_layout)

/* CKAnimation flag bits (subset used during legacy load) */
#define CKANIMATION_LINKTOFRAMERATE       0x00000001u
#define CKANIMATION_CANBEBREAK            0x00000004u
#define CKANIMATION_ALIGNORIENTATION      0x00000010u

/* Animation controller type constants */
#define CKANIMATION_LINPOS_CONTROL      0x637c4301u
#define CKANIMATION_LINROT_CONTROL      0x49ed4002u
#define CKANIMATION_LINSCL_CONTROL      0x654a3a04u
#define CKANIMATION_LINSCLAXIS_CONTROL  0x2f200b08u
#define CKANIMATION_TCBPOS_CONTROL      0x347e4a01u
#define CKANIMATION_TCBROT_CONTROL      0x45b52a02u
#define CKANIMATION_TCBSCL_CONTROL      0x1b545904u
#define CKANIMATION_TCBSCLAXIS_CONTROL  0x32595908u
#define CKANIMATION_BEZIERPOS_CONTROL   0x921ab801u
#define CKANIMATION_BEZIERSCL_CONTROL   0x18ab4404u

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_animation_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_animation_state_t, base),
                    sizeof(nmo_sceneobject_state_t), CKPGUID_SCENEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_animation_state_t, has_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_animation_state_t, data_is_legacy, CKPGUID_UINT8),
    NMO_FIELD(nmo_animation_state_t, flags, CKPGUID_UINT32),
    NMO_FIELD(nmo_animation_state_t, frame_rate, CKPGUID_FLOAT),
    NMO_FIELD(nmo_animation_state_t, has_length, CKPGUID_UINT8),
    NMO_FIELD(nmo_animation_state_t, length, CKPGUID_FLOAT),
    NMO_FIELD(nmo_animation_state_t, has_root_entity, CKPGUID_UINT8),
    NMO_FIELD(nmo_animation_state_t, legacy_body_part_count, CKPGUID_UINT32),
    NMO_FIELD_REF_RECORD_ARRAY_COUNTED(
        nmo_animation_state_t, legacy_body_parts,
        legacy_body_part_count),
    NMO_FIELD_REF_VALUE(nmo_animation_state_t, root_entity),
    NMO_FIELD(nmo_animation_state_t, has_character, CKPGUID_UINT8),
    NMO_FIELD_REF_VALUE(nmo_animation_state_t, character),
    NMO_FIELD(nmo_animation_state_t, has_current_step, CKPGUID_UINT8),
    NMO_FIELD(nmo_animation_state_t, current_step, CKPGUID_FLOAT)
};

static const nmo_type_field_t nmo_keyedanimation_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_keyedanimation_state_t, base),
                    sizeof(nmo_animation_state_t), CKPGUID_ANIMATION,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_keyedanimation_state_t, animation_count, CKPGUID_UINT32),
    NMO_FIELD_REF_RECORD_ARRAY_COUNTED(nmo_keyedanimation_state_t, animation_ids, animation_count),
    NMO_FIELD(nmo_keyedanimation_state_t, has_merge, CKPGUID_UINT8),
    NMO_FIELD(nmo_keyedanimation_state_t, merged, CKPGUID_INT),
    NMO_FIELD(nmo_keyedanimation_state_t, merge_factor, CKPGUID_FLOAT),
    NMO_FIELD(nmo_keyedanimation_state_t, subanim_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_keyedanimation_state_t, subanims, subanim_count, 1, NMO_GUID_STRUCT_CKKEYEDANIMATIONSUBANIM)
};

static const nmo_type_field_t nmo_objectanimation_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_objectanimation_state_t, base),
                    sizeof(nmo_sceneobject_state_t), CKPGUID_SCENEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_objectanimation_state_t, format, NMO_GUID_ENUM_CK_OBJECTANIMATION_FORMAT),
    NMO_FIELD(nmo_objectanimation_state_t, root_pos, CKPGUID_VECTOR),
    NMO_FIELD(nmo_objectanimation_state_t, has_root_pos, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, root_extra, CKPGUID_VECTOR4),
    NMO_FIELD(nmo_objectanimation_state_t, flags, CKPGUID_UINT32),
    NMO_FIELD_REF_VALUE(nmo_objectanimation_state_t, entity),
    NMO_FIELD(nmo_objectanimation_state_t, has_length, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, length, CKPGUID_FLOAT),
    NMO_FIELD(nmo_objectanimation_state_t, has_merge, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, merge_factor, CKPGUID_FLOAT),
    NMO_FIELD_REF_VALUE(nmo_objectanimation_state_t, anim1),
    NMO_FIELD_REF_VALUE(nmo_objectanimation_state_t, anim2),
    NMO_FIELD(nmo_objectanimation_state_t, has_shared_anim, CKPGUID_UINT8),
    NMO_FIELD_REF_VALUE(nmo_objectanimation_state_t, shared_anim),
    NMO_FIELD(nmo_objectanimation_state_t, has_morph_counts, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, morph_vertex_count, CKPGUID_INT),
    NMO_FIELD(nmo_objectanimation_state_t, morph_key_count, CKPGUID_INT),
    NMO_FIELD(nmo_objectanimation_state_t, controller_count, CKPGUID_UINT32),
    NMO_FIELD_OPT(nmo_objectanimation_state_t, controllers, CKPGUID_POINTER),
    NMO_FIELD(nmo_objectanimation_state_t, has_legacy_position_section, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, has_legacy_rotation_section, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, has_legacy_scale_section, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, has_legacy_flags_section, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, has_legacy_entity_section, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, morph_key_parsed_count, CKPGUID_UINT32),
    NMO_FIELD_OPT(nmo_objectanimation_state_t, morph_keys, CKPGUID_POINTER),
    NMO_FIELD(nmo_objectanimation_state_t, morph_comp_count, CKPGUID_UINT32),
    NMO_FIELD_OPT(nmo_objectanimation_state_t, morph_comp_sizes, CKPGUID_POINTER),
    NMO_FIELD_OPT(nmo_objectanimation_state_t, morph_comp_data, CKPGUID_POINTER),
    NMO_FIELD(nmo_objectanimation_state_t, morph_normals_count, CKPGUID_UINT32),
    NMO_FIELD_OPT(nmo_objectanimation_state_t, morph_normals_sizes, CKPGUID_POINTER),
    NMO_FIELD_OPT(nmo_objectanimation_state_t, morph_normals_data, CKPGUID_POINTER),
    NMO_FIELD(nmo_objectanimation_state_t, has_legacy_morphkeys, CKPGUID_UINT8),
    NMO_FIELD_ARRAY_COUNTED_FLAGS(nmo_objectanimation_state_t, legacy_morphkeys,
                                  legacy_morphkeys_size, 1, CKPGUID_UINT8,
                                  NMO_FIELD_OPTIONAL, 0),
    NMO_FIELD(nmo_objectanimation_state_t, legacy_morphkeys_size, CKPGUID_UINT64),
    NMO_FIELD_ARRAY_COUNTED_FLAGS(nmo_objectanimation_state_t, raw_tail, raw_tail_size, 1,
                                  CKPGUID_UINT8, NMO_FIELD_OPTIONAL, 0),
    NMO_FIELD(nmo_objectanimation_state_t, raw_tail_size, CKPGUID_UINT64)
};

/* =============================================================================
 * IDENTIFIER HELPERS
 * ============================================================================= */

/* Chunks older than CHUNK_VERSION1 hold an object array as a leading non-zero
 * dword, 4 dwords that are skipped, a count and then that many plain object
 * ids (XSObjectPointerArray::Load 0x2402b715, CKStateChunk::ReadXObjectArray
 * 0x24022ac0). The ids are not file indices, so they are kept as raw ids. */
static nmo_status_t read_ref_array_before_chunk_version1(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ref_t **out_refs,
    uint32_t *out_count,
    size_t trailing_dwords)
{
    *out_refs = NULL;
    *out_count = 0u;
    uint32_t lead = 0u;
    NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &lead));
    if (lead == 0u) NMO_RETURN_OK();

    const size_t header_dwords = 5u;
    if (nmo_chunk_identifier_remaining_dwords(chunk) < header_dwords) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Old animation reference array header is truncated");
    }
    NMO_RETURN_IF_ERROR(nmo_chunk_skip(chunk, 4u));
    int32_t signed_count = 0;
    NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &signed_count));
    /* The engine loops only while the count is positive. */
    if (signed_count <= 0) NMO_RETURN_OK();

    const size_t count = (size_t)signed_count;
    const size_t remaining_dwords =
        nmo_chunk_identifier_remaining_dwords(chunk);
    if (trailing_dwords > remaining_dwords ||
        count > remaining_dwords - trailing_dwords) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Animation reference count exceeds identifier payload");
    }
    nmo_ref_t *refs = (nmo_ref_t *)nmo_arena_alloc(
        arena, count * sizeof(nmo_ref_t), _Alignof(nmo_ref_t));
    if (!refs) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate animation references");
    }
    for (size_t i = 0; i < count; ++i) {
        uint32_t id = 0u;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &id));
        refs[i] = nmo_ref_from_raw((nmo_object_id_t)id);
    }
    *out_refs = refs;
    *out_count = (uint32_t)count;
    NMO_RETURN_OK();
}

static nmo_status_t read_ref_array(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ref_t **out_refs,
    uint32_t *out_count,
    size_t trailing_dwords)
{
    if (nmo_chunk_get_chunk_version(chunk) < NMO_CHUNK_VERSION1) {
        return read_ref_array_before_chunk_version1(
            chunk, arena, out_refs, out_count, trailing_dwords);
    }
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(
        chunk, &count);
    if (result != NMO_OK) return result;
    if (count > UINT32_MAX || count > SIZE_MAX / sizeof(nmo_ref_t)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Animation reference count exceeds limits");
    }
    const size_t remaining_dwords =
        nmo_chunk_identifier_remaining_dwords(chunk);
    if (trailing_dwords > remaining_dwords ||
        count > remaining_dwords - trailing_dwords) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Animation reference count exceeds identifier payload");
    }
    nmo_ref_t *refs = NULL;
    if (count > 0) {
        refs = (nmo_ref_t *)nmo_arena_alloc(
            arena, count * sizeof(nmo_ref_t), _Alignof(nmo_ref_t));
        if (!refs) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate animation references");
        }
        for (size_t i = 0; i < count; ++i) {
            result = nmo_ref_read(chunk, &refs[i]);
            if (result != NMO_OK) return result;
        }
    }
    *out_refs = refs;
    *out_count = (uint32_t)count;
    NMO_RETURN_OK();
}

static void nmo_objectanimation_check_refs(
    nmo_objectanimation_state_t *state,
    void *context)
{
    const nmo_object_repository_t *repository =
        (const nmo_object_repository_t *)
            nmo_deserialize_context_get_repository(context);
    const nmo_type_registry_t *types =
        nmo_deserialize_context_get_type_registry(context);
    nmo_ref_check_class(&state->entity, repository, types, NMO_CID_3DENTITY);
    nmo_ref_check_class(&state->anim1, repository, types, NMO_CID_OBJECTANIMATION);
    nmo_ref_check_class(&state->anim2, repository, types, NMO_CID_OBJECTANIMATION);
    nmo_ref_check_class(
        &state->shared_anim, repository, types, NMO_CID_OBJECTANIMATION);
}

static nmo_status_t nmo_animation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_animation_state_t *state = instance;
    NMO_VALIDATE_COUNT(
        state->legacy_body_parts, state->legacy_body_part_count,
        "legacy_body_parts");
    if (state->legacy_body_part_count > (uint32_t)INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (state->data_is_legacy &&
        (state->flags & ~(CKANIMATION_LINKTOFRAMERATE |
                          CKANIMATION_CANBEBREAK)) != 0u) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    return nmo_sceneobject_vtable.validate(&state->base, NULL, context);
}

static nmo_status_t nmo_keyedanimation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_keyedanimation_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->animation_ids, s->animation_count, "animation_ids");
    NMO_VALIDATE_COUNT(s->subanims, s->subanim_count, "subanims");
    return nmo_animation_vtable.validate(&s->base, NULL, context);
}

static nmo_status_t nmo_keyedanimation_enumerate_refs(
    const void *instance,
    const nmo_type_descriptor_t *type,
    nmo_type_ref_visitor_fn visitor,
    void *user_data)
{
    (void)type;
    const nmo_keyedanimation_state_t *state = instance;
    if (!state || !visitor) return NMO_OK;
    NMO_RETURN_IF_ERROR(nmo_keyedanimation_validate(state, NULL, NULL));
    for (uint32_t i = 0; i < state->animation_count; ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(
            &state->animation_ids[i]);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, 0, "animation_ids", i)) {
            return NMO_OK;
        }
    }
    for (uint32_t i = 0; i < state->subanim_count; ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(
            &state->subanims[i].ref);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, 0, "subanims.ref", i)) {
            return NMO_OK;
        }
    }
    return NMO_OK;
}

static nmo_status_t nmo_objectanimation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_objectanimation_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->controllers, s->controller_count, "controllers");
    NMO_VALIDATE_COUNT(s->morph_keys, s->morph_key_parsed_count, "morph_keys");
    NMO_VALIDATE_COUNT(s->morph_comp_sizes, s->morph_comp_count, "morph_comp_sizes");
    NMO_VALIDATE_COUNT(s->morph_comp_data, s->morph_comp_count, "morph_comp_data");
    NMO_VALIDATE_COUNT(s->morph_normals_sizes, s->morph_normals_count, "morph_normals_sizes");
    NMO_VALIDATE_COUNT(s->morph_normals_data, s->morph_normals_count, "morph_normals_data");
    NMO_VALIDATE_BYTES(
        s->legacy_morphkeys, s->legacy_morphkeys_size,
        "legacy morph keys");
    NMO_VALIDATE_BYTES(s->raw_tail, s->raw_tail_size, "raw_tail");
    if ((!s->has_legacy_morphkeys && s->legacy_morphkeys_size != 0u) ||
        (s->legacy_morphkeys_size & 3u) != 0u ||
        (s->format != CKOBJANIM_FORMAT_LEGACY &&
         (s->has_legacy_morphkeys ||
          s->has_legacy_position_section ||
          s->has_legacy_rotation_section ||
          s->has_legacy_scale_section ||
          s->has_legacy_flags_section ||
          s->has_legacy_entity_section))) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    size_t allocation_size = 0;
    if (!nmo_safe_mul_size(
            s->controller_count, sizeof(nmo_objanim_controller_t),
            &allocation_size) ||
        !nmo_safe_mul_size(
            s->morph_key_parsed_count,
            sizeof(nmo_objanim_morph_key_t), &allocation_size) ||
        !nmo_safe_mul_size(
            s->morph_comp_count, sizeof(uint32_t), &allocation_size) ||
        !nmo_safe_mul_size(
            s->morph_comp_count, sizeof(void *), &allocation_size) ||
        !nmo_safe_mul_size(
            s->morph_normals_count, sizeof(uint32_t), &allocation_size) ||
        !nmo_safe_mul_size(
            s->morph_normals_count, sizeof(void *), &allocation_size)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if (s->format == CKOBJANIM_FORMAT_NEWDATA) {
        if (s->morph_vertex_count < 0 || s->morph_key_count < 0 ||
            (uint32_t)s->morph_key_count != s->morph_key_parsed_count) {
            return NMO_ERR_VALIDATION_FAILED;
        }
        if ((s->morph_comp_count != 0u &&
             s->morph_comp_count != s->morph_key_parsed_count) ||
            (s->morph_normals_count != 0u &&
             s->morph_normals_count != s->morph_key_parsed_count)) {
            return NMO_ERR_VALIDATION_FAILED;
        }
    }
    if (s->format == CKOBJANIM_FORMAT_LEGACY &&
        (s->morph_key_count < 0 || s->morph_vertex_count < 0 ||
         (uint32_t)s->morph_key_count != s->morph_key_parsed_count ||
         s->morph_comp_count != 0u || s->morph_normals_count != 0u)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    uint32_t controller_slots = 0u;
    for (uint32_t i = 0; i < s->controller_count; ++i) {
        NMO_VALIDATE_BYTES(
            s->controllers[i].data,
            s->controllers[i].data_size,
            "controller data");
        if (s->format == CKOBJANIM_FORMAT_CONTROLLERS &&
            (s->controllers[i].type == 0u ||
             (s->controllers[i].data_size & 3u) != 0u)) {
            return NMO_ERR_VALIDATION_FAILED;
        }
        if (s->format == CKOBJANIM_FORMAT_CONTROLLERS &&
            s->controllers[i].key_count > 0u) {
            size_t keys_size = 0u;
            if (!nmo_objanim_controller_keys_size(
                    s->controllers[i].type, s->format, s->controllers[i].data,
                    s->controllers[i].data_size, s->controllers[i].key_count,
                    &keys_size) ||
                keys_size != s->controllers[i].data_size) {
                return NMO_ERR_VALIDATION_FAILED;
            }
        }
        if (s->format == CKOBJANIM_FORMAT_NEWDATA ||
            s->format == CKOBJANIM_FORMAT_LEGACY) {
            uint32_t slot = 0u;
            switch (s->controllers[i].type) {
            case CKANIMATION_LINPOS_CONTROL: slot = 1u << 0; break;
            case CKANIMATION_LINSCL_CONTROL: slot = 1u << 1; break;
            case CKANIMATION_LINROT_CONTROL: slot = 1u << 2; break;
            case CKANIMATION_LINSCLAXIS_CONTROL: slot = 1u << 3; break;
            default: return NMO_ERR_VALIDATION_FAILED;
            }
            if ((controller_slots & slot) != 0u ||
                s->controllers[i].key_count == 0u ||
                s->controllers[i].data_size == 0u) {
                return NMO_ERR_VALIDATION_FAILED;
            }
            controller_slots |= slot;
        }
    }
    for (uint32_t i = 0; i < s->morph_key_parsed_count; ++i) {
        NMO_VALIDATE_BYTES(
            s->morph_keys[i].data,
            s->morph_keys[i].data_size,
            "morph key data");
    }
    for (uint32_t i = 0; i < s->morph_comp_count; ++i) {
        NMO_VALIDATE_BYTES(
            s->morph_comp_data[i],
            s->morph_comp_sizes[i],
            "morph compressed data");
    }
    for (uint32_t i = 0; i < s->morph_normals_count; ++i) {
        NMO_VALIDATE_BYTES(
            s->morph_normals_data[i],
            s->morph_normals_sizes[i],
            "morph normal data");
    }
    return nmo_sceneobject_vtable.validate(&s->base, NULL, context);
}

NMO_DEFINE_OBJECT_PREPARE_VIA_VALIDATE(nmo_animation)

nmo_status_t nmo_animation_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_animation_remap_dependencies");
    }

    nmo_animation_state_t *state = (nmo_animation_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_sceneobject_remap_dependencies(&state->base, NULL, context));

    return nmo_animation_validate(state, NULL, NULL);
}

NMO_DEFINE_OBJECT_PREPARE_VIA_VALIDATE(nmo_keyedanimation)

nmo_status_t nmo_keyedanimation_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_keyedanimation_remap_dependencies");
    }

    nmo_keyedanimation_state_t *state = (nmo_keyedanimation_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_animation_remap_dependencies(&state->base, NULL, context));

    if (state->animation_count > 0 && state->animation_ids == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "KeyedAnimation animation_ids missing");
    }
    if (state->subanim_count > 0 && state->subanims == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "KeyedAnimation subanims missing");
    }

    /* Keep unresolved, duplicate, and null entries in their original lanes.
     * Explicit normalization owns any destructive repair. */
    return nmo_keyedanimation_validate(state, NULL, NULL);
}

NMO_DEFINE_OBJECT_PREPARE_VIA_VALIDATE(nmo_objectanimation)

nmo_status_t nmo_objectanimation_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_objectanimation_remap_dependencies");
    }

    nmo_objectanimation_state_t *state = (nmo_objectanimation_state_t *)instance;

    NMO_RETURN_IF_ERROR(nmo_sceneobject_remap_dependencies(&state->base, NULL, context));

    /* Preserve serialized values during dependency resolution. */
    return nmo_objectanimation_validate(state, NULL, NULL);
}

static nmo_status_t nmo_animation_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_animation_pre_delete");
    }
    nmo_animation_state_t *state = instance;
    state->legacy_body_part_count = 0;
    NMO_RETURN_OK();
}

/* An animation that shares the keys of a deleted one keeps them: the engine
 * counts the references to the keyframe data, so the sharers of a deleted owner
 * own the keys afterwards. Only an owner in the CONTROLLERS format is
 * materialised; its controllers and length move into each sharer. */
static nmo_status_t nmo_objectanimation_adopt_shared_keys(
    const nmo_objectanimation_state_t *owner_state,
    nmo_object_repository_t *repository)
{
    if (owner_state->format != CKOBJANIM_FORMAT_CONTROLLERS) {
        NMO_RETURN_OK();
    }
    const size_t count = nmo_object_repository_get_count(repository);
    nmo_object_id_t owner_id = NMO_OBJECT_ID_NONE;
    for (size_t i = 0; i < count; ++i) {
        const nmo_object_t *candidate =
            nmo_object_repository_get_by_index(repository, i);
        if (candidate != NULL && candidate->state == owner_state) {
            owner_id = candidate->id;
            break;
        }
    }
    if (owner_id == NMO_OBJECT_ID_NONE) {
        NMO_RETURN_OK();
    }

    for (size_t i = 0; i < count; ++i) {
        nmo_object_t *sharer =
            nmo_object_repository_get_by_index(repository, i);
        if (sharer == NULL || sharer->class_id != NMO_CID_OBJECTANIMATION ||
            sharer->state == NULL || sharer->state == owner_state) {
            continue;
        }
        nmo_objectanimation_state_t *state = sharer->state;
        if (state->format != CKOBJANIM_FORMAT_SHARED ||
            nmo_ref_runtime_id(&state->shared_anim) != owner_id ||
            sharer->storage_arena == NULL) {
            continue;
        }

        nmo_objanim_controller_t *controllers = NULL;
        if (owner_state->controller_count > 0u) {
            controllers = nmo_arena_alloc(
                sharer->storage_arena,
                sizeof(*controllers) * owner_state->controller_count,
                _Alignof(nmo_objanim_controller_t));
            if (controllers == NULL) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate adopted controllers");
            }
            for (uint32_t c = 0; c < owner_state->controller_count; ++c) {
                controllers[c] = owner_state->controllers[c];
                controllers[c].data = NULL;
                NMO_RETURN_IF_ERROR(nmo_object_copy_bytes(
                    sharer->storage_arena, &controllers[c].data,
                    owner_state->controllers[c].data,
                    owner_state->controllers[c].data_size));
            }
        }
        state->format = CKOBJANIM_FORMAT_CONTROLLERS;
        state->controllers = controllers;
        state->controller_count = owner_state->controller_count;
        state->has_length = 1;
        state->length = owner_state->has_length ? owner_state->length : 100.0f;
        state->has_shared_anim = 0;
        state->shared_anim = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_objectanimation_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_objectanimation_pre_delete");
    }
    if (context != NULL) {
        return nmo_objectanimation_adopt_shared_keys(
            (const nmo_objectanimation_state_t *)instance,
            (nmo_object_repository_t *)context);
    }
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

nmo_type_vtable_t nmo_animation_vtable = {
    .prepare_dependencies = nmo_animation_prepare_dependencies,
    .remap_dependencies = nmo_animation_remap_dependencies,
    .pre_delete = nmo_animation_pre_delete,
    .post_delete = nmo_object_post_delete_noop,
    NMO_OBJECT_VTABLE(
        nmo_animation_create,
        nmo_animation_destroy,
        nmo_animation_serialize,
        nmo_animation_deserialize,
        nmo_animation_copy,
        nmo_animation_validate,
        nmo_animation_equals,
        nmo_animation_hash)
};

nmo_type_vtable_t nmo_keyedanimation_vtable = {
    .prepare_dependencies = nmo_keyedanimation_prepare_dependencies,
    .remap_dependencies = nmo_keyedanimation_remap_dependencies,
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
    NMO_OBJECT_VTABLE_EX(
        nmo_keyedanimation_create,
        nmo_keyedanimation_destroy,
        nmo_keyedanimation_serialize,
        nmo_keyedanimation_deserialize,
        nmo_keyedanimation_copy,
        nmo_keyedanimation_validate,
        nmo_keyedanimation_equals,
        nmo_keyedanimation_hash,
        nmo_keyedanimation_enumerate_refs)
};

nmo_type_vtable_t nmo_objectanimation_vtable = {
    .prepare_dependencies = nmo_objectanimation_prepare_dependencies,
    .remap_dependencies = nmo_objectanimation_remap_dependencies,
    .pre_delete = nmo_objectanimation_pre_delete,
    .post_delete = nmo_object_post_delete_noop,
    NMO_OBJECT_VTABLE(
        nmo_objectanimation_create,
        nmo_objectanimation_destroy,
        nmo_objectanimation_serialize,
        nmo_objectanimation_deserialize,
        nmo_objectanimation_copy,
        nmo_objectanimation_validate,
        nmo_objectanimation_equals,
        nmo_objectanimation_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_animation_type,
    CKPGUID_ANIMATION,
    "CKAnimation",
    NMO_CID_ANIMATION,
    CKPGUID_SCENEOBJECT,
    nmo_animation_state_t,
    &nmo_animation_vtable,
    nmo_animation_fields)

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_keyedanimation_type,
    CKPGUID_KEYEDANIMATION,
    "CKKeyedAnimation",
    NMO_CID_KEYEDANIMATION,
    CKPGUID_ANIMATION,
    nmo_keyedanimation_state_t,
    &nmo_keyedanimation_vtable,
    nmo_keyedanimation_fields)

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_objectanimation_type,
    CKPGUID_OBJECTANIMATION,
    "CKObjectAnimation",
    NMO_CID_OBJECTANIMATION,
    CKPGUID_SCENEOBJECT,
    nmo_objectanimation_state_t,
    &nmo_objectanimation_vtable,
    nmo_objectanimation_fields)

static nmo_status_t write_ref_array(
    nmo_chunk_t *chunk,
    const nmo_ref_t *refs,
    uint32_t count)
{
    return nmo_ref_write_sequence(chunk, refs, count);
}

uint32_t nmo_objanim_controller_key_size(uint32_t type)
{
    switch (type) {
    case CKANIMATION_LINPOS_CONTROL:      return 16;
    case CKANIMATION_LINROT_CONTROL:      return 20;
    case CKANIMATION_LINSCL_CONTROL:      return 16;
    case CKANIMATION_LINSCLAXIS_CONTROL:  return 20;
    case CKANIMATION_TCBPOS_CONTROL:      return 36;
    case CKANIMATION_TCBROT_CONTROL:      return 40;
    case CKANIMATION_TCBSCL_CONTROL:      return 36;
    case CKANIMATION_TCBSCLAXIS_CONTROL:  return 40;
    default: return 0;
    }
}

uint32_t nmo_objanim_controller_format_key_size(
    uint32_t type,
    nmo_objectanimation_format_t format)
{
    /*
     * NEWDATA and LEGACY files store a scale-axis key as 24 bytes: the time, an
     * unused float, then the quaternion. The engine drops the unused float when
     * it loads them into its 20-byte key, so CONTROLLERS files (which the engine
     * saves) use the 20-byte layout.
     */
    if (type == CKANIMATION_LINSCLAXIS_CONTROL &&
        (format == CKOBJANIM_FORMAT_NEWDATA ||
         format == CKOBJANIM_FORMAT_LEGACY)) {
        return 24;
    }
    return nmo_objanim_controller_key_size(type);
}

/*
 * Bezier keys are packed on disk: a 20-byte base (time, xyz and a dword with
 * two 16-bit flag words) followed by a 12-byte tangent for each flag word that
 * has bit 0x20 set. The engine's 44-byte key is its in-memory layout only.
 */
enum {
    OBJANIM_BEZIER_BASE_KEY_SIZE = 20,
    OBJANIM_BEZIER_TANGENT_SIZE = 12,
    OBJANIM_BEZIER_FLAGS_OFFSET = 16,
    OBJANIM_BEZIER_TANGENT_FLAG = 0x20,
};

bool nmo_objanim_controller_is_bezier(uint32_t type)
{
    return type == CKANIMATION_BEZIERPOS_CONTROL ||
           type == CKANIMATION_BEZIERSCL_CONTROL;
}

static uint64_t objanim_morph_key_size(const nmo_objanim_morph_info_t *info)
{
    return sizeof(float) + (uint64_t)info->vertex_count * 12u +
           (info->has_normals ? (uint64_t)info->vertex_count * 4u : 0u);
}

bool nmo_objanim_morph_controller_info(
    const nmo_objanim_controller_t *controller,
    nmo_objanim_morph_info_t *out_info)
{
    if (controller == NULL || out_info == NULL ||
        controller->type != NMO_OBJANIM_CONTROLLER_MORPH ||
        controller->data == NULL || controller->data_size < 12u) {
        return false;
    }
    uint32_t header[3];
    memcpy(header, controller->data, sizeof(header));

    nmo_objanim_morph_info_t info = {
        .key_count = header[0],
        .vertex_count = header[1],
        .has_normals = header[2] != 0u && header[0] != 0u,
    };
    if (info.key_count != 0u) {
        uint64_t key_size = objanim_morph_key_size(&info);  /* below 2^36 */
        if (key_size > (UINT64_MAX - 12u) / info.key_count ||
            12u + (uint64_t)info.key_count * key_size != controller->data_size) {
            return false;
        }
    } else if (controller->data_size != 12u) {
        return false;
    }
    *out_info = info;
    return true;
}

bool nmo_objanim_morph_controller_key(
    const nmo_objanim_controller_t *controller,
    const nmo_objanim_morph_info_t *info,
    uint32_t index,
    float *out_time,
    const float **out_positions,
    const uint8_t **out_normals)
{
    if (controller == NULL || info == NULL || index >= info->key_count) {
        return false;
    }
    const uint8_t *key = (const uint8_t *)controller->data + 12u +
                         (size_t)((uint64_t)index * objanim_morph_key_size(info));
    if (out_time != NULL) memcpy(out_time, key, sizeof(float));
    if (out_positions != NULL) *out_positions = (const float *)(key + 4u);
    if (out_normals != NULL) {
        *out_normals = info->has_normals
            ? key + 4u + (size_t)info->vertex_count * 12u : NULL;
    }
    return true;
}

size_t nmo_objanim_bezier_key_decode(
    const void *key,
    size_t available,
    nmo_objanim_bezier_key_t *out)
{
    if (key == NULL || available < OBJANIM_BEZIER_BASE_KEY_SIZE) {
        return 0u;
    }
    const uint8_t *bytes = (const uint8_t *)key;
    nmo_objanim_bezier_key_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    memcpy(&decoded.time, bytes, sizeof(float));
    memcpy(decoded.position, bytes + sizeof(float), sizeof(decoded.position));
    memcpy(&decoded.flags, bytes + OBJANIM_BEZIER_FLAGS_OFFSET,
           sizeof(decoded.flags));

    size_t size = OBJANIM_BEZIER_BASE_KEY_SIZE;
    for (size_t i = 0; i < 2u; ++i) {
        const uint32_t word = (decoded.flags >> (16u * i)) & 0xFFFFu;
        if ((word & OBJANIM_BEZIER_TANGENT_FLAG) == 0u) {
            continue;
        }
        if (available - size < OBJANIM_BEZIER_TANGENT_SIZE) {
            return 0u;
        }
        decoded.has_tangent[i] = true;
        memcpy(decoded.tangent[i], bytes + size, OBJANIM_BEZIER_TANGENT_SIZE);
        size += OBJANIM_BEZIER_TANGENT_SIZE;
    }
    if (out != NULL) {
        *out = decoded;
    }
    return size;
}

bool nmo_objanim_controller_keys_size(
    uint32_t type,
    nmo_objectanimation_format_t format,
    const void *keys,
    size_t available,
    uint32_t key_count,
    size_t *out_size)
{
    if (out_size == NULL) {
        return false;
    }

    if (nmo_objanim_controller_is_bezier(type)) {
        if (key_count > 0u && keys == NULL) {
            return false;
        }
        size_t total = 0u;
        for (uint32_t i = 0; i < key_count; ++i) {
            size_t key_size = nmo_objanim_bezier_key_decode(
                (const uint8_t *)keys + total, available - total, NULL);
            if (key_size == 0u) {
                return false;
            }
            total += key_size;
        }
        *out_size = total;
        return true;
    }

    uint32_t key_size = nmo_objanim_controller_format_key_size(type, format);
    size_t total = 0u;
    if (key_size == 0u ||
        !nmo_safe_mul_size(key_count, key_size, &total) ||
        total > available) {
        return false;
    }
    *out_size = total;
    return true;
}

/*
 * A CONTROLLERS-format blob is [u32 key_count][keys]. Returns true when `blob`
 * is exactly that for a known controller type with at least one key; anything
 * else (unknown types, empty controllers, trailing bytes) stays a raw blob.
 */
static bool objanim_controller_split_blob(
    uint32_t type,
    const void *blob,
    uint32_t blob_size,
    uint32_t *out_key_count,
    uint32_t *out_keys_size)
{
    if (blob == NULL || blob_size <= sizeof(uint32_t)) {
        return false;
    }
    uint32_t key_count = 0u;
    memcpy(&key_count, blob, sizeof(key_count));
    if (key_count == 0u) {
        return false;
    }

    const size_t keys_available = blob_size - sizeof(uint32_t);
    size_t keys_size = 0u;
    if (!nmo_objanim_controller_keys_size(
            type, CKOBJANIM_FORMAT_CONTROLLERS,
            (const uint8_t *)blob + sizeof(uint32_t), keys_available,
            key_count, &keys_size) ||
        keys_size != keys_available) {
        return false;
    }
    *out_key_count = key_count;
    *out_keys_size = (uint32_t)keys_size;
    return true;
}

/* A chunk without a keyframe section keeps the sections it holds as a chain of
 * identifier sections, {id, next, payload...} with next relative to the start
 * of the buffer. The base object sections the state already holds are left
 * out, so writing them back after those cannot repeat them or lose a section
 * that sits before them. */
static bool unread_section_at(
    const uint8_t *bytes,
    size_t dwords,
    size_t index,
    uint32_t *out_id,
    size_t *out_end,
    bool *out_last)
{
    if (dwords - index < 2u) return false;
    uint32_t next = 0u;
    memcpy(out_id, bytes + index * sizeof(uint32_t), sizeof(*out_id));
    memcpy(&next, bytes + (index + 1u) * sizeof(uint32_t), sizeof(next));
    *out_last = next == 0u;
    if (*out_last) {
        *out_end = dwords;
        return true;
    }
    if (next < index + 2u || next > dwords - 2u) return false;
    *out_end = next;
    return true;
}

static bool unread_sections_are_chain(const uint8_t *bytes, size_t dwords)
{
    size_t index = 0u;
    bool last = false;
    while (!last) {
        uint32_t id = 0u;
        size_t end = 0u;
        if (!unread_section_at(bytes, dwords, index, &id, &end, &last)) {
            return false;
        }
        index = end;
    }
    return true;
}

static nmo_status_t read_unread_sections(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    const nmo_objectanimation_state_t *state,
    void **out_data,
    size_t *out_size)
{
    const uint8_t *bytes = (const uint8_t *)chunk->data.data;
    const size_t dwords = chunk->data.count;
    *out_data = NULL;
    *out_size = 0u;
    if (dwords == 0u) NMO_RETURN_OK();
    if (!unread_sections_are_chain(bytes, dwords)) {
        return NMO_ERR_NOT_FOUND;
    }

    uint32_t base_id = 0u;
    if ((state->base.base.visibility_flags & NMO_CKOBJECT_HIERARCHICAL) != 0u) {
        base_id = CK_STATESAVE_OBJECTHIERAHIDDEN;
    } else if ((state->base.base.visibility_flags & NMO_CKOBJECT_VISIBLE) == 0u) {
        base_id = CK_STATESAVE_OBJECTHIDDEN;
    }

    uint8_t *tail = nmo_arena_alloc(arena, dwords * sizeof(uint32_t), 1);
    if (!tail) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate raw tail buffer");
    }
    size_t used = 0u;
    size_t previous = (size_t)-1;
    size_t index = 0u;
    bool last = false;
    while (!last) {
        uint32_t id = 0u;
        size_t end = 0u;
        (void)unread_section_at(bytes, dwords, index, &id, &end, &last);
        if (base_id != 0u && id == base_id) {
            base_id = 0u;
        } else {
            const size_t section = end - index;
            memcpy(tail + used * sizeof(uint32_t),
                   bytes + index * sizeof(uint32_t), section * sizeof(uint32_t));
            if (previous != (size_t)-1) {
                const uint32_t link = (uint32_t)used;
                memcpy(tail + (previous + 1u) * sizeof(uint32_t), &link, sizeof(link));
            }
            const uint32_t none = 0u;
            memcpy(tail + (used + 1u) * sizeof(uint32_t), &none, sizeof(none));
            previous = used;
            used += section;
        }
        index = end;
    }
    if (used > 0u) {
        *out_data = tail;
        *out_size = used * sizeof(uint32_t);
    }
    NMO_RETURN_OK();
}

static nmo_status_t read_raw_tail(nmo_chunk_t *chunk, nmo_arena_t *arena,
                                  size_t remaining_dwords,
                                  void **out_data, size_t *out_size)
{
    if (remaining_dwords == 0u) {
        *out_data = NULL;
        *out_size = 0;
        NMO_RETURN_OK();
    }
    if (remaining_dwords > SIZE_MAX / sizeof(uint32_t) ||
        !nmo_chunk_has_read_capacity(chunk, remaining_dwords)) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }

    size_t remaining_bytes = remaining_dwords * sizeof(uint32_t);
    void *data = nmo_arena_alloc(arena, remaining_bytes, 1);
    if (!data) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate raw tail buffer");
    }

    NMO_RETURN_IF_ERROR(
        nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, remaining_bytes));

    *out_data = data;
    *out_size = remaining_bytes;
    NMO_RETURN_OK();
}

/* Read controllers in CONTROLLERS format: loop of {type(DWORD), size_dwords(DWORD), data[]} until type==0 */
static nmo_status_t nmo_animation_seek_optional_sized(
    nmo_chunk_t *chunk,
    uint32_t identifier,
    bool *out_found,
    size_t *out_dwords)
{
    nmo_status_t result = nmo_chunk_seek_identifier_with_size(
        chunk, identifier, out_dwords);
    if (result == NMO_OK) {
        *out_found = true;
        return NMO_OK;
    }
    *out_found = false;
    *out_dwords = 0u;
    return result == NMO_ERR_NOT_FOUND ? NMO_OK : result;
}

static nmo_status_t nmo_animation_require_section_end(
    const nmo_chunk_t *chunk,
    size_t section_end)
{
    /* Load ignores what follows the fields it reads in a section. */
    return nmo_chunk_get_position(chunk) > section_end
        ? NMO_ERR_TRUNCATED_CHUNK
        : NMO_OK;
}

static nmo_status_t nmo_animation_validate_payload_size(
    nmo_chunk_t *chunk,
    uint32_t size_bytes,
    size_t trailing_dwords)
{
    const size_t required_dwords = ((size_t)size_bytes + 3u) / 4u;
    const size_t remaining_dwords =
        nmo_chunk_identifier_remaining_dwords(chunk);
    if (trailing_dwords > remaining_dwords ||
        required_dwords > remaining_dwords - trailing_dwords) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Animation payload exceeds identifier section");
    }
    return NMO_OK;
}

static nmo_status_t nmo_objectanimation_read_morph_section(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t count,
    uint32_t *out_count,
    uint32_t **out_sizes,
    void ***out_data)
{
    if ((size_t)count >
        nmo_chunk_identifier_remaining_dwords(chunk)) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Morph normal sizes exceed identifier payload");
    }

    size_t sizes_bytes = 0u;
    size_t pointers_bytes = 0u;
    if (!nmo_safe_mul_size(count, sizeof(uint32_t), &sizes_bytes) ||
        !nmo_safe_mul_size(count, sizeof(void *), &pointers_bytes)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Morph normal array size overflows");
    }

    uint32_t *sizes = nmo_arena_alloc(
        arena, sizes_bytes, _Alignof(uint32_t));
    void **data_ptrs = nmo_arena_alloc(
        arena, pointers_bytes, _Alignof(void *));
    if (!sizes || !data_ptrs) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate morph normal arrays");
    }

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t size_bytes = 0u;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &size_bytes));
        sizes[i] = size_bytes;
        NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
            chunk, size_bytes, (size_t)(count - i - 1u)));

        data_ptrs[i] = NULL;
        if (size_bytes > 0u) {
            void *data = nmo_arena_alloc(arena, size_bytes, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate morph normal data");
            }
            NMO_RETURN_IF_ERROR(
                nmo_chunk_read_and_fill_buffer_nosize_checked(
                    chunk, data, size_bytes));
            data_ptrs[i] = data;
        }
    }

    *out_count = count;
    *out_sizes = sizes;
    *out_data = data_ptrs;
    return NMO_OK;
}

static nmo_status_t read_controllers_loop(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_objectanimation_state_t *out_state)
{
    nmo_arena_array_t controllers;
    NMO_RETURN_IF_ERROR(nmo_arena_array_init(
        &controllers, sizeof(nmo_objanim_controller_t), 0u, arena));

    for (;;) {
        if (nmo_chunk_identifier_remaining_dwords(chunk) < 1u) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Controller sequence has no terminator");
        }
        uint32_t type = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &type));
        if (type == 0) {
            break;
        }

        if (nmo_chunk_identifier_remaining_dwords(chunk) < 1u) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Controller size is truncated");
        }
        uint32_t size_dwords = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &size_dwords));
        if (size_dwords > UINT32_MAX / 4u) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Controller byte size overflows");
        }
        if ((size_t)size_dwords >
            nmo_chunk_identifier_remaining_dwords(chunk)) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Controller payload exceeds identifier section");
        }
        uint32_t data_size = size_dwords * 4;

        void *data = NULL;
        if (data_size > 0) {
            data = nmo_arena_alloc(arena, data_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate controller data buffer");
            }
            NMO_RETURN_IF_ERROR(
                nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, data_size));
        }

        if (controllers.count >= UINT32_MAX) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Controller count exceeds state limits");
        }
        /* Known layouts drop the count prefix; other blobs stay raw. */
        nmo_objanim_controller_t controller = {
            .type = type,
            .key_count = 0u,
            .data_size = data_size,
            .data = data,
        };
        if (objanim_controller_split_blob(
                type, data, data_size,
                &controller.key_count, &controller.data_size)) {
            controller.data = (uint8_t *)data + sizeof(uint32_t);
        }
        NMO_RETURN_IF_ERROR(nmo_arena_array_append(
            &controllers, &controller));
    }

    if (controllers.count > 0u) {
        out_state->controllers = controllers.data;
        out_state->controller_count = (uint32_t)controllers.count;
    }

    NMO_RETURN_OK();
}

/* Read controllers in NEWDATA format: morph keys + 4 inline controllers + optional morph normals */
static nmo_status_t read_newdata_controllers(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_objectanimation_state_t *out_state,
    size_t data_section_end)
{
    nmo_objanim_controller_t local_controllers[8];
    uint32_t count = 0;
    bool section_found = false;
    size_t section_dwords = 0u;

    /* 1. Read morph keys if present */
    if (out_state->morph_key_count < 0 || out_state->morph_vertex_count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Invalid morph key counts");
    }
    if (out_state->morph_key_count > 0) {
        int32_t morph_key_count = out_state->morph_key_count;
        const size_t remaining_dwords =
            nmo_chunk_identifier_remaining_dwords(chunk);
        if ((size_t)morph_key_count >
                SIZE_MAX / sizeof(nmo_objanim_morph_key_t) ||
            remaining_dwords < 8u ||
            (size_t)morph_key_count > (remaining_dwords - 8u) / 2u) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Morph key count exceeds identifier payload");
        }
        nmo_objanim_morph_key_t *morph_keys = nmo_arena_alloc(
            arena, sizeof(nmo_objanim_morph_key_t) * (uint32_t)morph_key_count,
            _Alignof(nmo_objanim_morph_key_t));
        if (!morph_keys) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate morph keys array");
        }

        for (int32_t i = 0; i < morph_key_count; ++i) {
            float time_step = 0.0f;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &time_step));
            morph_keys[i].time_step = time_step;

            uint32_t size_bytes = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &size_bytes));
            morph_keys[i].data_size = size_bytes;
            const size_t trailing_dwords =
                (size_t)(morph_key_count - i - 1) * 2u + 8u;
            NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
                chunk, size_bytes, trailing_dwords));

            if (size_bytes > 0) {
                void *data = nmo_arena_alloc(arena, size_bytes, 4);
                if (!data) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                     "Failed to allocate morph key data");
                }
                NMO_RETURN_IF_ERROR(
                    nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, size_bytes));
                morph_keys[i].data = data;
            } else {
                morph_keys[i].data = NULL;
            }
        }

        out_state->morph_keys = morph_keys;
        out_state->morph_key_parsed_count = (uint32_t)morph_key_count;
    }

    /* 2. Read 4 controllers: position, scale, rotation, scaleAxis */
    static const uint32_t controller_types[4] = {
        CKANIMATION_LINPOS_CONTROL,
        CKANIMATION_LINSCL_CONTROL,
        CKANIMATION_LINROT_CONTROL,
        CKANIMATION_LINSCLAXIS_CONTROL
    };

    for (int i = 0; i < 4; ++i) {
        uint32_t buf_size = 0;
        uint32_t key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &buf_size));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &key_count));
        if ((key_count == 0u) != (buf_size == 0u)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Inconsistent NEWDATA controller payload");
        }
        NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
            chunk, buf_size, (size_t)(3 - i) * 2u));

        if (key_count > 0 && buf_size > 0) {
            void *data = nmo_arena_alloc(arena, buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate controller data");
            }
            NMO_RETURN_IF_ERROR(
                nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, buf_size));

            local_controllers[count].type = controller_types[i];
            local_controllers[count].key_count = key_count;
            local_controllers[count].data_size = buf_size;
            local_controllers[count].data = data;
            count++;
        }
    }

    /* 3. The per-key morph sections. RCKObjectAnimation::Load reads each one
     * independently and only when it has a morph controller, which it has
     * when there are morph keys; without keys both are ignored. */
    NMO_RETURN_IF_ERROR(nmo_animation_require_section_end(
        chunk, data_section_end));
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMMORPHCOMP, &section_found,
        &section_dwords));
    if (section_found && out_state->morph_key_parsed_count != 0u) {
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        NMO_RETURN_IF_ERROR(nmo_objectanimation_read_morph_section(
            chunk, arena, out_state->morph_key_parsed_count,
            &out_state->morph_comp_count, &out_state->morph_comp_sizes,
            &out_state->morph_comp_data));
        NMO_RETURN_IF_ERROR(nmo_animation_require_section_end(
            chunk, section_end));
    }
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMMORPHNORMALS, &section_found,
        &section_dwords));
    if (section_found && out_state->morph_key_parsed_count != 0u) {
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        NMO_RETURN_IF_ERROR(nmo_objectanimation_read_morph_section(
            chunk, arena, out_state->morph_key_parsed_count,
            &out_state->morph_normals_count, &out_state->morph_normals_sizes,
            &out_state->morph_normals_data));
        NMO_RETURN_IF_ERROR(nmo_animation_require_section_end(
            chunk, section_end));
    }

    /* Copy controllers to arena-allocated array */
    if (count > 0) {
        nmo_objanim_controller_t *controllers = nmo_arena_alloc(
            arena, sizeof(nmo_objanim_controller_t) * count,
            _Alignof(nmo_objanim_controller_t));
        if (!controllers) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate controllers array");
        }
        memcpy(controllers, local_controllers, sizeof(nmo_objanim_controller_t) * count);
        out_state->controllers = controllers;
        out_state->controller_count = count;
    }

    NMO_RETURN_OK();
}

/* Read controllers in LEGACY format: identifier-based sections */
static nmo_status_t read_legacy_controllers(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_objectanimation_state_t *out_state)
{
    nmo_objanim_controller_t local_controllers[8];
    uint32_t count = 0;
    bool section_found = false;
    size_t section_dwords = 0u;

    /* Preserve the old morphkeys payload without assigning unproven semantics. */
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMMORPHKEYS, &section_found,
        &section_dwords));
    if (section_found) {
        out_state->has_legacy_morphkeys = 1;
        NMO_RETURN_IF_ERROR(read_raw_tail(
            chunk, arena, section_dwords,
            (void **)&out_state->legacy_morphkeys,
            &out_state->legacy_morphkeys_size));
    }

    /* Read morph keys (legacy format) */
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMMORPHKEYS2, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        out_state->has_morph_counts = 1;
        int32_t morph_key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &morph_key_count));
        if (morph_key_count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid morph key count");
        }
        if (morph_key_count > 0) {
            if (section_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;
            int32_t morph_vertex_count = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &morph_vertex_count));
            if (morph_vertex_count < 0) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Invalid morph vertex count");
            }
            if ((size_t)morph_key_count >
                    SIZE_MAX / sizeof(nmo_objanim_morph_key_t) ||
                (size_t)morph_key_count >
                    nmo_chunk_identifier_remaining_dwords(chunk) / 2u) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                 "Morph key count exceeds identifier payload");
            }

            out_state->morph_key_count = morph_key_count;
            out_state->morph_vertex_count = morph_vertex_count;

            nmo_objanim_morph_key_t *morph_keys = nmo_arena_alloc(
                arena, sizeof(nmo_objanim_morph_key_t) * (uint32_t)morph_key_count,
                _Alignof(nmo_objanim_morph_key_t));
            if (!morph_keys) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate morph keys array");
            }

            for (int32_t i = 0; i < morph_key_count; ++i) {
                float time_step = 0.0f;
                NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &time_step));
                morph_keys[i].time_step = time_step;

                uint32_t size_bytes = 0;
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &size_bytes));
                morph_keys[i].data_size = size_bytes;
                NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
                    chunk, size_bytes,
                    (size_t)(morph_key_count - i - 1) * 2u));

                if (size_bytes > 0) {
                    void *data = nmo_arena_alloc(arena, size_bytes, 4);
                    if (!data) {
                        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                         "Failed to allocate morph key data");
                    }
                    NMO_RETURN_IF_ERROR(nmo_chunk_read_and_fill_buffer_nosize_checked(
                        chunk, data, size_bytes));
                    morph_keys[i].data = data;
                } else {
                    morph_keys[i].data = NULL;
                }
            }

            out_state->morph_keys = morph_keys;
            out_state->morph_key_parsed_count = (uint32_t)morph_key_count;
        }
        NMO_RETURN_IF_ERROR(nmo_animation_require_section_end(
            chunk, section_end));
    }

    /* Read position controller */
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMPOSKEYS, &section_found,
        &section_dwords));
    if (section_found) {
        out_state->has_legacy_position_section = 1;
        if (section_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        uint32_t buf_size = 0;
        uint32_t key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &buf_size));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &key_count));
        if ((key_count == 0u) != (buf_size == 0u)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Inconsistent legacy position controller payload");
        }
        NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
            chunk, buf_size, 0u));

        if (key_count > 0 && buf_size > 0) {
            void *data = nmo_arena_alloc(arena, buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate position controller data");
            }
            NMO_RETURN_IF_ERROR(
                nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, buf_size));

            local_controllers[count].type = CKANIMATION_LINPOS_CONTROL;
            local_controllers[count].key_count = key_count;
            local_controllers[count].data_size = buf_size;
            local_controllers[count].data = data;
            count++;
        }
        NMO_RETURN_IF_ERROR(nmo_animation_require_section_end(
            chunk, section_end));
    }

    /* Read rotation controller + scale axis controller */
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMROTKEYS, &section_found,
        &section_dwords));
    if (section_found) {
        out_state->has_legacy_rotation_section = 1;
        if (section_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        uint32_t rot_buf_size = 0;
        uint32_t rot_key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &rot_buf_size));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &rot_key_count));
        if ((rot_key_count == 0u) != (rot_buf_size == 0u)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Inconsistent legacy rotation controller payload");
        }
        NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
            chunk, rot_buf_size, 2u));

        if (rot_key_count > 0 && rot_buf_size > 0) {
            void *data = nmo_arena_alloc(arena, rot_buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate rotation controller data");
            }
            NMO_RETURN_IF_ERROR(
                nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, rot_buf_size));

            local_controllers[count].type = CKANIMATION_LINROT_CONTROL;
            local_controllers[count].key_count = rot_key_count;
            local_controllers[count].data_size = rot_buf_size;
            local_controllers[count].data = data;
            count++;
        }

        uint32_t axis_buf_size = 0;
        uint32_t axis_key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &axis_buf_size));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &axis_key_count));
        if ((axis_key_count == 0u) != (axis_buf_size == 0u)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Inconsistent legacy scale-axis controller payload");
        }
        NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
            chunk, axis_buf_size, 0u));

        if (axis_key_count > 0 && axis_buf_size > 0) {
            void *data = nmo_arena_alloc(arena, axis_buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate scale axis controller data");
            }
            NMO_RETURN_IF_ERROR(
                nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, axis_buf_size));

            local_controllers[count].type = CKANIMATION_LINSCLAXIS_CONTROL;
            local_controllers[count].key_count = axis_key_count;
            local_controllers[count].data_size = axis_buf_size;
            local_controllers[count].data = data;
            count++;
        }
        NMO_RETURN_IF_ERROR(nmo_animation_require_section_end(
            chunk, section_end));
    }

    /* Read scale controller */
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMSCLKEYS, &section_found,
        &section_dwords));
    if (section_found) {
        out_state->has_legacy_scale_section = 1;
        if (section_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        uint32_t buf_size = 0;
        uint32_t key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &buf_size));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &key_count));
        if ((key_count == 0u) != (buf_size == 0u)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Inconsistent legacy scale controller payload");
        }
        NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
            chunk, buf_size, 0u));

        if (key_count > 0 && buf_size > 0) {
            void *data = nmo_arena_alloc(arena, buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate scale controller data");
            }
            NMO_RETURN_IF_ERROR(
                nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, buf_size));

            local_controllers[count].type = CKANIMATION_LINSCL_CONTROL;
            local_controllers[count].key_count = key_count;
            local_controllers[count].data_size = buf_size;
            local_controllers[count].data = data;
            count++;
        }
        NMO_RETURN_IF_ERROR(nmo_animation_require_section_end(
            chunk, section_end));
    }

    /* Read legacy header fields */
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMFLAGS, &section_found,
        &section_dwords));
    if (section_found) {
        out_state->has_legacy_flags_section = 1;
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->flags));
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMENTITY, &section_found,
        &section_dwords));
    if (section_found) {
        out_state->has_legacy_entity_section = 1;
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &out_state->entity));
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMLENGTH, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_length = 1;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->length));
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMMERGE, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 4u) return NMO_ERR_TRUNCATED_CHUNK;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->merge_factor));
        int32_t merged = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &merged));
        out_state->has_merge = 1;
        if (merged) {
            out_state->flags |= 0x80u;
        } else {
            out_state->flags &= ~0x80u;
        }
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &out_state->anim1));
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &out_state->anim2));
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMNEWDATA, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 3u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_root_pos = 1;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_vector3(chunk, &out_state->root_pos));
    }

    /* Copy controllers to arena-allocated array */
    if (count > 0) {
        nmo_objanim_controller_t *controllers = nmo_arena_alloc(
            arena, sizeof(nmo_objanim_controller_t) * count,
            _Alignof(nmo_objanim_controller_t));
        if (!controllers) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate controllers array");
        }
        memcpy(controllers, local_controllers, sizeof(nmo_objanim_controller_t) * count);
        out_state->controllers = controllers;
        out_state->controller_count = count;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_animation_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_animation_state_t *out_state)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_animation_deserialize");
    }

    {
        nmo_status_t result = nmo_sceneobject_deserialize(&out_state->base, chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    out_state->has_data = 0;
    out_state->data_is_legacy = 0;
    out_state->flags = CKANIMATION_LINKTOFRAMERATE | CKANIMATION_CANBEBREAK;
    out_state->frame_rate = 30.0f;
    out_state->has_length = 0;
    out_state->length = 100.0f;
    out_state->has_root_entity = 0;
    out_state->legacy_body_part_count = 0;
    out_state->legacy_body_parts = NULL;
    out_state->root_entity = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->has_character = 0;
    out_state->character = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->has_current_step = 0;
    out_state->current_step = 0.0f;
    bool section_found = false;
    size_t section_dwords = 0u;

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_ANIMATIONDATA, &section_found,
        &section_dwords));
    /* RCKAnimation::Load takes a block of 8 or 12 bytes and ignores any other
       size, empty included. */
    if (section_found && (section_dwords == 2u || section_dwords == 3u)) {
        out_state->has_data = 1;

        const size_t remaining_dwords = section_dwords;
        if (remaining_dwords == 3u) {
            out_state->data_is_legacy = 1;
            int32_t can_interrupt = 0;
            int32_t linked_to_framerate = 0;
            float frame_rate = 0.0f;

            nmo_status_t result = nmo_chunk_read_int(chunk, &can_interrupt);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_int(chunk, &linked_to_framerate);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_float(chunk, &frame_rate);
            if (result != NMO_OK) return result;

            out_state->flags = 0;
            if (linked_to_framerate) {
                out_state->flags |= CKANIMATION_LINKTOFRAMERATE;
            }
            if (can_interrupt) {
                out_state->flags |= CKANIMATION_CANBEBREAK;
            }
            out_state->frame_rate = frame_rate;
        } else {
            nmo_status_t result = nmo_chunk_read_dword(chunk, &out_state->flags);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_float(chunk, &out_state->frame_rate);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_ANIMATIONLENGTH, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_length = 1;
        nmo_status_t result = nmo_chunk_read_float(chunk, &out_state->length);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_ANIMATIONBODYPARTS, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
        if (arena == NULL) arena = chunk->arena;
        nmo_ref_t *legacy_body_parts = NULL;
        uint32_t legacy_body_part_count = 0;
        nmo_status_t result = read_ref_array(
            chunk, arena, &legacy_body_parts,
            &legacy_body_part_count, 1u);
        if (result != NMO_OK) return result;
        const size_t remaining_dwords =
            nmo_chunk_identifier_remaining_dwords(chunk);
        if (remaining_dwords == 0) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Animation root entity is missing");
        }
        nmo_ref_t root_entity = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_read(chunk, &root_entity);
        if (result != NMO_OK) return result;
        result = nmo_animation_require_section_end(chunk, section_end);
        if (result != NMO_OK) return result;
        const nmo_object_repository_t *repository =
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context);
        const nmo_type_registry_t *types =
            nmo_deserialize_context_get_type_registry(context);
        for (uint32_t i = 0; i < legacy_body_part_count; ++i) {
            nmo_ref_check_class(
                &legacy_body_parts[i], repository, types,
                NMO_CID_BODYPART);
        }
        nmo_ref_check_class(
            &root_entity, repository, types,
            NMO_CID_3DENTITY);
        out_state->has_root_entity = 1;
        out_state->legacy_body_part_count = legacy_body_part_count;
        out_state->legacy_body_parts = legacy_body_parts;
        out_state->root_entity = root_entity;
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_ANIMATIONCHARACTER, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_character = 1;
        nmo_ref_t character = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        nmo_status_t result = nmo_ref_read(chunk, &character);
        if (result != NMO_OK) return result;
        nmo_ref_check_class(
            &character,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_CHARACTER);
        out_state->character = character;
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_ANIMATIONCURRENTSTEP, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_current_step = 1;
        nmo_status_t result = nmo_chunk_read_float(chunk, &out_state->current_step);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_animation_serialize_internal(
    const nmo_animation_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_animation_serialize");
    }
    NMO_RETURN_IF_ERROR(nmo_animation_validate(in_state, NULL, context));

    {
        nmo_status_t result = nmo_sceneobject_serialize(&in_state->base, out_chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    const bool is_file = nmo_object_serialize_is_file(out_chunk, context);
    if (!is_file && save_flags == 0) {
        NMO_RETURN_OK();
    }

    if (is_file || (save_flags & CK_STATESAVE_ANIMATIONDATA) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONDATA);
        if (result != NMO_OK) return result;
        if (in_state->data_is_legacy) {
            result = nmo_chunk_write_int(
                out_chunk,
                (in_state->flags & CKANIMATION_CANBEBREAK) != 0u);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(
                out_chunk,
                (in_state->flags & CKANIMATION_LINKTOFRAMERATE) != 0u);
            if (result != NMO_OK) return result;
        } else {
            result = nmo_chunk_write_dword(out_chunk, in_state->flags);
            if (result != NMO_OK) return result;
        }
        result = nmo_chunk_write_float(out_chunk, in_state->frame_rate);
        if (result != NMO_OK) return result;
    }

    if (is_file || (save_flags & CK_STATESAVE_ANIMATIONLENGTH) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONLENGTH);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->length);
        if (result != NMO_OK) return result;
    }

    if (is_file || (save_flags & CK_STATESAVE_ANIMATIONBODYPARTS) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONBODYPARTS);
        if (result != NMO_OK) return result;
        result = nmo_ref_write_sequence(
            out_chunk, in_state->legacy_body_parts,
            in_state->legacy_body_part_count);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->root_entity);
        if (result != NMO_OK) return result;
    }

    if (is_file || (save_flags & CK_STATESAVE_ANIMATIONCHARACTER) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONCHARACTER);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->character);
        if (result != NMO_OK) return result;
    }

    if (is_file || (save_flags & CK_STATESAVE_ANIMATIONCURRENTSTEP) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONCURRENTSTEP);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->current_step);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_keyedanimation_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_keyedanimation_state_t *out_state)
{
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_keyedanimation_deserialize");
    }

    NMO_RETURN_IF_ERROR(nmo_keyedanimation_create(out_state, NULL, context));

    nmo_status_t result = nmo_animation_deserialize_internal(chunk, context, &out_state->base);
    if (result != NMO_OK) return result;
    bool section_found = false;
    size_t section_dwords = 0u;

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_KEYEDANIMANIMLIST, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        nmo_ref_t *animation_ids = NULL;
        uint32_t animation_count = 0;
        result = read_ref_array(
            chunk, arena, &animation_ids, &animation_count, 0u);
        if (result != NMO_OK) return result;
        result = nmo_animation_require_section_end(chunk, section_end);
        if (result != NMO_OK) return result;
        const nmo_object_repository_t *repository =
            nmo_deserialize_context_get_repository(context);
        const nmo_type_registry_t *types =
            nmo_deserialize_context_get_type_registry(context);
        for (uint32_t i = 0; i < animation_count; ++i) {
            nmo_ref_check_class(
                &animation_ids[i], repository, types,
                NMO_CID_OBJECTANIMATION);
        }
        out_state->animation_ids = animation_ids;
        out_state->animation_count = animation_count;
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_KEYEDANIMMERGE, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;
        out_state->has_merge = 1;
        result = nmo_chunk_read_int(chunk, &out_state->merged);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_float(chunk, &out_state->merge_factor);
        if (result != NMO_OK) return result;
    }

    const bool is_file = nmo_object_deserialize_is_file(chunk, context);
    if (!is_file) {
        NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
            chunk, CK_STATESAVE_KEYEDANIMSUBANIMS, &section_found,
            &section_dwords));
    }
    if (!is_file && section_found) {
        if (section_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        uint32_t count = 0;
        result = nmo_chunk_read_dword(chunk, &count);
        if (result != NMO_OK) return result;
        const size_t remaining_dwords =
            nmo_chunk_identifier_remaining_dwords(chunk);
        if ((size_t)count > remaining_dwords / 2u) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Subanim count exceeds identifier payload");
        }
        size_t allocation_size = 0;
        if (!nmo_safe_mul_size(
                (size_t)count,
                sizeof(nmo_keyedanimation_subanim_t),
                &allocation_size)) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Subanim array size exceeds platform limits");
        }
        if (count > 0) {
            nmo_keyedanimation_subanim_t *subanims =
                (nmo_keyedanimation_subanim_t *)nmo_arena_alloc(
                arena, allocation_size,
                _Alignof(nmo_keyedanimation_subanim_t));
            if (!subanims) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate subanim array");
            }

            for (uint32_t i = 0; i < count; ++i) {
                subanims[i].ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
                subanims[i].chunk = NULL;
                result = nmo_ref_read(chunk, &subanims[i].ref);
                if (result != NMO_OK) return result;
                result = nmo_chunk_read_sub_chunk(chunk, &subanims[i].chunk);
                if (result != NMO_OK) return result;
                nmo_ref_check_class(
                    &subanims[i].ref,
                    (const nmo_object_repository_t *)
                        nmo_deserialize_context_get_repository(context),
                    nmo_deserialize_context_get_type_registry(context),
                    NMO_CID_OBJECTANIMATION);
            }
            out_state->subanims = subanims;
            out_state->subanim_count = count;
        }
        result = nmo_animation_require_section_end(chunk, section_end);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_keyedanimation_serialize_internal(
    const nmo_keyedanimation_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_keyedanimation_serialize");
    }
    NMO_RETURN_IF_ERROR(nmo_keyedanimation_validate(in_state, NULL, NULL));

    nmo_status_t result = nmo_animation_serialize_internal(&in_state->base, out_chunk, context);
    if (result != NMO_OK) return result;

    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    const bool is_file = nmo_object_serialize_is_file(out_chunk, context);

    if (is_file || (save_flags & CK_STATESAVE_KEYEDANIMANIMLIST) != 0) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KEYEDANIMANIMLIST);
        if (result != NMO_OK) return result;
        result = write_ref_array(out_chunk, in_state->animation_ids, in_state->animation_count);
        if (result != NMO_OK) return result;
    }

    if (is_file || (save_flags & CK_STATESAVE_KEYEDANIMMERGE) != 0) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KEYEDANIMMERGE);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->merged);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
        if (result != NMO_OK) return result;
    }

    if (!is_file && (save_flags & CK_STATESAVE_KEYEDANIMSUBANIMS) != 0) {
        const uint32_t count = in_state->subanim_count;
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KEYEDANIMSUBANIMS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, count);
        if (result != NMO_OK) return result;
        for (uint32_t i = 0; i < count; ++i) {
            result = nmo_ref_write(out_chunk, &in_state->subanims[i].ref);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_sub_chunk(
                out_chunk, in_state->subanims[i].chunk);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_objectanimation_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_objectanimation_state_t *out_state)
{
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_objectanimation_deserialize");
    }

    NMO_RETURN_IF_ERROR(nmo_objectanimation_create(out_state, NULL, context));

    {
        nmo_status_t result = nmo_sceneobject_deserialize(&out_state->base, chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    out_state->format = CKOBJANIM_FORMAT_NONE;
    out_state->has_root_pos = 0;
    out_state->root_pos.x = 0.0f;
    out_state->root_pos.y = 0.0f;
    out_state->root_pos.z = 0.0f;
    out_state->root_extra = (nmo_vector4_t){0.0f, 0.0f, 0.0f, 0.0f};
    out_state->flags = 0;
    out_state->entity = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->has_length = 0;
    /* A chunk without a keyframe section leaves the constructor's keyframe data
       (100 frames) in place; the legacy path below clears it first. */
    out_state->length = 100.0f;
    out_state->has_merge = 0;
    out_state->merge_factor = 0.5f;
    out_state->anim1 = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->anim2 = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->has_shared_anim = 0;
    out_state->shared_anim = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->has_morph_counts = 0;
    out_state->morph_vertex_count = 0;
    out_state->morph_key_count = 0;
    out_state->controller_count = 0;
    out_state->controllers = NULL;
    out_state->has_legacy_position_section = 0;
    out_state->has_legacy_rotation_section = 0;
    out_state->has_legacy_scale_section = 0;
    out_state->has_legacy_flags_section = 0;
    out_state->has_legacy_entity_section = 0;
    out_state->morph_key_parsed_count = 0;
    out_state->morph_keys = NULL;
    out_state->morph_comp_count = 0;
    out_state->morph_comp_sizes = NULL;
    out_state->morph_comp_data = NULL;
    out_state->morph_normals_count = 0;
    out_state->morph_normals_sizes = NULL;
    out_state->morph_normals_data = NULL;
    out_state->has_legacy_morphkeys = 0;
    out_state->legacy_morphkeys = NULL;
    out_state->legacy_morphkeys_size = 0;
    out_state->raw_tail = NULL;
    out_state->raw_tail_size = 0;

    uint32_t data_version = nmo_chunk_get_data_version(chunk);
    bool section_found = false;
    size_t section_dwords = 0u;

    /* RCKObjectAnimation::Load tests the data version first: the shared,
       controllers and new-data sections exist from version 1, and in a
       version 0 chunk the identifier 0x1000 is a three-float root vector. */
    if (data_version < 1u) {
        out_state->format = CKOBJANIM_FORMAT_LEGACY;
        /* The keyframe data is cleared (length 0) before the sections are
           read, and only a length section sets it again. */
        out_state->length = 0.0f;
        NMO_RETURN_IF_ERROR(read_legacy_controllers(chunk, arena, out_state));
        nmo_objectanimation_check_refs(out_state, context);
        NMO_RETURN_OK();
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMSHARED, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 10u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        out_state->format = CKOBJANIM_FORMAT_SHARED;
        out_state->has_shared_anim = 1;
        nmo_status_t result = nmo_ref_read(chunk, &out_state->shared_anim);
        if (result != NMO_OK) return result;
        out_state->has_root_pos = 1;
        result = nmo_chunk_read_vector3(chunk, &out_state->root_pos);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_vector4(chunk, &out_state->root_extra);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_dword(chunk, &out_state->flags);
        if (result != NMO_OK) return result;
        if ((out_state->flags & 0x80u) != 0u && section_dwords < 13u) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        result = nmo_ref_read(chunk, &out_state->entity);
        if (result != NMO_OK) return result;
        if (out_state->flags & 0x80u) {
            out_state->has_merge = 1;
            result = nmo_chunk_read_float(chunk, &out_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_ref_read(chunk, &out_state->anim1);
            if (result != NMO_OK) return result;
            result = nmo_ref_read(chunk, &out_state->anim2);
            if (result != NMO_OK) return result;
        }
        /* SHARED format has no controller data, keep raw_tail for any remainder */
        const size_t position = nmo_chunk_get_position(chunk);
        if (position > section_end) return NMO_ERR_TRUNCATED_CHUNK;
        result = read_raw_tail(
            chunk, arena, section_end - position,
            (void **)&out_state->raw_tail, &out_state->raw_tail_size);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_animation_require_section_end(
            chunk, section_end));
        nmo_objectanimation_check_refs(out_state, context);
        NMO_RETURN_OK();
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMCONTROLLERS, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 11u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        out_state->format = CKOBJANIM_FORMAT_CONTROLLERS;
        out_state->has_root_pos = 1;
        nmo_status_t result = nmo_chunk_read_vector3(chunk, &out_state->root_pos);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_vector4(chunk, &out_state->root_extra);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_dword(chunk, &out_state->flags);
        if (result != NMO_OK) return result;
        if ((out_state->flags & 0x80u) != 0u && section_dwords < 14u) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        result = nmo_ref_read(chunk, &out_state->entity);
        if (result != NMO_OK) return result;
        out_state->has_length = 1;
        result = nmo_chunk_read_float(chunk, &out_state->length);
        if (result != NMO_OK) return result;
        if (out_state->flags & 0x80u) {
            out_state->has_merge = 1;
            result = nmo_chunk_read_float(chunk, &out_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_ref_read(chunk, &out_state->anim1);
            if (result != NMO_OK) return result;
            result = nmo_ref_read(chunk, &out_state->anim2);
            if (result != NMO_OK) return result;
        }
        /* CONTROLLERS format: parse controller loop */
        result = read_controllers_loop(chunk, arena, out_state);
        if (result != NMO_OK) return result;
        NMO_RETURN_IF_ERROR(nmo_animation_require_section_end(
            chunk, section_end));
        nmo_objectanimation_check_refs(out_state, context);
        NMO_RETURN_OK();
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional_sized(
        chunk, CK_STATESAVE_OBJANIMNEWDATA, &section_found,
        &section_dwords));
    if (section_found) {
        if (section_dwords < 12u) return NMO_ERR_TRUNCATED_CHUNK;
        const size_t section_end =
            nmo_chunk_get_position(chunk) + section_dwords;
        out_state->format = CKOBJANIM_FORMAT_NEWDATA;
        out_state->has_root_pos = 1;
        nmo_status_t result = nmo_chunk_read_vector3(chunk, &out_state->root_pos);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_vector4(chunk, &out_state->root_extra);
        if (result != NMO_OK) return result;
        out_state->has_morph_counts = 1;
        result = nmo_chunk_read_int(chunk, &out_state->morph_vertex_count);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_int(chunk, &out_state->morph_key_count);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_dword(chunk, &out_state->flags);
        if (result != NMO_OK) return result;
        if ((out_state->flags & 0x80u) != 0u && section_dwords < 15u) {
            return NMO_ERR_TRUNCATED_CHUNK;
        }
        result = nmo_ref_read(chunk, &out_state->entity);
        if (result != NMO_OK) return result;
        out_state->has_length = 1;
        result = nmo_chunk_read_float(chunk, &out_state->length);
        if (result != NMO_OK) return result;
        if (out_state->flags & 0x80u) {
            out_state->has_merge = 1;
            result = nmo_chunk_read_float(chunk, &out_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_ref_read(chunk, &out_state->anim1);
            if (result != NMO_OK) return result;
            result = nmo_ref_read(chunk, &out_state->anim2);
            if (result != NMO_OK) return result;
        }
        /* NEWDATA format: parse morph keys + 4 inline controllers */
        result = read_newdata_controllers(
            chunk, arena, out_state, section_end);
        if (result != NMO_OK) return result;
        nmo_objectanimation_check_refs(out_state, context);
        NMO_RETURN_OK();
    }

    if (out_state->format == CKOBJANIM_FORMAT_NONE) {
        /* Unknown format or empty, keep the sections as raw_tail */
        nmo_status_t result = read_unread_sections(
            chunk, arena, out_state,
            (void **)&out_state->raw_tail, &out_state->raw_tail_size);
        if (result == NMO_ERR_NOT_FOUND) {
            /* Not an identifier chain: keep the rest of the data as it is. */
            const size_t position = nmo_chunk_get_position(chunk);
            const size_t total_dwords =
                nmo_chunk_get_data_size(chunk) / sizeof(uint32_t);
            if (position > total_dwords) return NMO_ERR_TRUNCATED_CHUNK;
            result = read_raw_tail(
                chunk, arena, total_dwords - position,
                (void **)&out_state->raw_tail, &out_state->raw_tail_size);
        }
        if (result != NMO_OK) return result;
    }

    nmo_objectanimation_check_refs(out_state, context);
    NMO_RETURN_OK();
}

static const nmo_objanim_controller_t *nmo_objectanimation_find_controller(
    const nmo_objectanimation_state_t *state,
    uint32_t type)
{
    for (uint32_t i = 0; i < state->controller_count; ++i) {
        if (state->controllers[i].type == type) {
            return &state->controllers[i];
        }
    }
    return NULL;
}

static nmo_status_t nmo_objectanimation_write_legacy_controller(
    nmo_chunk_t *chunk,
    const nmo_objanim_controller_t *controller)
{
    const uint32_t data_size = controller ? controller->data_size : 0u;
    const uint32_t key_count = controller ? controller->key_count : 0u;
    NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(chunk, data_size));
    NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(chunk, key_count));
    if (controller != NULL) {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_buffer_no_size(
            chunk, controller->data, controller->data_size));
    }
    return NMO_OK;
}

static nmo_status_t nmo_objectanimation_serialize_internal(
    const nmo_objectanimation_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_objectanimation_serialize");
    }

    const uint32_t data_version = nmo_chunk_get_data_version(out_chunk);
    if (in_state->format == CKOBJANIM_FORMAT_LEGACY) {
        if (data_version >= 1u) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Legacy object animation requires data version 0");
        }
    } else if (data_version == 0u) {
        out_chunk->data_version = NMO_CHUNK_DATA_VERSION_CURRENT;
    }
    NMO_RETURN_IF_ERROR(nmo_chunk_start_write(out_chunk));

    {
        nmo_status_t result = nmo_sceneobject_serialize(&in_state->base, out_chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    const bool is_file = nmo_object_serialize_is_file(out_chunk, context);
    if (!is_file && (save_flags & CK_STATESAVE_OBJANIMALL) == 0) {
        NMO_RETURN_OK();
    }

    /* RCKObjectAnimation::Save writes SHARED only while another animation owns
     * the keys; without an owner the animation has fresh, empty keyframe data
     * (length 100), which it writes as CONTROLLERS. */
    nmo_objectanimation_state_t ownerless;
    if (in_state->format == CKOBJANIM_FORMAT_SHARED &&
        nmo_ref_runtime_id(&in_state->shared_anim) == NMO_OBJECT_ID_NONE &&
        in_state->shared_anim.state == NMO_REF_NONE) {
        ownerless = *in_state;
        ownerless.format = CKOBJANIM_FORMAT_CONTROLLERS;
        ownerless.has_shared_anim = 0;
        ownerless.controllers = NULL;
        ownerless.controller_count = 0;
        ownerless.has_length = 1;
        ownerless.length = 100.0f;
        in_state = &ownerless;
    }

    switch (in_state->format) {
    case CKOBJANIM_FORMAT_SHARED: {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMSHARED);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->shared_anim);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->root_pos);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector4(out_chunk, &in_state->root_extra);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->flags);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->entity);
        if (result != NMO_OK) return result;
        if ((in_state->flags & 0x80u) != 0) {
            result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim1);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim2);
            if (result != NMO_OK) return result;
        }
        break;
    }
    case CKOBJANIM_FORMAT_CONTROLLERS: {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMCONTROLLERS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->root_pos);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector4(out_chunk, &in_state->root_extra);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->flags);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->entity);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->length);
        if (result != NMO_OK) return result;
        if ((in_state->flags & 0x80u) != 0) {
            result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim1);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim2);
            if (result != NMO_OK) return result;
        }
        break;
    }
    case CKOBJANIM_FORMAT_NEWDATA: {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMNEWDATA);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->root_pos);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector4(out_chunk, &in_state->root_extra);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->morph_vertex_count);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->morph_key_count);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->flags);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->entity);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->length);
        if (result != NMO_OK) return result;
        if ((in_state->flags & 0x80u) != 0) {
            result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim1);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim2);
            if (result != NMO_OK) return result;
        }
        break;
    }
    default:
        break;
    }

    /* Write controller data based on format */
    if (in_state->format == CKOBJANIM_FORMAT_CONTROLLERS) {
        /* CONTROLLERS format: write {type, data_size/4, data} per controller + terminator 0 */
        for (uint32_t i = 0; i < in_state->controller_count; ++i) {
            const nmo_objanim_controller_t *ctrl = &in_state->controllers[i];
            /* Controllers with keys get their count written as the blob prefix. */
            const bool has_prefix = ctrl->key_count > 0u;
            /* The engine reads a key count from every blob, so a controller
               without keys is written as {type, 1, 0}. */
            const bool empty = ctrl->key_count == 0u && ctrl->data_size == 0u;
            nmo_status_t result = nmo_chunk_write_dword(out_chunk, ctrl->type);
            if (result != NMO_OK) return result;
            uint32_t size_dwords =
                (ctrl->data_size + (has_prefix ? (uint32_t)sizeof(uint32_t) : 0u)) / 4;
            if (empty) size_dwords = 1u;
            result = nmo_chunk_write_dword(out_chunk, size_dwords);
            if (result != NMO_OK) return result;
            if (has_prefix || empty) {
                result = nmo_chunk_write_dword(out_chunk, ctrl->key_count);
                if (result != NMO_OK) return result;
            }
            if (ctrl->data_size > 0 && ctrl->data != NULL) {
                result = nmo_chunk_write_buffer_no_size(out_chunk, ctrl->data, ctrl->data_size);
                if (result != NMO_OK) return result;
            }
        }
        /* Write terminator */
        nmo_status_t result = nmo_chunk_write_dword(out_chunk, 0);
        if (result != NMO_OK) return result;
    } else if (in_state->format == CKOBJANIM_FORMAT_NEWDATA) {
        /* NEWDATA format: write morph keys, then 4 controllers as {bufSize, keyCount, data} */
        if (in_state->morph_key_parsed_count > 0 && in_state->morph_keys != NULL) {
            for (uint32_t i = 0; i < in_state->morph_key_parsed_count; ++i) {
                const nmo_objanim_morph_key_t *key = &in_state->morph_keys[i];
                nmo_status_t result = nmo_chunk_write_float(out_chunk, key->time_step);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_dword(out_chunk, key->data_size);
                if (result != NMO_OK) return result;
                if (key->data_size > 0 && key->data != NULL) {
                    result = nmo_chunk_write_buffer_no_size(out_chunk, key->data, key->data_size);
                    if (result != NMO_OK) return result;
                }
            }
        }

        /* Write 4 controllers in fixed order: position, scale, rotation, scaleAxis */
        static const uint32_t expected_types[4] = {
            CKANIMATION_LINPOS_CONTROL,
            CKANIMATION_LINSCL_CONTROL,
            CKANIMATION_LINROT_CONTROL,
            CKANIMATION_LINSCLAXIS_CONTROL
        };

        for (int slot = 0; slot < 4; ++slot) {
            const nmo_objanim_controller_t *ctrl = NULL;
            for (uint32_t i = 0; i < in_state->controller_count; ++i) {
                if (in_state->controllers[i].type == expected_types[slot]) {
                    ctrl = &in_state->controllers[i];
                    break;
                }
            }

            uint32_t buf_size = ctrl ? ctrl->data_size : 0;
            uint32_t key_count = ctrl ? ctrl->key_count : 0;
            nmo_status_t result = nmo_chunk_write_dword(out_chunk, buf_size);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, key_count);
            if (result != NMO_OK) return result;
            if (ctrl && buf_size > 0 && ctrl->data != NULL) {
                result = nmo_chunk_write_buffer_no_size(out_chunk, ctrl->data, buf_size);
                if (result != NMO_OK) return result;
            }
        }

        /* Write the per-key morph sections that are present */
        const struct {
            uint32_t identifier;
            uint32_t count;
            const uint32_t *sizes;
            void *const *data;
        } morph_sections[2] = {
            {CK_STATESAVE_OBJANIMMORPHCOMP, in_state->morph_comp_count,
             in_state->morph_comp_sizes, in_state->morph_comp_data},
            {CK_STATESAVE_OBJANIMMORPHNORMALS, in_state->morph_normals_count,
             in_state->morph_normals_sizes, in_state->morph_normals_data},
        };
        for (int section = 0; section < 2; ++section) {
            if (morph_sections[section].count == 0) continue;
            nmo_status_t result = nmo_chunk_write_identifier(
                out_chunk, morph_sections[section].identifier);
            if (result != NMO_OK) return result;
            for (uint32_t i = 0; i < morph_sections[section].count; ++i) {
                uint32_t size_bytes = morph_sections[section].sizes[i];
                result = nmo_chunk_write_dword(out_chunk, size_bytes);
                if (result != NMO_OK) return result;
                if (size_bytes > 0 && morph_sections[section].data[i] != NULL) {
                    result = nmo_chunk_write_buffer_no_size(
                        out_chunk, morph_sections[section].data[i], size_bytes);
                    if (result != NMO_OK) return result;
                }
            }
        }
    } else if (in_state->format == CKOBJANIM_FORMAT_LEGACY) {
        /* LEGACY format: write identifier-based sections */

        if (in_state->has_legacy_morphkeys) {
            nmo_status_t result = nmo_chunk_write_identifier(
                out_chunk, CK_STATESAVE_OBJANIMMORPHKEYS);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_buffer_no_size(
                out_chunk, in_state->legacy_morphkeys,
                in_state->legacy_morphkeys_size);
            if (result != NMO_OK) return result;
        }

        /* Morph keys */
        if (in_state->has_morph_counts ||
            in_state->morph_key_parsed_count > 0) {
            nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMMORPHKEYS2);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->morph_key_parsed_count);
            if (result != NMO_OK) return result;
            if (in_state->morph_key_parsed_count > 0) {
                result = nmo_chunk_write_int(
                    out_chunk, in_state->morph_vertex_count);
                if (result != NMO_OK) return result;
            }
            for (uint32_t i = 0; i < in_state->morph_key_parsed_count; ++i) {
                const nmo_objanim_morph_key_t *mk = &in_state->morph_keys[i];
                result = nmo_chunk_write_float(out_chunk, mk->time_step);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_dword(out_chunk, mk->data_size);
                if (result != NMO_OK) return result;
                if (mk->data_size > 0 && mk->data != NULL) {
                    result = nmo_chunk_write_buffer_no_size(out_chunk, mk->data, mk->data_size);
                    if (result != NMO_OK) return result;
                }
            }
        }

        const nmo_objanim_controller_t *position =
            nmo_objectanimation_find_controller(
                in_state, CKANIMATION_LINPOS_CONTROL);
        const nmo_objanim_controller_t *rotation =
            nmo_objectanimation_find_controller(
                in_state, CKANIMATION_LINROT_CONTROL);
        const nmo_objanim_controller_t *scale_axis =
            nmo_objectanimation_find_controller(
                in_state, CKANIMATION_LINSCLAXIS_CONTROL);
        const nmo_objanim_controller_t *scale =
            nmo_objectanimation_find_controller(
                in_state, CKANIMATION_LINSCL_CONTROL);

        if (in_state->has_legacy_position_section || position != NULL) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(
                out_chunk, CK_STATESAVE_OBJANIMPOSKEYS));
            NMO_RETURN_IF_ERROR(nmo_objectanimation_write_legacy_controller(
                out_chunk, position));
        }
        if (in_state->has_legacy_rotation_section ||
            rotation != NULL || scale_axis != NULL) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(
                out_chunk, CK_STATESAVE_OBJANIMROTKEYS));
            NMO_RETURN_IF_ERROR(nmo_objectanimation_write_legacy_controller(
                out_chunk, rotation));
            NMO_RETURN_IF_ERROR(nmo_objectanimation_write_legacy_controller(
                out_chunk, scale_axis));
        }
        if (in_state->has_legacy_scale_section || scale != NULL) {
            NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(
                out_chunk, CK_STATESAVE_OBJANIMSCLKEYS));
            NMO_RETURN_IF_ERROR(nmo_objectanimation_write_legacy_controller(
                out_chunk, scale));
        }

        /* Legacy header fields */
        if (in_state->has_legacy_flags_section || in_state->flags != 0) {
            nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMFLAGS);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->flags);
            if (result != NMO_OK) return result;
        }

        if (in_state->has_legacy_entity_section ||
            nmo_ref_serialized_id(&in_state->entity) !=
                NMO_OBJECT_ID_NONE) {
            nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMENTITY);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->entity);
            if (result != NMO_OK) return result;
        }

        if (in_state->has_length) {
            nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMLENGTH);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_float(out_chunk, in_state->length);
            if (result != NMO_OK) return result;
        }

        if (in_state->has_merge) {
            nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMMERGE);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, (in_state->flags & 0x80u) ? 1 : 0);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim1);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim2);
            if (result != NMO_OK) return result;
        }

        if (in_state->has_root_pos) {
            nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMNEWDATA);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_vector3(out_chunk, &in_state->root_pos);
            if (result != NMO_OK) return result;
        }

        /* Fallback raw_tail for any remaining unparsed data */
        if (in_state->raw_tail && in_state->raw_tail_size > 0) {
            nmo_status_t result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                                 in_state->raw_tail,
                                                                 in_state->raw_tail_size);
            if (result != NMO_OK) return result;
        }
    } else if (in_state->format == CKOBJANIM_FORMAT_NONE &&
               in_state->raw_tail && in_state->raw_tail_size > 0 &&
               (in_state->raw_tail_size & 3u) == 0u &&
               unread_sections_are_chain(
                   in_state->raw_tail, in_state->raw_tail_size / 4u)) {
        /* Write the kept sections through the identifier chain, after the
           base object sections. */
        const size_t dwords = in_state->raw_tail_size / 4u;
        size_t index = 0u;
        bool last = false;
        while (!last) {
            uint32_t id = 0u;
            size_t end = 0u;
            (void)unread_section_at(
                in_state->raw_tail, dwords, index, &id, &end, &last);
            NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(out_chunk, id));
            NMO_RETURN_IF_ERROR(nmo_chunk_write_buffer_no_size(
                out_chunk, in_state->raw_tail + (index + 2u) * 4u,
                (end - index - 2u) * 4u));
            index = end;
        }
    } else {
        /* Fallback: write raw_tail if present */
        if (in_state->raw_tail && in_state->raw_tail_size > 0) {
            nmo_status_t result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                                 in_state->raw_tail,
                                                                 in_state->raw_tail_size);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_animation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_animation_state_t *out_state = (nmo_animation_state_t *)instance;
    if (!out_state || !chunk) return NMO_ERR_INVALID_ARGUMENT;
    nmo_animation_state_t decoded;
    nmo_status_t result = nmo_animation_create(&decoded, NULL, context);
    if (result != NMO_OK) return result;
    result = nmo_animation_deserialize_internal(chunk, context, &decoded);
    if (result != NMO_OK) {
        nmo_animation_destroy(&decoded, NULL, context);
        return result;
    }
    nmo_animation_destroy(out_state, NULL, context);
    *out_state = decoded;
    return NMO_OK;
}

NMO_DEFINE_OBJECT_STAGED_SERIALIZE_STATE(nmo_animation, nmo_animation_state_t, NULL)

nmo_status_t nmo_keyedanimation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_keyedanimation_state_t *out_state = (nmo_keyedanimation_state_t *)instance;
    if (!out_state || !chunk) return NMO_ERR_INVALID_ARGUMENT;
    nmo_keyedanimation_state_t decoded;
    nmo_status_t result = nmo_keyedanimation_deserialize_internal(
        chunk, context, &decoded);
    if (result != NMO_OK) {
        nmo_keyedanimation_destroy(&decoded, NULL, context);
        return result;
    }
    nmo_keyedanimation_destroy(out_state, NULL, context);
    *out_state = decoded;
    return NMO_OK;
}

NMO_DEFINE_OBJECT_STAGED_SERIALIZE_STATE(nmo_keyedanimation, nmo_keyedanimation_state_t, NULL)

nmo_status_t nmo_objectanimation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_objectanimation_state_t *out_state = (nmo_objectanimation_state_t *)instance;
    if (!out_state || !chunk) return NMO_ERR_INVALID_ARGUMENT;
    nmo_objectanimation_state_t decoded;
    nmo_status_t result = nmo_objectanimation_deserialize_internal(
        chunk, context, &decoded);
    if (result != NMO_OK) {
        nmo_objectanimation_destroy(&decoded, NULL, context);
        return result;
    }
    nmo_objectanimation_destroy(out_state, NULL, context);
    *out_state = decoded;
    return NMO_OK;
}

NMO_DEFINE_OBJECT_STAGED_SERIALIZE_STATE(nmo_objectanimation, nmo_objectanimation_state_t, nmo_objectanimation_validate)
