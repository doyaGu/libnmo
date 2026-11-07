/**
 * @file engine_render.h
 * @brief The render manager and render context as the blocks change them:
 *        what is shown, the attached camera, materials, 2D entities and
 *        lights in their run time state, the background, the fog, the
 *        screen mode. The host draws (nmo_engine_render.h).
 */

#ifndef NMO_ENGINE_RENDER_H_INTERNAL
#define NMO_ENGINE_RENDER_H_INTERNAL

#include "engine_base.h"

#include "core/nmo_math.h"
#include "engine/nmo_engine_render.h"

struct nmo_material_state;
struct nmo_sprite3d_state;
struct eng_matrix;
struct eng_vector;

/* A mesh made at run time (TT Sky's), with what CKMesh would hold */
typedef struct eng_mesh {
    nmo_object_id_t id;
    int32_t vertex_count;
    nmo_vertex_t *vertices;
    uint32_t *colors;
    int32_t face_count;
    uint16_t *indices;
    nmo_face_t *faces;
    int32_t material_count;
    nmo_object_id_t *materials;
    bool prelit;
} eng_mesh_t;

/*
 * A file mesh as it is at run time: its vertices (with the normals CK
 * rebuilds on load when the file stored none) and colors, once a block
 * changed them or the normals had to be built.
 */
typedef struct eng_mesh_copy {
    nmo_object_id_t mesh;
    nmo_vertex_t *vertices;
    uint32_t *colors;   /* NULL: the file's */
    bool prelit;
    uint32_t version;   /* bumped by each change */
    bool geometry_changed;
    nmo_vector_t bary_center;
    float radius;
    /* Its material channels: the file's, then the ones blocks added; each channel's
       uvs (owned) are NULL when it uses the mesh's own */
    ENG_VEC(nmo_engine_channel_t) channels;
    /* Its faces and material groups once a block changed them (ApplyGlobalMaterial,
       ReplaceMaterial); NULL: the file's. Each group is its material, 0 for none. */
    nmo_face_t *faces;
    nmo_object_id_t *groups;
    int32_t group_count;
    uint32_t flags; /* VXMESH_FLAGS as blocks set them (wrap, forced transparency) */
} eng_mesh_copy_t;

/* A screen mode of the engine's one render driver (4:3, in 16 and 32 bits) */
typedef struct eng_screen_mode {
    int32_t width, height, bpp;
} eng_screen_mode_t;

#define ENG_SCREEN_MODE_COUNT 6
extern const eng_screen_mode_t eng_screen_modes[ENG_SCREEN_MODE_COUNT];

typedef struct eng_camera_target {
    nmo_object_id_t id, target;
} eng_camera_target_t;

/* A camera's runtime projection members, initialized from the file on first write. */
typedef struct eng_camera_state {
    nmo_object_id_t id;
    float fov, orthographic_zoom, near_plane, far_plane;
    uint32_t projection_type;
} eng_camera_state_t;

/* A CKMaterial once a block changed it or the host asked for it: the file's, then the changes */
typedef struct eng_material {
    nmo_object_id_t id;
    nmo_engine_material_t desc;
} eng_material_t;

/* A CK2dEntity as CK2_3D keeps it (RCK2dEntity's members) */
typedef struct eng_2d {
    nmo_object_id_t id;
    uint32_t flags;          /* CK_2DENTITY_FLAGS */
    nmo_rect_t rect;         /* m_Rect, in render context pixels */
    nmo_rect_t homogeneous;  /* m_HomogeneousRect, with CK_2DENTITY_USEHOMOGENEOUSCOORD */
    nmo_rect_t source;       /* m_SourceRect */
    int32_t z_order;
    nmo_object_id_t material;
    nmo_object_id_t parent;  /* 0: a child of the background or foreground root */
    nmo_rect_t extents;      /* m_VtxPos, as UpdateExtents left it */
    nmo_rect_t source_extents; /* m_SrcRect */
    bool destroyed;
} eng_2d_t;

/* A CKLight's state the blocks change: its color, activity and VxLight data, and a target light's target */
typedef struct eng_light {
    nmo_object_id_t id;
    float color[4];
    bool active;
    int32_t type;          /* VXLIGHT_TYPE */
    float range;
    float attenuation[3];  /* constant, linear, quadratic */
    float hot_spot;        /* the inner cone (InnerSpotCone) */
    float fall_off;        /* the outer cone (OuterSpotCone) */
    float fall_off_shape;  /* Falloff */
    bool specular;
    nmo_object_id_t target; /* CKTargetLight's; 0 for none */
} eng_light_t;

/* CKSprite3D keeps size in its local box; signed extents are preserved. */
typedef struct eng_sprite3d {
    nmo_object_id_t id, material;
    uint32_t mode;
    nmo_vector2_t min, max, offset;
    nmo_rect_t uv_rect;
} eng_sprite3d_t;

/* An image of a texture's slot, decoded the first time the host asks for it */
typedef struct eng_image {
    nmo_object_id_t texture;
    int32_t slot;
    int32_t width, height;
    uint8_t *rgba;
    uint32_t version;
} eng_image_t;

typedef struct eng_render {
    int32_t screen_mode;      /* the render context's mode, of the engine's driver's list */

    /* CKObject::Show at run time: object -> its CK_OBJECT_VISIBLE and HIERACHICALHIDE flags */
    eng_map_t shown;
    ENG_VEC(eng_material_t) materials;
    eng_map_t material_map;
    ENG_VEC(eng_2d_t) entities_2d;
    eng_map_t entity_2d_map;
    size_t entities_2d_modules; /* the modules whose 2D entities are in entities_2d */
    ENG_VEC(eng_light_t) lights;
    eng_map_t light_map;
    ENG_VEC(eng_sprite3d_t) sprites_3d;
    eng_map_t sprite_3d_map;
    ENG_VEC(eng_image_t) images;
    ENG_VEC(eng_image_t) user_mipmaps; /* a texture's saved mipmap levels, `slot` their level */
    uint32_t image_version;

    /* The render context (CKRenderContext / CKRenderedScene) */
    nmo_object_id_t camera; /* AttachViewpointToCamera */
    ENG_VEC(eng_camera_state_t) camera_states;
    ENG_VEC(eng_camera_target_t) camera_targets;
    uint32_t background_color; /* the background material's diffuse and texture */
    nmo_object_id_t background_texture;
    int32_t fog_mode;
    uint32_t fog_color;
    float fog_start, fog_end, fog_density;
    uint32_t ambient_light;

    ENG_VEC(eng_mesh_t) meshes;
    ENG_VEC(nmo_engine_vertex_t) vertices; /* the frame's triangles */
    ENG_VEC(eng_mesh_copy_t) mesh_copies;
    uint32_t mesh_version;

    /* What nmo_engine_render_frame() handed out last */
    ENG_VEC(nmo_engine_draw_t) draws;
    ENG_VEC(nmo_engine_light_t) frame_lights;
} eng_render_t;

/* core/render.c */
/* The render context as the level scene sets it up (CKScene::ApplyEnvironmentSettings) */
void eng_render_apply_scene(nmo_engine_t *engine, uint32_t background_color,
                            nmo_object_id_t background_texture, uint32_t ambient_light,
                            int32_t fog_mode, uint32_t fog_color, float fog_start,
                            float fog_end, float fog_density, nmo_object_id_t camera);
void eng_free_render(nmo_engine_t *engine);
/* The attached camera's world matrix, or the render context's default eye. */
void eng_render_viewpoint(nmo_engine_t *engine, struct eng_matrix *out);
/* CKRenderContext::Transform of a world point into render context pixels. */
void eng_render_project(nmo_engine_t *engine, const struct eng_vector *world, struct eng_vector *screen);
/* The selected camera's inverse-world times projection, and its near-plane origin. */
bool eng_camera_view_projection(nmo_engine_t *engine, nmo_object_id_t camera,
    struct eng_matrix *out, struct eng_vector *near_origin);
/* VxTransformBox2D: normalized screen bounds, including native near-plane expansion. */
bool eng_project_box(const struct eng_matrix *matrix, const struct eng_vector *min,
    const struct eng_vector *max, nmo_rect_t *out);
/* The engine ID an object's reference names, in the object's document */
nmo_object_id_t eng_object_ref(const nmo_engine_t *engine, nmo_object_id_t owner,
                               const nmo_ref_t *ref);
/* CKObject::Show (CKSHOW, CKHIDE or CKHIERARCHICALHIDE); a group shows its objects too. */
void eng_show(nmo_engine_t *engine, nmo_object_id_t id, int32_t show);
/* The object's CK_OBJECT_VISIBLE and HIERACHICALHIDE flags, and setting them on it alone */
int32_t eng_object_visibility(const nmo_engine_t *engine, nmo_object_id_t id);
void eng_set_object_visibility(nmo_engine_t *engine, nmo_object_id_t id, int32_t flags);
/* The material as it is now, made from the file's the first time; NULL for no CKMaterial */
eng_material_t *eng_material(nmo_engine_t *engine, nmo_object_id_t id);
/* Decode a material's saved members, including references in its module. */
void eng_material_from_state(const nmo_engine_t *engine, nmo_object_id_t id,
                              const struct nmo_material_state *state, nmo_engine_material_t *out);
/* A material made at run time (CreateObject(CKCID_MATERIAL)), with CKMaterial's defaults */
eng_material_t *eng_material_new(nmo_engine_t *engine, nmo_object_id_t id);
/* The 2D entity drawn no more: the object was destroyed */
void eng_2d_destroy(nmo_engine_t *engine, nmo_object_id_t id);
/* The 2D entity's run time state; NULL for no CK2dEntity */
eng_2d_t *eng_2d_entity(nmo_engine_t *engine, nmo_object_id_t id);
eng_light_t *eng_light(nmo_engine_t *engine, nmo_object_id_t id);
/* CKTargetLight::SetTarget: the light points at `target` (0 for none), its flags moved over; false for no target light */
bool eng_set_light_target(nmo_engine_t *engine, nmo_object_id_t light, nmo_object_id_t target);
eng_sprite3d_t *eng_sprite3d(nmo_engine_t *engine, nmo_object_id_t id);
void eng_sprite3d_from_state(const nmo_engine_t *engine, nmo_object_id_t id,
    const struct nmo_sprite3d_state *state, eng_sprite3d_t *out);
bool eng_sprite3d_set_size(nmo_engine_t *engine, nmo_object_id_t id, nmo_vector2_t size);
void eng_copy_sprite3d(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
    const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count);
/* CKTargetCamera's live attachment; writes do not modify the document. */
nmo_object_id_t eng_camera_target(nmo_engine_t *engine, nmo_object_id_t camera);
bool eng_set_camera_target(nmo_engine_t *engine, nmo_object_id_t camera, nmo_object_id_t target);
/* RCKTargetCamera::Load stores the reference directly, leaving target flags alone. */
bool eng_restore_camera_target(nmo_engine_t *engine, nmo_object_id_t camera, nmo_object_id_t target);
void eng_camera_forget_object(nmo_engine_t *engine, nmo_object_id_t id);
void eng_copy_camera_target(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target,
    const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count);
/* Camera projection members now; reads fall back to the file without allocating. */
bool eng_camera_projection_type(nmo_engine_t *engine, nmo_object_id_t id, uint32_t *projection);
bool eng_set_camera_projection_type(nmo_engine_t *engine, nmo_object_id_t id, uint32_t projection);
bool eng_camera_fov(nmo_engine_t *engine, nmo_object_id_t id, float *fov);
bool eng_set_camera_fov(nmo_engine_t *engine, nmo_object_id_t id, float fov);
bool eng_camera_orthographic_zoom(nmo_engine_t *engine, nmo_object_id_t id, float *zoom);
bool eng_set_camera_orthographic_zoom(nmo_engine_t *engine, nmo_object_id_t id, float zoom);
void eng_copy_camera_state(nmo_engine_t *engine, nmo_object_id_t source, nmo_object_id_t target);
/* A camera's front and back planes now (Set Clipping Planes', else the file's); false for none */
bool eng_camera_planes(nmo_engine_t *engine, nmo_object_id_t id, float *near_plane,
                       float *far_plane);
void eng_set_camera_planes(nmo_engine_t *engine, nmo_object_id_t id, float near_plane,
                           float far_plane);
/* A texture's image changed (Texture Load, a slot change): decode it again */
void eng_texture_changed(nmo_engine_t *engine, nmo_object_id_t texture);

eng_mesh_t *eng_render_mesh(nmo_engine_t *engine, nmo_object_id_t id);
/* A file mesh's run time copy, made the first time a block changes it; NULL for no CKMesh */
eng_mesh_copy_t *eng_mesh_copy(nmo_engine_t *engine, nmo_object_id_t mesh);
/* Look up a mesh copy without allocating one. */
eng_mesh_copy_t *eng_mesh_copy_find(const nmo_engine_t *engine, nmo_object_id_t mesh);
/* CKMesh::GetChannelByMaterial; -1 for none */
int32_t eng_mesh_channel(const eng_mesh_copy_t *copy, nmo_object_id_t material);
/* CKMesh::AddChannel(material, FALSE): UVs of its own, every face; -1 out of memory */
int32_t eng_mesh_add_channel(eng_mesh_copy_t *copy, nmo_object_id_t material);
void eng_mesh_remove_channel(eng_mesh_copy_t *copy, int32_t channel);
/* CKMesh::ApplyGlobalMaterial: every face in the group of `material`, made when missing.
   False when `mesh` is none. */
bool eng_mesh_apply_material(nmo_engine_t *engine, nmo_object_id_t mesh, nmo_object_id_t material);
/* CKMesh::ReplaceMaterial: the group of `old_material` takes `new_material` (its faces move
   to that group when it is the first, and to the first when there is no new material). */
bool eng_mesh_replace_material(nmo_engine_t *engine, nmo_object_id_t mesh,
                               nmo_object_id_t old_material, nmo_object_id_t new_material);
/* CKMesh::SetLitMode (VX_PRELITMESH 0, VX_LITMESH 1). False when `mesh` is none. */
bool eng_mesh_set_lit_mode(nmo_engine_t *engine, nmo_object_id_t mesh, int32_t mode);
/* Add a draw to the frame being made; NULL out of memory */
nmo_engine_draw_t *eng_render_push(nmo_engine_t *engine);
/* A texture with a transparent color: RCKTexture::SetAsCurrent tests alpha > 0 with it */
bool eng_texture_keyed(const nmo_engine_t *engine, nmo_object_id_t texture);

#endif /* NMO_ENGINE_RENDER_H_INTERNAL */
