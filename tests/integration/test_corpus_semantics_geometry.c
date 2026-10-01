/**
 * @file test_corpus_semantics_geometry.c
 * @brief Decoded geometry, entity, camera, light, curve, character, place and
 *        grid values of the reference corpus satisfy what the engine guarantees
 *
 * test_corpus_chunk_roundtrip proves that no bytes are lost; it cannot tell a
 * field that was read into the wrong member from a correct one. Every
 * invariant below is a relationship between decoded values that the Virtools
 * engine (CK2_3D.dll / CK2.dll) maintains: a setter or loader enforces it, or
 * its own Save always writes values that satisfy it. The comment above each
 * check names the engine function that establishes it. Each invariant keeps
 * two counters, the values checked and the values violating it; the test
 * requires the first to be positive (the corpus exercises the check) and the
 * second to be zero.
 *
 * Checks marked "sanity" are not enforced by a setter; the engine would feed
 * the value to a division, a projection or a rasterizer, so a misread member
 * (an integer read as a float, a swapped pair) shows up as a violation.
 */

#include "../test_framework.h"

#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_grid_schemas.h"
#include "object/builtin/nmo_layer_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_patchmesh_schemas.h"
#include "object/builtin/nmo_place_schemas.h"
#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
#include "type/nmo_type_query.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REPORTED_VIOLATIONS 60
#define MAX_REPORTED_PER_INVARIANT 4

/* VXMESH_ALLFLAGS of CK2_3D.dll; libnmo's enum names a subset of these bits. */
#define MESH_FLAGS_KNOWN 0x1FFFF39Fu
/* VXCHANNEL_SAMEUV: the channel shares the main texture coordinates. */
#define MESH_CHANNEL_SAMEUV 0x00800000u
/* VXCHANNEL_FLAGS: ACTIVE, KEEPFOG, SAMEUV, NOTLIT, MONO, RESERVED1, LAST, UPTODATE. */
#define MESH_CHANNEL_FLAGS_KNOWN 0x1FC00001u
/* VXBLEND_MODE runs from VXBLEND_ZERO (1) to VXBLEND_BOTHINVSRCALPHA (13). */
#define BLEND_MODE_FIRST 1u
#define BLEND_MODE_LAST 13u

#define INVARIANT_LIST(X) \
    X(MESH_FACE_VERTEX, "mesh: face vertex index < vertex count") \
    X(MESH_FACE_GROUP, "mesh: face material group index < group count") \
    X(MESH_GROUP_PADDING, "mesh: material group padding word is 0") \
    X(MESH_GROUP_MATERIAL, "mesh: material group names a CKMaterial") \
    X(MESH_CHANNEL_MATERIAL, "mesh: channel material is a CKMaterial") \
    X(MESH_CHANNEL_UV_COUNT, "mesh: channel uv count = vertices (0 if SAMEUV)") \
    X(MESH_CHANNEL_BLEND, "mesh: channel blend modes are VXBLEND_MODE values") \
    X(MESH_CHANNEL_FLAG_BITS, "mesh: channel flags use VXCHANNEL bits only") \
    X(MESH_WEIGHT_COUNT, "mesh: vertex weight count = vertex count") \
    X(MESH_PM_RENDERED, "mesh: PM vertices rendered in [0, vertex count]") \
    X(MESH_PM_DATA_SIZE, "mesh: PM collapse table = one int per vertex") \
    X(MESH_PM_DATA_INDEX, "mesh: PM collapse target < vertex count") \
    X(MESH_PROCEDURAL_POS, "mesh: PROCEDURALPOS => stored positions are zero") \
    X(MESH_NORMALS_OMITTED, "mesh: GENNORMALS/PROCEDURALPOS => no stored normals") \
    X(MESH_FLAG_BITS, "mesh: flags use defined VXMESH bits only") \
    X(MESH_FACE_MASK_CHANNELS, "mesh: a cleared face channel mask needs channels") \
    X(MESH_FINITE, "mesh: positions, normals, uvs finite") \
    X(PATCH_VEC_COUNT, "patch mesh: 0 <= vec count <= total vector count") \
    X(PATCH_TYPE, "patch mesh: patch type is 3 or 4") \
    X(PATCH_CORNER_INDEX, "patch mesh: patch corner index < vertex count") \
    X(PATCH_VEC_INDEX, "patch mesh: patch vec/interior index < vec count") \
    X(PATCH_EDGE_INDEX, "patch mesh: edge vertex/patch index in range") \
    X(PATCH_MATERIAL, "patch mesh: patch material is a CKMaterial") \
    X(PATCH_TV_INDEX, "patch mesh: tv patch index < channel uv count") \
    X(ENT_POSITION_FINITE, "entity: world position finite") \
    X(ENT_ROTATION_FINITE, "entity: world rotation finite (not curve points)") \
    X(ENT_VISIBLE_SYNC, "entity: moveable VISIBLE <=> object visible") \
    X(ENT_HIDE_SYNC, "entity: moveable HIERARCHICALHIDE <=> object hidden by hierarchy") \
    X(ENT_PARENT_CLASS, "entity: parent is a CK3dEntity") \
    X(ENT_PARENT_NOT_SELF, "entity: parent is not the entity itself") \
    X(ENT_PARENT_ACYCLIC, "entity: parent chain has no cycle") \
    X(ENT_PARENT_FLAG, "entity: PARENTVALID <=> parent set") \
    X(ENT_PLACE_FLAG, "entity: PLACEVALID <=> place set") \
    X(ENT_PLACE_CLASS, "entity: place is a CKPlace") \
    X(ENT_PLACE_DERIVED, "entity: place = parent, or the parent's place") \
    X(ENT_ZORDER_FLAG, "entity: ZORDERVALID <=> z order != 0") \
    X(ENT_ZORDER_RANGE, "entity: z order within +-10000") \
    X(ENT_MESH_CLASS, "entity: current mesh and mesh list are CKMesh") \
    X(ENT_MESH_LISTED, "entity: current mesh is in the mesh list") \
    X(ENT_MESH_UNIQUE, "entity: mesh list holds a mesh once") \
    X(ENT_CURVE_NO_MESH, "entity: a CKCurve stores no meshes") \
    X(ENT_ANIM_CLASS, "entity: animation list holds CKObjectAnimation") \
    X(ENT_ANIM_UNIQUE, "entity: animation list holds an animation once") \
    X(SKIN_BONE_CLASS, "skin: bone is a CK3dEntity") \
    X(SKIN_COVERS_MESH, "skin: skin vertices >= current mesh vertices") \
    X(SKIN_MESH_PROCEDURAL, "skin: current mesh has PROCEDURALPOS") \
    X(CAM_PROJECTION, "camera: projection type is 1 or 2") \
    X(CAM_FOV, "camera: perspective fov in (0, pi)") \
    X(CAM_PLANES, "camera: 0 < front < back (perspective), front < back") \
    X(CAM_ASPECT, "camera: aspect width, height in [1, 65535]") \
    X(CAM_ORTHO_ZOOM, "camera: orthographic zoom > 0") \
    X(CAM_TARGET_CLASS, "target camera: target is a CK3dEntity, not itself") \
    X(LIGHT_TYPE, "light: type is point, spot or directional") \
    X(LIGHT_POWER_CHUNK, "light: power section stored <=> power != 1") \
    X(LIGHT_RANGE, "light: range >= 0") \
    X(LIGHT_ATTENUATION, "light: attenuation terms >= 0") \
    X(LIGHT_SPOT_CONES, "light: spot 0 <= inner <= outer <= pi, falloff >= 0") \
    X(LIGHT_TARGET_CLASS, "target light: target is a CK3dEntity, not itself") \
    X(CURVE_POINT_CLASS, "curve: control point is a CKCurvePoint") \
    X(CURVE_POINT_UNIQUE, "curve: control point listed once") \
    X(CURVE_POINT_BACKREF, "curve: listed point names the curve") \
    X(CURVE_POINT_PARENT, "curve: listed point is a child of the curve") \
    X(CURVE_STEP_COUNT, "curve: step count > 0") \
    X(CURVE_OPEN_FLAG, "curve: opened is 0 or 1") \
    X(POINT_CURVE_CLASS, "curve point: curve is a CKCurve") \
    X(POINT_PARENT, "curve point: its curve is its parent") \
    X(POINT_TANGENT_MODE, "curve point: tangent mode is 0 or 1") \
    X(POINT_FINITE, "curve point: tcb and tangents finite") \
    X(CHAR_PART_CLASS, "character: body part is a CKBodyPart") \
    X(CHAR_PART_UNIQUE, "character: body part listed once") \
    X(CHAR_PART_BACKREF, "character: listed body part names the character") \
    X(CHAR_ROOT_CLASS, "character: root is a CKBodyPart") \
    X(CHAR_ROOT_OWNER, "character: root body part names the character") \
    X(CHAR_ROOT_LISTED, "character: root body part is listed") \
    X(CHAR_ROOT_PARENT, "character: root body part is a child of the character") \
    X(CHAR_ANIM_CLASS, "character: animations are CKAnimation") \
    X(CHAR_ANIM_UNIQUE, "character: animation listed once") \
    X(CHAR_ANIM_BACKREF, "character: listed animation names the character") \
    X(CHAR_ACTIVE_CLASS, "character: active/dest animation is a CKAnimation") \
    X(CHAR_FLOOR_CLASS, "character: floor reference is a CK3dEntity") \
    X(PART_CHARACTER_CLASS, "body part: character is a CKCharacter") \
    X(PART_BACKREF, "body part: its character lists it (or roots on it)") \
    X(PART_SECTION, "body part: character section stored, joint iff IKJOINTVALID") \
    X(PLACE_LEVEL_CLASS, "place: level is a CKLevel") \
    X(PLACE_PORTAL_PLACE, "place: portal place is a CKPlace") \
    X(PLACE_PORTAL_ENTITY, "place: portal entity is a CK3dEntity with PORTAL") \
    X(PLACE_PORTAL_SYMMETRIC, "place: portal is listed by both places") \
    X(GRID_DIMENSIONS, "grid: width and length > 0") \
    X(GRID_ORIENTATION, "grid: orientation mode in 0..3") \
    X(GRID_CONSTANTS, "grid: reserved word 0 and file marker 1") \
    X(GRID_LAYER_CLASS, "grid: layer is a CKLayer") \
    X(GRID_LAYER_BACKREF, "grid: listed layer names the grid") \
    X(GRID_LAYER_UNIQUE, "grid: layer names are unique") \
    X(LAYER_GRID_LISTS, "layer: its grid lists it") \
    X(LAYER_SQUARE_SIZE, "layer: square buffer = 4 * width * length") \
    X(LAYER_FORMAT_DATA, "layer: square buffer only for format 0") \
    X(LAYER_VERSION, "layer: style version in 1..3") \
    X(LAYER_FLAGS, "layer: flags use the visible bit only") \
    X(LAYER_TYPE_STYLE, "layer: one name has one color and parameter") \
    X(SPRITE3D_MODE, "sprite3d: mode in 0..3") \
    X(SPRITE3D_MATERIAL, "sprite3d: material is a CKMaterial") \
    X(SPRITE3D_FINITE, "sprite3d: size, offset, uv rectangle finite")

typedef enum invariant_id {
#define X(id, text) INV_##id,
    INVARIANT_LIST(X)
#undef X
    INV_COUNT
} invariant_id_t;

static const char *const invariant_text[INV_COUNT] = {
#define X(id, text) text,
    INVARIANT_LIST(X)
#undef X
};

typedef struct invariant_counts {
    size_t checked;
    size_t violated;
    size_t external;   /* references to objects outside the file, not checked */
    size_t printed;
} invariant_counts_t;

/* One layer name's color and parameter, for the per-file style comparison. */
typedef struct layer_style {
    const char *name;
    uint32_t color_rgba;
    nmo_guid_t param_guid;
} layer_style_t;

typedef struct corpus_semantics {
    nmo_context_t *ctx;
    size_t files;
    size_t load_errors;
    size_t reported;
    invariant_counts_t inv[INV_COUNT];
} corpus_semantics_t;

typedef struct file_ctx {
    corpus_semantics_t *stats;
    const char *path;
    const nmo_object_repository_t *repo;
    const nmo_type_registry_t *types;
} file_ctx_t;

typedef enum ref_kind {
    REF_NONE,       /* null reference */
    REF_EXTERNAL,   /* names an object that is not in the file */
    REF_FOUND
} ref_kind_t;

/* Count one checked value; returns whether a violation line should be printed. */
static int invariant_note(const file_ctx_t *f, invariant_id_t id,
                          const nmo_object_t *object, int ok)
{
    invariant_counts_t *counts = &f->stats->inv[id];
    counts->checked++;
    if (ok) {
        return 0;
    }
    counts->violated++;
    if (counts->printed >= MAX_REPORTED_PER_INVARIANT ||
        f->stats->reported >= MAX_REPORTED_VIOLATIONS) {
        return 0;
    }
    counts->printed++;
    f->stats->reported++;
    printf("  %s: object %u (class %u) [%s]: ", f->path,
           (unsigned)object->file_id, (unsigned)object->class_id,
           invariant_text[id]);
    return 1;
}

#define INV_CHECK(f, id, object, cond, ...)                                    \
    do {                                                                       \
        if (invariant_note((f), (id), (object), (cond) ? 1 : 0)) {             \
            printf(__VA_ARGS__);                                               \
            putchar('\n');                                                     \
        }                                                                      \
    } while (0)

static const nmo_object_t *ref_lookup(const file_ctx_t *f, const nmo_ref_t *ref,
                                      ref_kind_t *kind)
{
    if (ref == NULL || ref->state == NMO_REF_NONE) {
        *kind = REF_NONE;
        return NULL;
    }
    if (ref->state == NMO_REF_RESOLVED || ref->state == NMO_REF_CLASS_MISMATCH) {
        const nmo_object_t *target = nmo_object_repository_find_by_id(f->repo, ref->id);
        if (target != NULL) {
            *kind = REF_FOUND;
            return target;
        }
    }
    *kind = REF_EXTERNAL;
    return NULL;
}

static int class_is(const file_ctx_t *f, const nmo_object_t *object, nmo_class_id_t base)
{
    return object->class_id == base ||
           nmo_type_query_class_is_derived_from(f->types, object->class_id, base);
}

/* Check that a non-null reference names an object of class `base`. */
static void check_ref_class(const file_ctx_t *f, invariant_id_t id,
                            const nmo_object_t *owner, const nmo_ref_t *ref,
                            nmo_class_id_t base, const char *what)
{
    ref_kind_t kind;
    const nmo_object_t *target = ref_lookup(f, ref, &kind);
    if (kind == REF_NONE) {
        return;
    }
    if (kind == REF_EXTERNAL) {
        f->stats->inv[id].external++;
        return;
    }
    INV_CHECK(f, id, owner, class_is(f, target, base),
              "%s names object %u of class %u", what,
              (unsigned)target->file_id, (unsigned)target->class_id);
}

static int is_finite3(const nmo_vector_t *v)
{
    return isfinite(v->x) && isfinite(v->y) && isfinite(v->z);
}

/* The CK3dEntity part of every object of the entity family; the derived state
 * structs of the family all start with their parent state. */
static const nmo_3dentity_state_t *entity_of(const nmo_object_t *object)
{
    switch (object->class_id) {
    case NMO_CID_3DENTITY:
    case NMO_CID_CAMERA:
    case NMO_CID_TARGETCAMERA:
    case NMO_CID_CURVEPOINT:
    case NMO_CID_SPRITE3D:
    case NMO_CID_LIGHT:
    case NMO_CID_TARGETLIGHT:
    case NMO_CID_CHARACTER:
    case NMO_CID_3DOBJECT:
    case NMO_CID_BODYPART:
    case NMO_CID_CURVE:
    case NMO_CID_GRID:
        return (const nmo_3dentity_state_t *)nmo_object_get_state(object);
    default:
        return NULL;
    }
}

static int ref_names(const file_ctx_t *f, const nmo_ref_t *ref, const nmo_object_t *wanted)
{
    ref_kind_t kind;
    return ref_lookup(f, ref, &kind) == wanted && kind == REF_FOUND;
}

/* ------------------------------------------------------------------------ */
/* CKMesh and CKPatchMesh                                                    */
/* ------------------------------------------------------------------------ */

static void check_mesh(const file_ctx_t *f, const nmo_object_t *o, const nmo_mesh_state_t *m)
{
    /* RCKMesh::Load ends in BuildNormals/BuildFaceNormals, which read
       m_Vertices through the face indices unchecked, and maps the face's group
       through a table of the saved groups; the engine's Save writes both
       straight from its arrays. */
    for (uint32_t i = 0; i < m->face_count; ++i) {
        for (uint32_t k = 0; k < 3u; ++k) {
            const uint32_t index = m->face_vertex_indices[3u * i + k];
            INV_CHECK(f, INV_MESH_FACE_VERTEX, o, index < m->vertex_count,
                      "face %u corner %u = %u, %u vertices", (unsigned)i,
                      (unsigned)k, (unsigned)index, (unsigned)m->vertex_count);
        }
        INV_CHECK(f, INV_MESH_FACE_GROUP, o,
                  m->faces[i].material_group_idx < m->material_group_count,
                  "face %u group %u, %u groups", (unsigned)i,
                  (unsigned)m->faces[i].material_group_idx,
                  (unsigned)m->material_group_count);
    }

    /* RCKMesh::Save writes WriteInt(0) after every material group object. */
    for (uint32_t i = 0; i < m->material_group_count; ++i) {
        INV_CHECK(f, INV_MESH_GROUP_PADDING, o, m->material_groups[i].padding == 0,
                  "group %u padding %d", (unsigned)i, (int)m->material_groups[i].padding);
        check_ref_class(f, INV_MESH_GROUP_MATERIAL, o, &m->material_groups[i].material,
                        NMO_CID_MATERIAL, "material group");
    }

    /* RCKMesh::Save writes GetVertexCount() coordinates when the channel owns
       an array and none otherwise, and SetChannelFlags frees the array exactly
       for SAMEUV. A channel may have no material (CheckPreDeletion nulls the
       material of a deleted one and Load then drops the channel), so only a
       present material is checked. */
    for (uint32_t i = 0; i < m->material_channel_count; ++i) {
        const nmo_material_channel_t *channel = &m->material_channels[i];
        check_ref_class(f, INV_MESH_CHANNEL_MATERIAL, o, &channel->material,
                        NMO_CID_MATERIAL, "channel material");
        /* sanity: SetChannelSourceBlend/DestBlend take a VXBLEND_MODE and
           SetChannelFlags a VXCHANNEL_FLAGS mask. */
        INV_CHECK(f, INV_MESH_CHANNEL_BLEND, o,
                  channel->source_blend >= BLEND_MODE_FIRST &&
                      channel->source_blend <= BLEND_MODE_LAST &&
                      channel->dest_blend >= BLEND_MODE_FIRST &&
                      channel->dest_blend <= BLEND_MODE_LAST,
                  "channel %u blend %u -> %u", (unsigned)i, (unsigned)channel->source_blend,
                  (unsigned)channel->dest_blend);
        INV_CHECK(f, INV_MESH_CHANNEL_FLAG_BITS, o,
                  (channel->flags & ~MESH_CHANNEL_FLAGS_KNOWN) == 0u,
                  "channel %u flags 0x%x", (unsigned)i, (unsigned)channel->flags);
        const uint32_t expected =
            (channel->flags & MESH_CHANNEL_SAMEUV) ? 0u : m->vertex_count;
        INV_CHECK(f, INV_MESH_CHANNEL_UV_COUNT, o, channel->uv_count == expected,
                  "channel %u flags 0x%x has %u uvs, expected %u", (unsigned)i,
                  (unsigned)channel->flags, (unsigned)channel->uv_count,
                  (unsigned)expected);
    }

    /* RCKMesh::Save writes the face channel masks (CK_STATESAVE_MESHFACECHANMASK)
       only when a mask has a cleared bit and the mesh has channels. */
    {
        int cleared = 0;
        for (uint32_t i = 0; i < m->face_count; ++i) {
            cleared = cleared || m->faces[i].channel_mask != 0xFFFFu;
        }
        if (cleared) {
            INV_CHECK(f, INV_MESH_FACE_MASK_CHANNELS, o, m->material_channel_count > 0u,
                      "face channel masks stored without channels");
        }
    }

    /* RCKMesh::SetVertexCount resizes m_VertexWeights with the vertices and
       Save writes its size. */
    if (m->vertex_weight_count > 0u) {
        INV_CHECK(f, INV_MESH_WEIGHT_COUNT, o, m->vertex_weight_count == m->vertex_count,
                  "%u weights for %u vertices", (unsigned)m->vertex_weight_count,
                  (unsigned)m->vertex_count);
    }

    if (m->has_progressive_mesh && m->vertex_count > 0u) {
        /* SetVerticesRendered clamps the count to [0, GetVertexCount()]. */
        INV_CHECK(f, INV_MESH_PM_RENDERED, o,
                  m->pm_field_0 >= 0 && (uint32_t)m->pm_field_0 <= m->vertex_count,
                  "%d vertices rendered of %u", (int)m->pm_field_0,
                  (unsigned)m->vertex_count);
        /* RCKMesh::CreatePM builds one collapse target per vertex, remapped
           into the vertex order of the mesh, and Save writes that table. */
        if (m->pm_data_size > 0u) {
            INV_CHECK(f, INV_MESH_PM_DATA_SIZE, o,
                      m->pm_data_size == 4u * m->vertex_count,
                      "collapse table %u bytes for %u vertices",
                      (unsigned)m->pm_data_size, (unsigned)m->vertex_count);
            const uint32_t entries = m->pm_data_size / 4u;
            const uint32_t *table = (const uint32_t *)m->pm_data;
            for (uint32_t i = 0; i < entries; ++i) {
                INV_CHECK(f, INV_MESH_PM_DATA_INDEX, o, table[i] < m->vertex_count,
                          "collapse target %u = %u", (unsigned)i, (unsigned)table[i]);
            }
        }
    }

    /* RCKMesh::GetSaveFlags sets "positions external" (0x10) for PROCEDURALPOS;
       the loader leaves the omitted positions at their zero fill. */
    if ((m->flags & VXMESH_PROCEDURALPOS) && m->vertex_count > 0u) {
        int all_zero = 1;
        for (uint32_t i = 0; i < m->vertex_count; ++i) {
            const nmo_vector_t *p = &m->vertices[i].position;
            if (p->x != 0.0f || p->y != 0.0f || p->z != 0.0f) {
                all_zero = 0;
                break;
            }
        }
        INV_CHECK(f, INV_MESH_PROCEDURAL_POS, o, all_zero,
                  "flags 0x%x but positions are stored", (unsigned)m->flags);
    }
    /* GetSaveFlags leaves "normals missing" (0x04) set when (flags & 0x280000)
       is non-zero, i.e. GENNORMALS or PROCEDURALPOS. */
    if ((m->flags & (VXMESH_GENNORMALS | VXMESH_PROCEDURALPOS)) && m->vertex_count > 0u) {
        INV_CHECK(f, INV_MESH_NORMALS_OMITTED, o,
                  nmo_mesh_normals_are_derived(m) && !m->zero_normals_stored,
                  "flags 0x%x but normals are stored", (unsigned)m->flags);
    }
    /* SetFlags(Dword & 0x7FE39A) on load; every bit the engine sets itself is
       one of VXMESH_ALLFLAGS. */
    INV_CHECK(f, INV_MESH_FLAG_BITS, o, (m->flags & ~MESH_FLAGS_KNOWN) == 0u,
              "flags 0x%x", (unsigned)m->flags);

    /* sanity */
    for (uint32_t i = 0; i < m->vertex_count; ++i) {
        const nmo_vertex_t *v = &m->vertices[i];
        INV_CHECK(f, INV_MESH_FINITE, o,
                  is_finite3(&v->position) && is_finite3(&v->normal) &&
                      isfinite(v->uv.x) && isfinite(v->uv.y),
                  "vertex %u has a non-finite value", (unsigned)i);
    }
    for (uint32_t c = 0; c < m->material_channel_count; ++c) {
        const nmo_material_channel_t *channel = &m->material_channels[c];
        for (uint32_t i = 0; i < channel->uv_count; ++i) {
            INV_CHECK(f, INV_MESH_FINITE, o,
                      isfinite(channel->uv_coords[i].x) && isfinite(channel->uv_coords[i].y),
                      "channel %u uv %u is not finite", (unsigned)c, (unsigned)i);
        }
    }
}

static void check_patchmesh(const file_ctx_t *f, const nmo_object_t *o,
                            const nmo_patchmesh_state_t *p)
{
    /* RCKPatchMesh::Load sets m_VertCount = total - m_VecCount and points
       m_Vecs at m_Verts[m_VertCount]; SetVertVecCount takes two counts. */
    INV_CHECK(f, INV_PATCH_VEC_COUNT, o,
              p->vec_count >= 0 && (uint32_t)p->vec_count <= p->total_count,
              "%d vectors of %u", (int)p->vec_count, (unsigned)p->total_count);
    if (p->vec_count < 0 || (uint32_t)p->vec_count > p->total_count) {
        return;
    }
    const uint32_t vec_count = (uint32_t)p->vec_count;
    const uint32_t vert_count = p->total_count - vec_count;

    /* RCKPatchMesh::ComputePatchAux / ComputePatchInteriors / BuildRenderMesh
       read m_Verts[v[j]] for j < type and m_Vecs[vec[j]], m_Vecs[interior[j]]
       without a check; the patch record is the 40 bytes of shorts
       v[4] vec[8] interior[4] aux[4] after type and smoothing group. */
    for (uint32_t i = 0; i < p->patch_count; ++i) {
        const nmo_patchmesh_patch_record_t *record = &p->patches[i];
        const uint32_t type = record->patch.type;
        INV_CHECK(f, INV_PATCH_TYPE, o, type == 3u || type == 4u,
                  "patch %u type %u", (unsigned)i, (unsigned)type);
        check_ref_class(f, INV_PATCH_MATERIAL, o, &record->material, NMO_CID_MATERIAL,
                        "patch material");
        if (type != 3u && type != 4u) {
            continue;
        }
        const uint8_t *raw = record->patch.data;
        uint16_t shorts[20];
        memcpy(shorts, raw, sizeof(shorts));
        for (uint32_t j = 0; j < type; ++j) {
            INV_CHECK(f, INV_PATCH_CORNER_INDEX, o, shorts[j] < vert_count,
                      "patch %u corner %u = %u, %u vertices", (unsigned)i,
                      (unsigned)j, (unsigned)shorts[j], (unsigned)vert_count);
            INV_CHECK(f, INV_PATCH_VEC_INDEX, o, shorts[12u + j] < vec_count,
                      "patch %u interior %u = %u, %u vectors", (unsigned)i,
                      (unsigned)j, (unsigned)shorts[12u + j], (unsigned)vec_count);
        }
        for (uint32_t j = 0; j < 2u * type; ++j) {
            INV_CHECK(f, INV_PATCH_VEC_INDEX, o, shorts[4u + j] < vec_count,
                      "patch %u vec %u = %u, %u vectors", (unsigned)i, (unsigned)j,
                      (unsigned)shorts[4u + j], (unsigned)vec_count);
        }
    }

    /* BuildRenderMesh walks the edge list: v1 at +0 and v2 at +6 name corner
       vertices, +8 and +10 the two patches (the second is -1 on a boundary). */
    for (uint32_t i = 0; i < p->edge_count; ++i) {
        int16_t edge[6];
        memcpy(edge, p->edge_data + 12u * i, sizeof(edge));
        const int ok = edge[0] >= 0 && (uint32_t)edge[0] < vert_count &&
                       edge[3] >= 0 && (uint32_t)edge[3] < vert_count &&
                       edge[4] >= 0 && (uint32_t)edge[4] < p->patch_count &&
                       edge[5] >= -1 && edge[5] < (int)p->patch_count;
        INV_CHECK(f, INV_PATCH_EDGE_INDEX, o, ok,
                  "edge %u = (%d, %d, %d, %d, %d, %d)", (unsigned)i, edge[0], edge[1],
                  edge[2], edge[3], edge[4], edge[5]);
    }

    /* A texture patch is four indices into its channel's coordinate list. */
    for (uint32_t c = 0; c < p->channel_count; ++c) {
        const nmo_patchmesh_channel_t *channel = &p->channels[c];
        for (uint32_t i = 0; i < channel->patch_count; ++i) {
            uint16_t tv[4];
            memcpy(tv, channel->patches_raw + 8u * i, sizeof(tv));
            for (uint32_t j = 0; j < 4u; ++j) {
                INV_CHECK(f, INV_PATCH_TV_INDEX, o, tv[j] < channel->uv_count,
                          "channel %u tv patch %u index %u of %u uvs", (unsigned)c,
                          (unsigned)i, (unsigned)tv[j], (unsigned)channel->uv_count);
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* CK3dEntity                                                                */
/* ------------------------------------------------------------------------ */

static int parent_chain_reaches(const file_ctx_t *f, const nmo_object_t *start,
                                const nmo_object_t *target)
{
    const nmo_object_t *current = start;
    const size_t limit = nmo_object_repository_get_count(f->repo) + 1u;
    for (size_t steps = 0; steps < limit && current != NULL; ++steps) {
        const nmo_3dentity_state_t *e = entity_of(current);
        if (e == NULL) {
            return 0;
        }
        ref_kind_t kind;
        current = ref_lookup(f, &e->parent, &kind);
        if (current == target) {
            return 1;
        }
    }
    return current != NULL;   /* ran out of steps: a cycle that skips target */
}

static int list_has_ref(const nmo_array_t *array, const nmo_object_t *wanted,
                        const file_ctx_t *f, size_t ref_offset)
{
    for (size_t i = 0; i < nmo_array_size(array); ++i) {
        const char *element = (const char *)nmo_array_get(array, i);
        if (ref_names(f, (const nmo_ref_t *)(element + ref_offset), wanted)) {
            return 1;
        }
    }
    return 0;
}

static void check_entity(const file_ctx_t *f, const nmo_object_t *o, const nmo_3dentity_state_t *e)
{
    /* sanity: the world matrix feeds the bounding box and every child matrix
       (RCK3dEntity::WorldMatrixChanged). RCK3dEntity::Save writes three rows
       and a position and Load rebuilds the fourth column as (0, 0, 0, 1), so
       the affine column cannot be wrong in a file. A CKCurvePoint's rotation
       is never read by the curve (it samples the positions), and the engine
       itself stores 0xFFC00000 (the x87 "indefinite" NaN of a zero-length
       normalize) there (Set Projection.cmo), so it is exempt. */
    INV_CHECK(f, INV_ENT_POSITION_FINITE, o,
              isfinite(e->world_matrix[12]) && isfinite(e->world_matrix[13]) &&
                  isfinite(e->world_matrix[14]),
              "position (%g, %g, %g)", (double)e->world_matrix[12],
              (double)e->world_matrix[13], (double)e->world_matrix[14]);
    if (o->class_id != NMO_CID_CURVEPOINT) {
        int finite = 1;
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                finite = finite && isfinite(e->world_matrix[4 * row + column]);
            }
        }
        INV_CHECK(f, INV_ENT_ROTATION_FINITE, o, finite, "rotation has a non-finite value");
    }

    /* RCK3dEntity::ModifyMoveableFlags and Show keep VX_MOVEABLE_VISIBLE and
       VX_MOVEABLE_HIERARCHICALHIDE in step with the CKObject visibility
       (CK_OBJECT_VISIBLE, CK_OBJECT_HIERACHICALHIDE); Load recomputes both
       from the object. */
    {
        const uint32_t visibility = e->base.base.base.base.visibility_flags;
        INV_CHECK(f, INV_ENT_VISIBLE_SYNC, o,
                  ((e->moveable_flags & VX_MOVEABLE_VISIBLE) != 0u) ==
                      ((visibility & NMO_CKOBJECT_VISIBLE) != 0u),
                  "moveable flags 0x%x, object visibility 0x%x", (unsigned)e->moveable_flags,
                  (unsigned)visibility);
        INV_CHECK(f, INV_ENT_HIDE_SYNC, o,
                  ((e->moveable_flags & VX_MOVEABLE_HIERARCHICALHIDE) != 0u) ==
                      ((visibility & NMO_CKOBJECT_HIERARCHICAL) != 0u),
                  "moveable flags 0x%x, object visibility 0x%x", (unsigned)e->moveable_flags,
                  (unsigned)visibility);
    }

    ref_kind_t parent_kind;
    const nmo_object_t *parent = ref_lookup(f, &e->parent, &parent_kind);
    if (parent_kind == REF_FOUND) {
        /* RCK3dEntity::SetParent takes a CK3dEntity, returns 0 for the entity
           itself and refuses a parent that is a descendant of the entity. */
        INV_CHECK(f, INV_ENT_PARENT_CLASS, o, class_is(f, parent, NMO_CID_3DENTITY),
                  "parent is object %u of class %u", (unsigned)parent->file_id,
                  (unsigned)parent->class_id);
        INV_CHECK(f, INV_ENT_PARENT_NOT_SELF, o, parent != o, "parent is the entity");
        INV_CHECK(f, INV_ENT_PARENT_ACYCLIC, o, !parent_chain_reaches(f, parent, o),
                  "parent chain returns to the entity");
    } else if (parent_kind == REF_EXTERNAL) {
        f->stats->inv[INV_ENT_PARENT_CLASS].external++;
    }

    /* RCK3dEntity::Save sets PLACEVALID/PARENTVALID from the place and parent
       it holds, writes each only then, and ZORDERVALID with a non-zero
       priority; CKSceneGraphNode::SetPriority clamps to +-10000. */
    if (e->has_entityndata_chunk) {
        INV_CHECK(f, INV_ENT_PARENT_FLAG, o,
                  ((e->entity_flags & CK_3DENTITY_PARENTVALID) != 0) ==
                      (e->parent.state != NMO_REF_NONE),
                  "flags 0x%x, parent state %d", (unsigned)e->entity_flags,
                  (int)e->parent.state);
        INV_CHECK(f, INV_ENT_PLACE_FLAG, o,
                  ((e->entity_flags & CK_3DENTITY_PLACEVALID) != 0) ==
                      (e->place.state != NMO_REF_NONE),
                  "flags 0x%x, place state %d", (unsigned)e->entity_flags,
                  (int)e->place.state);
        INV_CHECK(f, INV_ENT_ZORDER_FLAG, o,
                  ((e->entity_flags & CK_3DENTITY_ZORDERVALID) != 0) == (e->z_order != 0),
                  "flags 0x%x, z order %d", (unsigned)e->entity_flags, (int)e->z_order);
        INV_CHECK(f, INV_ENT_ZORDER_RANGE, o, e->z_order >= -10000 && e->z_order <= 10000,
                  "z order %d", (int)e->z_order);
    }

    /* RCK3dEntity::SetParent -> UpdatePlace: an entity under a CKPlace is in
       that place, an entity under another entity takes that entity's place.
       RCK3dEntity::Load reads the stored place and drops it. */
    check_ref_class(f, INV_ENT_PLACE_CLASS, o, &e->place, NMO_CID_PLACE, "place");
    {
        ref_kind_t place_kind;
        const nmo_object_t *place = ref_lookup(f, &e->place, &place_kind);
        const nmo_3dentity_state_t *parent_entity = parent ? entity_of(parent) : NULL;
        if (parent_kind == REF_NONE) {
            INV_CHECK(f, INV_ENT_PLACE_DERIVED, o, place_kind == REF_NONE,
                      "place set without a parent");
        } else if (parent_kind == REF_FOUND && place_kind != REF_EXTERNAL) {
            const nmo_object_t *expected = NULL;
            ref_kind_t expected_kind = REF_FOUND;
            if (class_is(f, parent, NMO_CID_PLACE)) {
                expected = parent;
            } else if (parent_entity != NULL) {
                expected = ref_lookup(f, &parent_entity->place, &expected_kind);
            }
            if (expected_kind != REF_EXTERNAL) {
                INV_CHECK(f, INV_ENT_PLACE_DERIVED, o, place == expected,
                          "place %u, parent %u (place %u)",
                          place ? (unsigned)place->file_id : 0u,
                          (unsigned)parent->file_id,
                          expected ? (unsigned)expected->file_id : 0u);
            }
        }
    }

    /* RCK3dEntity::SetCurrentMesh(CKMesh*), AddMesh(CKMesh*), and Load adds the
       current mesh to the list (SetCurrentMesh(m, 1)); a CKCurve's Save skips
       the mesh section (its line mesh is rebuilt). */
    check_ref_class(f, INV_ENT_MESH_CLASS, o, &e->current_mesh, NMO_CID_MESH, "current mesh");
    for (uint32_t i = 0; i < e->mesh_count; ++i) {
        check_ref_class(f, INV_ENT_MESH_CLASS, o, &e->mesh_ids[i], NMO_CID_MESH, "mesh list");
    }
    if (e->current_mesh.state != NMO_REF_NONE) {
        ref_kind_t mesh_kind;
        const nmo_object_t *current = ref_lookup(f, &e->current_mesh, &mesh_kind);
        if (mesh_kind == REF_FOUND) {
            int listed = 0;
            for (uint32_t i = 0; i < e->mesh_count; ++i) {
                listed = listed || ref_names(f, &e->mesh_ids[i], current);
            }
            INV_CHECK(f, INV_ENT_MESH_LISTED, o, listed, "current mesh %u not in the list",
                      (unsigned)current->file_id);
        }
    }
    if (o->class_id == NMO_CID_CURVE) {
        INV_CHECK(f, INV_ENT_CURVE_NO_MESH, o,
                  e->mesh_count == 0u && e->current_mesh.state == NMO_REF_NONE &&
                      !e->has_mesh_chunk,
                  "curve with %u meshes", (unsigned)e->mesh_count);
    }
    /* AddMesh and AddObjectAnimation use XObjectPointerArray::AddIfNotHere, as
       does Load. */
    for (uint32_t i = 0; i < e->mesh_count; ++i) {
        ref_kind_t kind;
        const nmo_object_t *mesh = ref_lookup(f, &e->mesh_ids[i], &kind);
        if (kind == REF_FOUND) {
            int repeats = 0;
            for (uint32_t j = i + 1u; j < e->mesh_count; ++j) {
                repeats = repeats || ref_names(f, &e->mesh_ids[j], mesh);
            }
            INV_CHECK(f, INV_ENT_MESH_UNIQUE, o, !repeats, "mesh %u listed twice",
                      (unsigned)mesh->file_id);
        }
    }
    /* RCK3dEntity::AddObjectAnimation(CKObjectAnimation*) */
    for (uint32_t i = 0; i < e->animation_count; ++i) {
        check_ref_class(f, INV_ENT_ANIM_CLASS, o, &e->animation_ids[i],
                        NMO_CID_OBJECTANIMATION, "animation list");
        ref_kind_t kind;
        const nmo_object_t *animation = ref_lookup(f, &e->animation_ids[i], &kind);
        if (kind != REF_FOUND || animation->class_id != NMO_CID_OBJECTANIMATION) {
            continue;
        }
        int repeats = 0;
        for (uint32_t j = i + 1u; j < e->animation_count; ++j) {
            repeats = repeats || ref_names(f, &e->animation_ids[j], animation);
        }
        INV_CHECK(f, INV_ENT_ANIM_UNIQUE, o, !repeats, "animation %u listed twice",
                  (unsigned)animation->file_id);
    }

    if (e->skin != NULL) {
        const nmo_3dentity_skin_t *skin = e->skin;
        /* RCKSkin bone data holds CK3dEntity pointers (CheckPreDeletion). */
        for (uint32_t i = 0; i < skin->bone_count; ++i) {
            check_ref_class(f, INV_SKIN_BONE_CLASS, o, &skin->bones[i].bone,
                            NMO_CID_3DENTITY, "skin bone");
        }
        ref_kind_t mesh_kind;
        const nmo_object_t *mesh_object = ref_lookup(f, &e->current_mesh, &mesh_kind);
        if (mesh_kind == REF_FOUND && mesh_object->class_id == NMO_CID_MESH) {
            const nmo_mesh_state_t *mesh = (const nmo_mesh_state_t *)nmo_object_get_state(mesh_object);
            if (mesh != NULL) {
                /* RCK3dEntity::UpdateSkin skins nothing when the skin has fewer
                   vertices than the mesh; PreSave adds PROCEDURALPOS to the
                   mesh of a skinned entity. */
                INV_CHECK(f, INV_SKIN_COVERS_MESH, o, skin->vertex_count >= mesh->vertex_count,
                          "skin %u vertices, mesh %u", (unsigned)skin->vertex_count,
                          (unsigned)mesh->vertex_count);
                INV_CHECK(f, INV_SKIN_MESH_PROCEDURAL, o,
                          (mesh->flags & VXMESH_PROCEDURALPOS) != 0u,
                          "skinned mesh flags 0x%x", (unsigned)mesh->flags);
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Cameras and lights                                                        */
/* ------------------------------------------------------------------------ */

static void check_target(const file_ctx_t *f, const nmo_object_t *o, const nmo_ref_t *target_ref,
                         invariant_id_t class_id_inv, const char *what)
{
    ref_kind_t kind;
    const nmo_object_t *target = ref_lookup(f, target_ref, &kind);
    if (kind == REF_NONE) {
        return;
    }
    if (kind == REF_EXTERNAL) {
        f->stats->inv[class_id_inv].external++;
        return;
    }
    /* RCKTargetCamera/RCKTargetLight::SetTarget(CK3dEntity*) takes an entity
       and ignores the object itself. (It also sets TARGETCAMERA/TARGETLIGHT
       and clears FRAME on the target, but Load does not call it and a target
       shared by several lights loses the flag when one of them retargets, so
       the flags are not an invariant of a stored file.) */
    INV_CHECK(f, class_id_inv, o, class_is(f, target, NMO_CID_3DENTITY) && target != o,
              "%s is object %u of class %u", what, (unsigned)target->file_id,
              (unsigned)target->class_id);
}

static void check_camera(const file_ctx_t *f, const nmo_object_t *o, const nmo_camera_state_t *c)
{
    /* RCKCamera::SetProjectionType takes CK_CAMERA_PROJECTION (1 perspective,
       2 orthographic); ComputeProjectionMatrix tests the low bit. */
    INV_CHECK(f, INV_CAM_PROJECTION, o,
              c->projection_type == CK_PERSPECTIVEPROJECTION ||
                  c->projection_type == CK_ORTHOGRAPHICPROJECTION,
              "projection %u", (unsigned)c->projection_type);
    const int perspective = (c->projection_type & 1u) != 0u;
    /* sanity: ComputeProjectionMatrix -> VxMatrix::Perspective(fov, aspect,
       front, back) / Orthographic(zoom, aspect, front, back). */
    if (perspective) {
        INV_CHECK(f, INV_CAM_FOV, o, c->fov > 0.0f && c->fov < 3.14159265f,
                  "fov %g", (double)c->fov);
    } else {
        INV_CHECK(f, INV_CAM_ORTHO_ZOOM, o, c->orthographic_zoom > 0.0f,
                  "orthographic zoom %g", (double)c->orthographic_zoom);
    }
    INV_CHECK(f, INV_CAM_PLANES, o,
              isfinite(c->near_plane) && isfinite(c->far_plane) &&
                  c->near_plane < c->far_plane && (!perspective || c->near_plane > 0.0f),
              "planes %g .. %g", (double)c->near_plane, (double)c->far_plane);
    /* RCKCamera::Load unpacks two 16-bit halves; the projection divides
       width by height. */
    INV_CHECK(f, INV_CAM_ASPECT, o,
              c->aspect_width >= 1 && c->aspect_width <= 65535 &&
                  c->aspect_height >= 1 && c->aspect_height <= 65535,
              "aspect %d / %d", (int)c->aspect_width, (int)c->aspect_height);
}

static void check_light(const file_ctx_t *f, const nmo_object_t *o, const nmo_light_state_t *l)
{
    const nmo_light_data_t *d = &l->light_data;
    /* RCKLight::Load replaces a type outside point..directional by point and
       the decoder does the same, so the byte the file stores is checked. */
    if (l->has_raw_light_data) {
        const uint32_t stored_type = l->raw_type_dword & 0xFFu;
        INV_CHECK(f, INV_LIGHT_TYPE, o, stored_type >= VX_LIGHTPOINT && stored_type <= VX_LIGHTDIREC,
                  "stored type %u", (unsigned)stored_type);
    }
    /* RCKLight::Save writes the power section (CK_STATESAVE_LIGHTDATA2) only
       when the power is not 1, and Load sets 1 without the section. */
    if (!l->light_data_is_legacy) {
        INV_CHECK(f, INV_LIGHT_POWER_CHUNK, o,
                  (l->has_light_power_chunk != 0) == (l->light_power != 1.0f),
                  "power %g, section %d", (double)l->light_power, (int)l->has_light_power_chunk);
    }
    /* sanity: D3DLIGHT range and attenuation are non-negative; the engine
       passes m_LightData to the rasterizer as it is (RCKLight::Setup). */
    INV_CHECK(f, INV_LIGHT_RANGE, o, isfinite(d->range) && d->range >= 0.0f, "range %g",
              (double)d->range);
    INV_CHECK(f, INV_LIGHT_ATTENUATION, o,
              d->attenuation0 >= 0.0f && d->attenuation1 >= 0.0f &&
                  d->attenuation2 >= 0.0f && isfinite(d->attenuation0) &&
                  isfinite(d->attenuation1) && isfinite(d->attenuation2),
              "attenuation %g %g %g", (double)d->attenuation0, (double)d->attenuation1,
              (double)d->attenuation2);
    if (d->type == VX_LIGHTSPOT) {
        /* sanity: D3DLIGHT requires 0 <= theta (hot spot) <= phi (fall off)
           <= pi; Save writes the cones, in that order, only for spot lights. */
        INV_CHECK(f, INV_LIGHT_SPOT_CONES, o,
                  d->inner_spot_cone >= 0.0f && d->inner_spot_cone <= d->outer_spot_cone &&
                      d->outer_spot_cone <= 3.14159265f && d->falloff >= 0.0f,
                  "cones %g .. %g, falloff %g", (double)d->inner_spot_cone,
                  (double)d->outer_spot_cone, (double)d->falloff);
    }
}

/* ------------------------------------------------------------------------ */
/* Curves                                                                    */
/* ------------------------------------------------------------------------ */

static void check_curve(const file_ctx_t *f, const nmo_object_t *o, const nmo_curve_state_t *c)
{
    /* RCKCurve::AddControlPoint/InsertControlPoint take CKCurvePoint*, refuse
       a point already in the list, and call RCKCurvePoint::SetCurve, which
       stores the curve and makes it the point's parent (SetParent). */
    for (uint32_t i = 0; i < c->control_point_count; ++i) {
        const nmo_ref_t *ref = &c->control_point_ids[i];
        check_ref_class(f, INV_CURVE_POINT_CLASS, o, ref, NMO_CID_CURVEPOINT, "control point");
        ref_kind_t kind;
        const nmo_object_t *point = ref_lookup(f, ref, &kind);
        if (kind != REF_FOUND) {
            continue;
        }
        int repeats = 0;
        for (uint32_t j = i + 1u; j < c->control_point_count; ++j) {
            repeats = repeats || ref_names(f, &c->control_point_ids[j], point);
        }
        INV_CHECK(f, INV_CURVE_POINT_UNIQUE, o, !repeats, "point %u listed twice",
                  (unsigned)point->file_id);
        if (point->class_id == NMO_CID_CURVEPOINT) {
            const nmo_curvepoint_state_t *ps = (const nmo_curvepoint_state_t *)nmo_object_get_state(point);
            if (ps != NULL) {
                INV_CHECK(f, INV_CURVE_POINT_BACKREF, o, ref_names(f, &ps->curve, o),
                          "point %u names another curve", (unsigned)point->file_id);
                INV_CHECK(f, INV_CURVE_POINT_PARENT, o, ref_names(f, &ps->base.parent, o),
                          "point %u has another parent", (unsigned)point->file_id);
            }
        }
    }
    /* sanity: RCKCurve::UpdateMesh samples the curve in m_StepCount steps. */
    INV_CHECK(f, INV_CURVE_STEP_COUNT, o, c->step_count >= 1u && c->step_count <= 0x7FFFFFFFu,
              "step count %u", (unsigned)c->step_count);
    /* RCKCurve::Open/Close store 1 and 0 (the constructor 1). */
    INV_CHECK(f, INV_CURVE_OPEN_FLAG, o, c->opened <= 1u, "opened %u", (unsigned)c->opened);
}

static void check_curvepoint(const file_ctx_t *f, const nmo_object_t *o,
                             const nmo_curvepoint_state_t *p)
{
    check_ref_class(f, INV_POINT_CURVE_CLASS, o, &p->curve, NMO_CID_CURVE, "curve");
    ref_kind_t kind;
    const nmo_object_t *curve = ref_lookup(f, &p->curve, &kind);
    /* RCKCurvePoint::SetCurve makes the curve the point's parent, and
       RemapDependencies (the end of a copy, which keeps m_Curve) does too. A
       copied point is not added to the curve's list, so the curve need not
       list the point (SoundIsland.cmo has such copies). */
    if (kind == REF_FOUND) {
        INV_CHECK(f, INV_POINT_PARENT, o, ref_names(f, &p->base.parent, curve),
                  "curve %u is not the parent", (unsigned)curve->file_id);
    }
    /* RCKCurvePoint::UseTCB stores (arg == 0), so m_UseTCB is 0 or 1. */
    INV_CHECK(f, INV_POINT_TANGENT_MODE, o, p->tangent_mode == 0 || p->tangent_mode == 1,
              "tangent mode %d", (int)p->tangent_mode);
    INV_CHECK(f, INV_POINT_FINITE, o,
              isfinite(p->tension) && isfinite(p->continuity) && isfinite(p->bias) &&
                  is_finite3(&p->tangent_in) && is_finite3(&p->tangent_out),
              "tension %g continuity %g bias %g", (double)p->tension, (double)p->continuity,
              (double)p->bias);
}

/* ------------------------------------------------------------------------ */
/* Characters                                                                */
/* ------------------------------------------------------------------------ */

static void check_character(const file_ctx_t *f, const nmo_object_t *o, const nmo_character_state_t *c)
{
    /* RCKCharacter::AddBodyPart(CKBodyPart*) refuses a part already in the
       list, stores the character in the part and removes the part from the
       character it was in before. */
    for (size_t i = 0; i < nmo_array_size(&c->body_parts); ++i) {
        const nmo_character_part_t *part = (const nmo_character_part_t *)nmo_array_get(&c->body_parts, i);
        check_ref_class(f, INV_CHAR_PART_CLASS, o, &part->ref, NMO_CID_BODYPART, "body part");
        ref_kind_t kind;
        const nmo_object_t *object = ref_lookup(f, &part->ref, &kind);
        if (kind == REF_FOUND) {
            int repeats = 0;
            for (size_t j = i + 1u; j < nmo_array_size(&c->body_parts); ++j) {
                const nmo_character_part_t *later = (const nmo_character_part_t *)nmo_array_get(&c->body_parts, j);
                repeats = repeats || ref_names(f, &later->ref, object);
            }
            INV_CHECK(f, INV_CHAR_PART_UNIQUE, o, !repeats, "body part %u listed twice",
                      (unsigned)object->file_id);
        }
        if (kind == REF_FOUND && object->class_id == NMO_CID_BODYPART) {
            const nmo_bodypart_state_t *bp = (const nmo_bodypart_state_t *)nmo_object_get_state(object);
            if (bp != NULL) {
                INV_CHECK(f, INV_CHAR_PART_BACKREF, o,
                          bp->has_character && ref_names(f, &bp->character, o),
                          "body part %u names another character", (unsigned)object->file_id);
            }
        }
    }
    /* RCKCharacter::SetRootBodyPart / AddBodyPart make the root's character
       this one. AddBodyPart also lists the part, and makes a part without a
       parent the root and a child of the character (SetRootBodyPart does
       neither). */
    {
        ref_kind_t kind;
        const nmo_object_t *root = ref_lookup(f, &c->root_body_part, &kind);
        check_ref_class(f, INV_CHAR_ROOT_CLASS, o, &c->root_body_part, NMO_CID_BODYPART, "root");
        if (kind == REF_FOUND && root->class_id == NMO_CID_BODYPART) {
            const nmo_bodypart_state_t *bp = (const nmo_bodypart_state_t *)nmo_object_get_state(root);
            if (bp != NULL) {
                INV_CHECK(f, INV_CHAR_ROOT_OWNER, o,
                          bp->has_character && ref_names(f, &bp->character, o),
                          "root %u names another character", (unsigned)root->file_id);
            }
            INV_CHECK(f, INV_CHAR_ROOT_LISTED, o,
                      list_has_ref(&c->body_parts, root, f, offsetof(nmo_character_part_t, ref)),
                      "root %u is not a listed body part", (unsigned)root->file_id);
            const nmo_3dentity_state_t *root_entity = entity_of(root);
            if (root_entity != NULL) {
                INV_CHECK(f, INV_CHAR_ROOT_PARENT, o, ref_names(f, &root_entity->parent, o),
                          "root %u is not a child of the character", (unsigned)root->file_id);
            }
        }
    }
    /* RCKCharacter::AddAnimation(CKAnimation*) lists the animation once and
       stores the character in it. */
    for (size_t i = 0; i < nmo_array_size(&c->animations); ++i) {
        const nmo_ref_t *ref = (const nmo_ref_t *)nmo_array_get(&c->animations, i);
        check_ref_class(f, INV_CHAR_ANIM_CLASS, o, ref, NMO_CID_ANIMATION, "animation");
        ref_kind_t kind;
        const nmo_object_t *animation = ref_lookup(f, ref, &kind);
        if (kind == REF_FOUND) {
            int repeats = 0;
            for (size_t j = i + 1u; j < nmo_array_size(&c->animations); ++j) {
                repeats = repeats || ref_names(f, (const nmo_ref_t *)nmo_array_get(&c->animations, j), animation);
            }
            INV_CHECK(f, INV_CHAR_ANIM_UNIQUE, o, !repeats, "animation %u listed twice",
                      (unsigned)animation->file_id);
        }
        if (kind == REF_FOUND && class_is(f, animation, NMO_CID_ANIMATION)) {
            const nmo_animation_state_t *as = (const nmo_animation_state_t *)nmo_object_get_state(animation);
            if (as != NULL) {
                INV_CHECK(f, INV_CHAR_ANIM_BACKREF, o,
                          as->has_character && ref_names(f, &as->character, o),
                          "animation %u names another character", (unsigned)animation->file_id);
            }
        }
    }
    check_ref_class(f, INV_CHAR_ACTIVE_CLASS, o, &c->active_animation, NMO_CID_ANIMATION,
                    "active animation");
    check_ref_class(f, INV_CHAR_ACTIVE_CLASS, o, &c->anim_dest, NMO_CID_ANIMATION,
                    "destination animation");
    /* RCKCharacter::SetFloorReferenceObject(CK3dEntity*) */
    check_ref_class(f, INV_CHAR_FLOOR_CLASS, o, &c->floor_ref, NMO_CID_3DENTITY, "floor reference");
}

static void check_bodypart(const file_ctx_t *f, const nmo_object_t *o, const nmo_bodypart_state_t *b)
{
    /* RCKBodyPart::Save to a file always writes the character section (a null
       character is still written) and the joint exactly when IKJOINTVALID is
       set. */
    INV_CHECK(f, INV_PART_SECTION, o,
              b->has_character &&
                  (b->has_rotation_joint != 0) ==
                      ((b->base.entity.entity_flags & CK_3DENTITY_IKJOINTVALID) != 0u),
              "character section %d, joint stored %d, flags 0x%x", (int)b->has_character,
              (int)b->has_rotation_joint, (unsigned)b->base.entity.entity_flags);
    check_ref_class(f, INV_PART_CHARACTER_CLASS, o, &b->character, NMO_CID_CHARACTER, "character");
    ref_kind_t kind;
    const nmo_object_t *character = ref_lookup(f, &b->character, &kind);
    if (kind == REF_FOUND && character->class_id == NMO_CID_CHARACTER) {
        const nmo_character_state_t *cs = (const nmo_character_state_t *)nmo_object_get_state(character);
        if (cs != NULL) {
            /* AddBodyPart lists the part and sets its character, RemoveBodyPart
               clears both; SetRootBodyPart sets only the character. */
            const int listed = list_has_ref(&cs->body_parts, o, f, offsetof(nmo_character_part_t, ref));
            const int root = ref_names(f, &cs->root_body_part, o);
            INV_CHECK(f, INV_PART_BACKREF, o, listed || root,
                      "character %u neither lists nor roots on the part",
                      (unsigned)character->file_id);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Places, grids and layers                                                  */
/* ------------------------------------------------------------------------ */

static void check_place(const file_ctx_t *f, const nmo_object_t *o, const nmo_place_state_t *p)
{
    /* The level is the CKLevel that owns the place. */
    check_ref_class(f, INV_PLACE_LEVEL_CLASS, o, &p->level, NMO_CID_LEVEL, "level");
    /* RCKPlace::AddPortal(CKPlace*, CK3dEntity*) accepts only a portal entity
       (CK_3DENTITY_PORTAL) and appends (other place, portal) here and
       (this place, portal) to the other place. */
    for (size_t i = 0; i < nmo_array_size(&p->portals); ++i) {
        const nmo_place_portal_entry_t *entry = (const nmo_place_portal_entry_t *)nmo_array_get(&p->portals, i);
        check_ref_class(f, INV_PLACE_PORTAL_PLACE, o, &entry->place, NMO_CID_PLACE, "portal place");
        check_ref_class(f, INV_PLACE_PORTAL_ENTITY, o, &entry->portal, NMO_CID_3DENTITY, "portal");
        ref_kind_t portal_kind;
        const nmo_object_t *portal = ref_lookup(f, &entry->portal, &portal_kind);
        if (portal_kind == REF_FOUND) {
            const nmo_3dentity_state_t *pe = entity_of(portal);
            if (pe != NULL) {
                INV_CHECK(f, INV_PLACE_PORTAL_ENTITY, o,
                          (pe->entity_flags & CK_3DENTITY_PORTAL) != 0u,
                          "portal %u flags 0x%x", (unsigned)portal->file_id,
                          (unsigned)pe->entity_flags);
            }
        }
        ref_kind_t other_kind;
        const nmo_object_t *other = ref_lookup(f, &entry->place, &other_kind);
        if (other_kind == REF_FOUND && other->class_id == NMO_CID_PLACE) {
            const nmo_place_state_t *os = (const nmo_place_state_t *)nmo_object_get_state(other);
            int mirrored = 0;
            for (size_t j = 0; os != NULL && j < nmo_array_size(&os->portals); ++j) {
                const nmo_place_portal_entry_t *back =
                    (const nmo_place_portal_entry_t *)nmo_array_get(&os->portals, j);
                mirrored = mirrored ||
                           (ref_names(f, &back->place, o) &&
                            back->portal.state == entry->portal.state &&
                            (back->portal.state == NMO_REF_NONE ||
                             ref_names(f, &back->portal, portal)));
            }
            INV_CHECK(f, INV_PLACE_PORTAL_SYMMETRIC, o, mirrored,
                      "place %u does not list the portal back", (unsigned)other->file_id);
        }
    }
}

static void check_grid(const file_ctx_t *f, const nmo_object_t *o, const nmo_grid_state_t *g)
{
    /* RCKGrid::SetDimensions divides by both; RCKLayer allocates
       4 * length * width bytes. */
    INV_CHECK(f, INV_GRID_DIMENSIONS, o, g->width > 0 && g->length > 0, "%d x %d",
              (int)g->width, (int)g->length);
    /* RCKGrid::SetOrientationMode(CK_GRIDORIENTATION): FREE, XZ, XY, YZ */
    INV_CHECK(f, INV_GRID_ORIENTATION, o, g->orientation_mode <= CKGRID_YZ, "orientation %u",
              (unsigned)g->orientation_mode);
    /* RCKGrid::Save writes the reserved word as WriteInt(0) and, for a file,
       the marker WriteInt(1) before the layer list. */
    INV_CHECK(f, INV_GRID_CONSTANTS, o,
              g->reserved_value == 0 && (!g->has_file_flag || g->file_flag == 1),
              "reserved %d, file marker %d", (int)g->reserved_value, (int)g->file_flag);
    /* RCKGrid::AddLayer creates a CKLayer, InitOwner stores the grid in it and
       the layer type must not exist yet (the type is the layer's name). */
    for (size_t i = 0; i < nmo_array_size(&g->layers); ++i) {
        const nmo_grid_layer_t *entry = (const nmo_grid_layer_t *)nmo_array_get(&g->layers, i);
        check_ref_class(f, INV_GRID_LAYER_CLASS, o, &entry->ref, NMO_CID_LAYER, "layer");
        ref_kind_t kind;
        const nmo_object_t *layer = ref_lookup(f, &entry->ref, &kind);
        if (kind != REF_FOUND || layer->class_id != NMO_CID_LAYER) {
            continue;
        }
        const nmo_layer_state_t *ls = (const nmo_layer_state_t *)nmo_object_get_state(layer);
        if (ls != NULL) {
            INV_CHECK(f, INV_GRID_LAYER_BACKREF, o, ref_names(f, &ls->grid, o),
                      "layer %u names another grid", (unsigned)layer->file_id);
        }
        int repeated = 0;
        const char *name = nmo_object_get_name(layer);
        for (size_t j = i + 1u; name != NULL && j < nmo_array_size(&g->layers); ++j) {
            const nmo_grid_layer_t *later = (const nmo_grid_layer_t *)nmo_array_get(&g->layers, j);
            ref_kind_t later_kind;
            const nmo_object_t *other = ref_lookup(f, &later->ref, &later_kind);
            const char *other_name = other ? nmo_object_get_name(other) : NULL;
            repeated = repeated || (other_name != NULL && strcmp(name, other_name) == 0);
        }
        INV_CHECK(f, INV_GRID_LAYER_UNIQUE, o, !repeated, "layer name \"%s\" repeated", name);
    }
}

static void check_layer(const file_ctx_t *f, const nmo_object_t *o, const nmo_layer_state_t *l)
{
    ref_kind_t kind;
    const nmo_object_t *grid = ref_lookup(f, &l->grid, &kind);
    if (kind == REF_FOUND && grid->class_id == NMO_CID_GRID) {
        const nmo_grid_state_t *gs = (const nmo_grid_state_t *)nmo_object_get_state(grid);
        if (gs != NULL) {
            int listed = 0;
            for (size_t i = 0; i < nmo_array_size(&gs->layers); ++i) {
                const nmo_grid_layer_t *entry = (const nmo_grid_layer_t *)nmo_array_get(&gs->layers, i);
                listed = listed || ref_names(f, &entry->ref, o);
            }
            INV_CHECK(f, INV_LAYER_GRID_LISTS, o, listed, "grid %u does not list the layer",
                      (unsigned)grid->file_id);
            /* RCKLayer::Save writes the square array (4 * width * length
               bytes) only for format 0 with a grid. */
            if (l->format == 0 && l->has_square_data) {
                INV_CHECK(f, INV_LAYER_SQUARE_SIZE, o,
                          l->square_data_size == (size_t)4u * (size_t)gs->width * (size_t)gs->length,
                          "%zu bytes for %d x %d", l->square_data_size, (int)gs->width,
                          (int)gs->length);
            }
        }
    }
    INV_CHECK(f, INV_LAYER_FORMAT_DATA, o, l->format == 0 || !l->has_square_data,
              "format %d with square data", (int)l->format);
    /* RCKLayer::Save writes style version 3 to a file; Load accepts 1 and 2
       from older files. The ctor sets flags to 1 and SetVisible only changes
       bit 0. */
    if (l->has_version) {
        INV_CHECK(f, INV_LAYER_VERSION, o, l->version >= 1 && l->version <= 3, "version %d",
                  (int)l->version);
    }
    if (l->has_flags) {
        INV_CHECK(f, INV_LAYER_FLAGS, o, (l->flags & ~1u) == 0u, "flags 0x%x", (unsigned)l->flags);
    }
}

static void check_sprite3d(const file_ctx_t *f, const nmo_object_t *o, const nmo_sprite3d_state_t *s)
{
    if (!s->has_data) {
        return;
    }
    /* RCKSprite3D::SetMode(VXSPRITE3D_TYPE), SetMaterial(CKMaterial*) */
    INV_CHECK(f, INV_SPRITE3D_MODE, o, s->mode <= VXSPRITE3D_ORIENTABLE, "mode %u",
              (unsigned)s->mode);
    check_ref_class(f, INV_SPRITE3D_MATERIAL, o, &s->material, NMO_CID_MATERIAL, "material");
    /* sanity: SetSize/SetOffset/SetUVMapping feed the local bounding box. */
    INV_CHECK(f, INV_SPRITE3D_FINITE, o,
              isfinite(s->half_width) && isfinite(s->half_height) && isfinite(s->offset.x) &&
                  isfinite(s->offset.y) && isfinite(s->uv_rect.left) && isfinite(s->uv_rect.top) &&
                  isfinite(s->uv_rect.right) && isfinite(s->uv_rect.bottom),
              "half size (%g, %g)", (double)s->half_width, (double)s->half_height);
}

/* ------------------------------------------------------------------------ */
/* Walk                                                                      */
/* ------------------------------------------------------------------------ */

/* RCKLayer::Save writes the color and parameter the grid manager keeps for the
   layer's type, and the type is the layer's name, so layers sharing a name in
   one file share both. */
static void check_layer_styles(const file_ctx_t *f, const nmo_object_t *const *layers, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const nmo_layer_state_t *a = (const nmo_layer_state_t *)nmo_object_get_state(layers[i]);
        const char *name_a = nmo_object_get_name(layers[i]);
        if (a == NULL || name_a == NULL) {
            continue;
        }
        for (size_t j = i + 1u; j < count; ++j) {
            const nmo_layer_state_t *b = (const nmo_layer_state_t *)nmo_object_get_state(layers[j]);
            const char *name_b = nmo_object_get_name(layers[j]);
            if (b == NULL || name_b == NULL || strcmp(name_a, name_b) != 0) {
                continue;
            }
            INV_CHECK(f, INV_LAYER_TYPE_STYLE, layers[j],
                      a->color_rgba == b->color_rgba &&
                          memcmp(&a->param_guid, &b->param_guid, sizeof(a->param_guid)) == 0,
                      "\"%s\": color 0x%x vs 0x%x", name_a, (unsigned)a->color_rgba,
                      (unsigned)b->color_rgba);
        }
    }
}

static void check_object(const file_ctx_t *f, const nmo_object_t *o)
{
    const void *state = nmo_object_get_state(o);
    if (state == NULL) {
        return;
    }
    const nmo_3dentity_state_t *entity = entity_of(o);
    if (entity != NULL) {
        check_entity(f, o, entity);
    }
    switch (o->class_id) {
    case NMO_CID_MESH:
        check_mesh(f, o, (const nmo_mesh_state_t *)state);
        break;
    case NMO_CID_PATCHMESH:
        check_mesh(f, o, &((const nmo_patchmesh_state_t *)state)->base);
        check_patchmesh(f, o, (const nmo_patchmesh_state_t *)state);
        break;
    case NMO_CID_CAMERA:
        check_camera(f, o, (const nmo_camera_state_t *)state);
        break;
    case NMO_CID_TARGETCAMERA: {
        const nmo_targetcamera_state_t *tc = (const nmo_targetcamera_state_t *)state;
        check_camera(f, o, &tc->base);
        check_target(f, o, &tc->target, INV_CAM_TARGET_CLASS, "target");
        break;
    }
    case NMO_CID_LIGHT:
        check_light(f, o, (const nmo_light_state_t *)state);
        break;
    case NMO_CID_TARGETLIGHT: {
        const nmo_targetlight_state_t *tl = (const nmo_targetlight_state_t *)state;
        check_light(f, o, &tl->base);
        check_target(f, o, &tl->target, INV_LIGHT_TARGET_CLASS, "target");
        break;
    }
    case NMO_CID_CURVE:
        check_curve(f, o, (const nmo_curve_state_t *)state);
        break;
    case NMO_CID_CURVEPOINT:
        check_curvepoint(f, o, (const nmo_curvepoint_state_t *)state);
        break;
    case NMO_CID_CHARACTER:
        check_character(f, o, (const nmo_character_state_t *)state);
        break;
    case NMO_CID_BODYPART:
        check_bodypart(f, o, (const nmo_bodypart_state_t *)state);
        break;
    case NMO_CID_PLACE:
        check_place(f, o, (const nmo_place_state_t *)state);
        break;
    case NMO_CID_GRID:
        check_grid(f, o, (const nmo_grid_state_t *)state);
        break;
    case NMO_CID_LAYER:
        check_layer(f, o, (const nmo_layer_state_t *)state);
        break;
    case NMO_CID_SPRITE3D:
        check_sprite3d(f, o, (const nmo_sprite3d_state_t *)state);
        break;
    default:
        break;
    }
}

static void check_file(const char *path, void *user)
{
    corpus_semantics_t *stats = (corpus_semantics_t *)user;
    stats->files++;

    nmo_session_t *session = nmo_session_create(stats->ctx);
    if (session == NULL || nmo_session_load_file(session, path, NULL, NULL) != NMO_OK) {
        stats->load_errors++;
        printf("  %s: load failed\n", path);
        nmo_session_destroy(session);
        return;
    }

    nmo_object_repository_t *repository = nmo_session_get_repository(session);
    file_ctx_t file;
    file.stats = stats;
    file.path = path;
    file.repo = repository;
    file.types = nmo_context_get_type_registry(stats->ctx);

    const size_t count = nmo_object_repository_get_count(repository);
    const nmo_object_t **layers = (const nmo_object_t **)calloc(count ? count : 1u, sizeof(*layers));
    size_t layer_count = 0;
    for (size_t i = 0; i < count; i++) {
        const nmo_object_t *object = nmo_object_repository_get_by_index(repository, i);
        if (object == NULL) {
            continue;
        }
        check_object(&file, object);
        if (object->class_id == NMO_CID_LAYER && layers != NULL) {
            layers[layer_count++] = object;
        }
    }
    if (layers != NULL) {
        check_layer_styles(&file, layers, layer_count);
    }
    free(layers);

    nmo_session_destroy(session);
}

TEST(corpus_semantics_geometry, decoded_values_satisfy_engine_guarantees)
{
    /* Corpus scan: skip when the gitignored Virtools sample set is absent. */
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    corpus_semantics_t *stats = (corpus_semantics_t *)calloc(1, sizeof(*stats));
    ASSERT_NOT_NULL(stats);
    stats->ctx = ctx;
    int walk_status = test_corpus_walk(NMO_TEST_DATA_DIR, check_file, stats);
    nmo_context_release(ctx);

    size_t vacuous = 0;
    size_t violations = 0;
    printf("  Corpus semantics (geometry): files=%zu load_errors=%zu\n", stats->files,
           stats->load_errors);
    for (int i = 0; i < INV_COUNT; ++i) {
        const invariant_counts_t *c = &stats->inv[i];
        printf("    %-64s checked=%-9zu violated=%-6zu external=%zu%s\n", invariant_text[i],
               c->checked, c->violated, c->external, c->checked == 0 ? "  VACUOUS" : "");
        vacuous += c->checked == 0 ? 1u : 0u;
        violations += c->violated;
    }

    size_t load_errors = stats->load_errors;
    size_t files = stats->files;
    free(stats);
    ASSERT_EQ(0, walk_status);
    ASSERT_GE(files, 1u);
    ASSERT_EQ(0u, load_errors);
    ASSERT_EQ(0u, vacuous);
    ASSERT_EQ(0u, violations);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(corpus_semantics_geometry, decoded_values_satisfy_engine_guarantees);
TEST_MAIN_END()
