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
struct nmo_mesh_state;
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

/*
 * An Environment Mapping block's pre-render callback (temporary: the next
 * frame's render runs it once): its inputs when the block ran.
 */
typedef struct eng_env_map {
    nmo_object_id_t entity;
    nmo_object_id_t behavior;
    int32_t channel;
    uint32_t options;              /* 1 invert U, 2 invert V, 4 use the referential's orientation, 8 reflect */
    nmo_object_id_t referential;   /* 0: the render context's camera */
    bool old_version;              /* before 1.5, always the referential's orientation */
} eng_env_map_t;

/*
 * A render callback a block gives a 3D entity: CK3dEntity::AddPreRenderCallBack,
 * SetRenderCallBack (in place of its mesh's draw; one an entity) or
 * AddPostRenderCallBack. The scene traversal runs it as it renders the
 * entity (shown, in the scene, with a mesh or callbacks), with whether the
 * entity is rendered last (its draws then go with the transparent ones,
 * sorted at the entity); a temporary one is removed after the frame. `node`
 * is the block, passed back.
 */
typedef enum eng_entity_callback_kind {
    ENG_ENTITY_PRE_RENDER = 0,
    ENG_ENTITY_RENDER = 1,
    ENG_ENTITY_POST_RENDER = 2
} eng_entity_callback_kind_t;

typedef void (*eng_entity_callback_fn)(nmo_engine_t *engine, nmo_object_id_t entity, int32_t node,
                                       bool transparent);

typedef struct eng_entity_callback {
    nmo_object_id_t entity;
    eng_entity_callback_kind_t kind;
    eng_entity_callback_fn fn;
    int32_t node;
    bool temporary;
} eng_entity_callback_t;

/* A render of the scene into part of the screen this frame (Additional View's temporary render
   callback): from `camera`, into `rect`, with `options`, after the 3D or after everything */
typedef struct eng_view_request {
    int32_t node; /* the block asking: its newer request replaces an older one not drawn yet */
    nmo_object_id_t camera;
    nmo_rect_t rect;
    uint32_t options;
    bool over_2d;
} eng_view_request_t;

typedef struct eng_render {
    int32_t screen_mode;      /* the render context's mode, of the engine's driver's list */
    ENG_VEC(eng_env_map_t) env_maps;
    ENG_VEC(eng_entity_callback_t) entity_callbacks;
    /* The render context's stencil states as callbacks set them, given to each draw made, and
       the stencil bits used since the last stencil clear (m_StencilFreeMask) */
    nmo_engine_stencil_t stencil;
    uint32_t stencil_used;
    /* The frame's render passes, each with the draws from its first; the pass being made (-1:
       none) and the eye a pass draws from (eng_render_viewpoint's while it draws) */
    ENG_VEC(nmo_engine_pass_t) passes;
    ENG_VEC(size_t) pass_firsts;
    int32_t open_pass;
    bool pass_eye_set;
    float pass_eye[16];

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
    /* Its render options and global render mode once a block set them (eng_render_settings) */
    bool settings_set;
    uint32_t render_options;  /* CK_RENDER_FLAGS */
    int32_t shading;          /* VxShadeType: 0 wireframe, 1 flat, 2 Gouraud, ... 4 material default */
    bool use_textures, wireframe_layer;
    bool antialias;           /* VXRENDERSTATE_ANTIALIAS as blocks set it (TT_SetFSAA); off by default */
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

    /* The render context's viewport (m_ViewportData: left, top, width, height): SetViewRect's,
       made the attached camera's ratio at each frame while CK_RENDER_USECAMERARATIO is on; the
       whole render context until either */
    bool viewport_set;
    int32_t viewport[4];
    /* The views asked for this frame, and those made, each with the draws from its first */
    ENG_VEC(eng_view_request_t) view_requests;
    ENG_VEC(nmo_engine_view_t) views;
    ENG_VEC(size_t) view_firsts;
    bool making_views;

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
/* The render context's render options and global render mode, CK's defaults the first time
   (CK_RENDER_DEFAULTSETTINGS, Gouraud shading, textures, no wireframe) */
eng_render_t *eng_render_settings(nmo_engine_t *engine);
/* The attached camera's world matrix, or the render context's default eye. */
void eng_render_viewpoint(nmo_engine_t *engine, struct eng_matrix *out);
/* CKRenderContext::GetViewRect: the viewport now (left, top, width, height) */
void eng_render_viewport(nmo_engine_t *engine, int32_t out[4]);
/* CKRenderContext::SetViewRect: the viewport from now on (its corners truncated to pixels) */
void eng_render_set_view_rect(nmo_engine_t *engine, const nmo_rect_t *rect);
/* A render of the scene into part of the screen once, in the next frame drawn (a temporary
   render callback); none asked while views are being made */
void eng_render_request_view(nmo_engine_t *engine, const eng_view_request_t *request);
/* CKRenderContext::Transform of a world point into render context pixels. */
void eng_render_project(nmo_engine_t *engine, const struct eng_vector *world, struct eng_vector *screen);
/* The selected camera's inverse-world times projection, and its near-plane origin. */
bool eng_camera_view_projection(nmo_engine_t *engine, nmo_object_id_t camera,
    struct eng_matrix *out, struct eng_vector *near_origin);
/* CKRasterizerContext::ComputeBoxVisibility through VxTransformBox2D: 0 when all of the box is
   out of one plane (or it is not a box), 2 when no corner is out of any, else 1; when not 0, its
   normalized screen bounds in `out` (may be NULL). */
int eng_box_visibility(const struct eng_matrix *matrix, const struct eng_vector *min,
    const struct eng_vector *max, nmo_rect_t *out);
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
/* CK2dEntity::Render in a render callback: the entity's draws (when it is shown and in the
   scene), then its children's, pushed onto the frame being made */
void eng_render_2d_entity(nmo_engine_t *engine, nmo_object_id_t id, float width, float height);
/* Every 2D entity of the files loaded so far in render.entities_2d (their children found there) */
void eng_load_2d_entities(nmo_engine_t *engine);
/* CK2dEntity::SetRect: the rect, the homogeneous one with it, and unless `keep_children`
   the children moved along (their STICK flags, or their homogeneous rects); false for no
   2D entity */
bool eng_2d_set_rect(nmo_engine_t *engine, nmo_object_id_t id, nmo_rect_t rect, bool keep_children);
/* CK2dEntity::SetHomogeneousRect: the rect in fractions of the parent's (the render context's) */
bool eng_2d_set_homogeneous_rect(nmo_engine_t *engine, nmo_object_id_t id, nmo_rect_t rect, bool keep_children);
/* CK2dEntity::SetPosition: the top left corner, in homogeneous coordinates or in pixels
   from `ref`'s (0: the render context's) */
bool eng_2d_set_position(nmo_engine_t *engine, nmo_object_id_t id, float x, float y, bool homogeneous,
                         bool keep_children, nmo_object_id_t ref);
/* CK2dEntity::SetSize, the same way */
bool eng_2d_set_size(nmo_engine_t *engine, nmo_object_id_t id, float width, float height, bool homogeneous,
                     bool keep_children);
/* CK2dEntity::SetBackground: a root entity moves to the background or foreground root,
   its children with it; a child stays as its parent has it */
bool eng_2d_set_background(nmo_engine_t *engine, nmo_object_id_t id, bool back);
/* CK2dEntity::SetParent: 0 for none; a background parent makes the entity background,
   another foreground. False for no 2D entity or a parent among its children. */
bool eng_2d_set_parent(nmo_engine_t *engine, nmo_object_id_t id, nmo_object_id_t parent);
eng_light_t *eng_light(nmo_engine_t *engine, nmo_object_id_t id);
/* CKTargetLight::SetTarget: the light points at `target` (0 for none), its flags moved over; false for no target light */
bool eng_set_light_target(nmo_engine_t *engine, nmo_object_id_t light, nmo_object_id_t target);
eng_sprite3d_t *eng_sprite3d(nmo_engine_t *engine, nmo_object_id_t id);
void eng_sprite3d_from_state(const nmo_engine_t *engine, nmo_object_id_t id,
    const struct nmo_sprite3d_state *state, eng_sprite3d_t *out);
bool eng_sprite3d_set_size(nmo_engine_t *engine, nmo_object_id_t id, nmo_vector2_t size);
bool eng_sprite3d_set_offset(nmo_engine_t *engine, nmo_object_id_t id, nmo_vector2_t offset);
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
/* CKMesh::SetFaceMaterial: face `face` in the group of `material`, made when missing. False when
   `mesh` is none or has no such face. */
bool eng_mesh_set_face_material(nmo_engine_t *engine, nmo_object_id_t mesh, int32_t face,
                                nmo_object_id_t material);
/* CKMesh::ReplaceMaterial: the group of `old_material` takes `new_material` (its faces move
   to that group when it is the first, and to the first when there is no new material). */
bool eng_mesh_replace_material(nmo_engine_t *engine, nmo_object_id_t mesh,
                               nmo_object_id_t old_material, nmo_object_id_t new_material);
/* The copy's faces, copied from the file the first time; NULL for a mesh without faces. */
nmo_face_t *eng_mesh_copy_faces(nmo_engine_t *engine, eng_mesh_copy_t *copy);
/* CKMesh::ModifierVertexMove(rebuild_normals, TRUE) once a block moved the copy's vertices: the
   vertex and face normals from the faces when `rebuild_normals`, the bounding sphere, a new version */
void eng_mesh_vertices_moved(nmo_engine_t *engine, eng_mesh_copy_t *copy, bool rebuild_normals);
/* CKMesh::BuildFaceNormals of the copy's vertices */
void eng_mesh_build_face_normals(nmo_engine_t *engine, eng_mesh_copy_t *copy);
/* CKMesh::LoadVertices of the mesh's initial value: the file's vertices and colors back in the
   copy (the vertex normals built when the file has none); the caller then calls
   eng_mesh_vertices_moved() */
void eng_mesh_reset_vertices(nmo_engine_t *engine, eng_mesh_copy_t *copy);
/* The copy's vertex colors, copied from the file the first time; NULL for no vertices */
uint32_t *eng_mesh_copy_colors(nmo_engine_t *engine, eng_mesh_copy_t *copy);
/* The mesh's vertices become `count` taken from `sources` (vertex i from sources[i], a new zeroed
   white one for -1; sources NULL: vertex i from vertex i): positions, normals, UVs (the
   channels' too), colors and weights, in the state and its copy. Faces are left alone. False on
   failure (nothing changed). */
bool eng_mesh_remap_vertices(nmo_engine_t *engine, nmo_object_id_t mesh, const int32_t *sources, int32_t count);
/* CKMesh::SetVertexCount: the vertices kept, new ones zeroed and white; fewer drop the faces and
   lines that lose a vertex */
bool eng_mesh_set_vertex_count(nmo_engine_t *engine, nmo_object_id_t mesh, int32_t count);
/* CKMesh::SetFaceCount: the faces kept, new ones of vertex 0 in material group 0 on every
   channel, with no normal */
bool eng_mesh_set_face_count(nmo_engine_t *engine, nmo_object_id_t mesh, int32_t count);
/* CKMesh::DissociateAllFaces: each face corner a vertex of its own (lines keep their ends);
   nothing past 65536 corners */
bool eng_mesh_dissociate_faces(nmo_engine_t *engine, nmo_object_id_t mesh);
/* CKContext::CopyObject of a mesh, made in `model`'s document so `model` can take it: the
   source's vertices, colors, faces and material groups as they are now (a material of another
   document left out; no channels). 0 on failure */
nmo_object_id_t eng_mesh_duplicate(nmo_engine_t *engine, nmo_object_id_t model, nmo_object_id_t source,
                                   const char *name);
/* CKMesh::GetTextureCoordinatesPtr: the UVs of `channel` (-1 the mesh's own, which a channel
   without UVs of its own shares), two floats every `*stride` bytes for `*count` vertices;
   NULL for no such channel or no vertices. */
float *eng_mesh_uvs(nmo_engine_t *engine, eng_mesh_copy_t *copy, int32_t channel, size_t *stride,
                    int32_t *count);
/* CK3dEntity::AddPreRenderCallBack of Environment Mapping, once per block and entity */
bool eng_add_env_map(nmo_engine_t *engine, const eng_env_map_t *map);
/* Add an entity's render callback: a render one takes the place of the entity's other, a pre
   or post one is added once (entity, kind, function and block). False out of memory. */
bool eng_add_entity_callback(nmo_engine_t *engine, const eng_entity_callback_t *callback);
/* Remove the entity's callbacks of `kind` by `fn` for the block `node` */
void eng_remove_entity_callback(nmo_engine_t *engine, nmo_object_id_t entity, eng_entity_callback_kind_t kind,
                                eng_entity_callback_fn fn, int32_t node);
/* CKMesh::Render of the entity's `mesh` at `world` (rows, as VxMatrix), with the entity's
   moveable flags; drawn with the transparent ones when `transparent`. NULL out of memory. */
nmo_engine_draw_t *eng_render_entity_mesh(nmo_engine_t *engine, nmo_object_id_t entity, nmo_object_id_t mesh,
                                          const float world[16], bool transparent);
/* CKRenderContext::GetFirstFreeStencilBits: the first of the 8 stencil bits unused since the
   last stencil clear; with none left the blocks clear the stencil (Clear(CK_RENDER_CLEARSTENCIL)):
   the engine starts over at bit 0 */
int32_t eng_render_first_free_stencil_bit(nmo_engine_t *engine);
/* CKRenderContext::UsedStencilBits */
void eng_render_use_stencil_bits(nmo_engine_t *engine, uint32_t bits);
/* CK3dEntity::IsHiddenByParent: a parent hides its children (CKHIERARCHICALHIDE) */
bool eng_render_hidden_by_parent(const nmo_engine_t *engine, int32_t index);
/* What a render pass of the scene looks through (Texture Render): `camera`'s projection and
   aspect ratio (none: the defaults, 4:3), the eye at `eye`, the fov `fov` when `has_fov` (a cube
   face's), the background (Clear's background material) and the render options */
typedef struct eng_scene_view {
    nmo_object_id_t camera;
    float eye[16];           /* VxMatrix rows */
    bool has_fov;
    float fov;
    uint32_t background_color;
    nmo_object_id_t background_texture;
    uint32_t render_options;
} eng_scene_view_t;
/* At ENG_RENDER_PASSES: SetRenderTarget(texture, slot) then Render: a pass drawing the scene
   from `view` (a viewport of the camera's ratio with CK_RENDER_USECAMERARATIO); nothing for a
   texture without an image. */
nmo_status_t eng_render_scene_pass(nmo_engine_t *engine, nmo_object_id_t texture, int32_t slot,
                                   const eng_scene_view_t *view);
/* At ENG_RENDER_PASSES: a pass of the caller's own draws into the texture's slot, the whole
   texture its viewport: the frame to set the view of (the main frame's fog, lights and settings
   are there), then the draws made until eng_render_end_pass(); NULL for a texture without an
   image */
nmo_engine_frame_t *eng_render_begin_pass(nmo_engine_t *engine, nmo_object_id_t texture, int32_t slot);
void eng_render_end_pass(nmo_engine_t *engine);
/* CKMesh::SetLitMode (VX_PRELITMESH 0, VX_LITMESH 1). False when `mesh` is none. */
bool eng_mesh_set_lit_mode(nmo_engine_t *engine, nmo_object_id_t mesh, int32_t mode);
/* A soft shadow on the floors under a 3D entity (Simple Shadow, TT Simple Shadow): the floors
   its material's channel is on */
#define ENG_SHADOW_FLOORS 50
typedef struct eng_floor_shadow {
    nmo_object_id_t material;
    nmo_object_id_t floors[ENG_SHADOW_FLOORS];
    int32_t floor_count;
} eng_floor_shadow_t;
/* The shadow's material: white, unlit by the specular and ambient, its texture clamped and
   copied; 0 out of memory */
nmo_object_id_t eng_floor_shadow_material(nmo_engine_t *engine, const char *name);
/* A_GetFloors and the channel UVs: the channel on the floors (Floor attribute, shown, not all
   outside the last frame's view) under `entity` whose height is within `max_height`, `zoom` times
   its width, off the others */
void eng_floor_shadow_place(nmo_engine_t *engine, eng_floor_shadow_t *shadow, nmo_object_id_t entity,
                            float zoom, float max_height);
/* The channel off every floor it is on (the list stays) */
void eng_floor_shadow_remove(nmo_engine_t *engine, eng_floor_shadow_t *shadow);
/* Quads in world space (four corners, UVs each, a color each) as one transparent triangle
   draw of `object`: `texture`, blended source_blend/dest_blend, without the depth test or write */
void eng_render_push_quads(nmo_engine_t *engine, nmo_object_id_t object, const struct eng_vector *corners,
                           const float (*uvs)[2], const uint32_t *colors, int32_t quads,
                           nmo_object_id_t texture, int32_t source_blend, int32_t dest_blend);
/* Add a draw to the frame being made; NULL out of memory */
nmo_engine_draw_t *eng_render_push(nmo_engine_t *engine);
/* CKContext::CreateObject(CKCID_MESH) then SetVertexCount and SetFaceCount: zeroed vertices,
   white vertex colors, no specular, faces of vertex 0 in no material group; the state is the
   caller's to fill before the mesh renders. 0 out of memory */
nmo_object_id_t eng_create_mesh(nmo_engine_t *engine, const char *name, int32_t vertex_count,
                                int32_t face_count, struct nmo_mesh_state **out_state);
/* CK3dEntity::SetCurrentMesh, for a mesh of the entity's own document or of objects made in it
   at run time (a created entity takes a created mesh); 0 for none. False when it cannot refer
   to it. */
bool eng_entity_set_current_mesh(nmo_engine_t *engine, nmo_object_id_t entity, nmo_object_id_t mesh);
/* CK3dEntity::GetMeshCount and GetMesh: the entity's meshes (its file's, then blocks') */
int32_t eng_entity_mesh_count(nmo_engine_t *engine, nmo_object_id_t entity);
nmo_object_id_t eng_entity_mesh_at(nmo_engine_t *engine, nmo_object_id_t entity, int32_t index);
/* CK3dEntity::AddMesh (once; a mesh it can refer to) and RemoveMesh (when it was the current
   one, the first left is, none without one); false when nothing was done */
bool eng_entity_add_mesh(nmo_engine_t *engine, nmo_object_id_t entity, nmo_object_id_t mesh);
bool eng_entity_remove_mesh(nmo_engine_t *engine, nmo_object_id_t entity, nmo_object_id_t mesh);
/* CKMesh::GetBaryCenter and GetRadius, in the mesh's space; false for no mesh */
bool eng_mesh_sphere(nmo_engine_t *engine, nmo_object_id_t mesh, struct eng_vector *center, float *radius);
/* Whether the scene traversal renders the 3D entity: in the scene, shown, and no parent hiding
   its children (its render callbacks run) */
bool eng_render_entity_drawn(nmo_engine_t *engine, nmo_object_id_t id);
/* A texture with a transparent color: RCKTexture::SetAsCurrent tests alpha > 0 with it */
bool eng_texture_keyed(const nmo_engine_t *engine, nmo_object_id_t texture);

#endif /* NMO_ENGINE_RENDER_H_INTERNAL */
