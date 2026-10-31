/**
 * @file engine_render.h
 * @brief The render manager and render context as the blocks change them:
 *        what is shown, the attached camera, materials, 2D entities and
 *        lights in their run time state, the background, the fog, the
 *        screen mode and the fonts. The host draws (nmo_engine_render.h).
 */

#ifndef NMO_ENGINE_RENDER_H_INTERNAL
#define NMO_ENGINE_RENDER_H_INTERNAL

#include "engine_base.h"

#include "core/nmo_math.h"
#include "engine/nmo_engine_render.h"

/* Where a character is in a font's texture (CharacterTextureCoordinates), in UVs */
typedef struct eng_glyph {
    float ustart, vstart;
    float uwidth, uprewidth, upostwidth;
    float vwidth;
} eng_glyph_t;

/* A CKTextureFont: its characters in its texture, and how Set Font Properties draws them */
typedef struct eng_font {
    char *name;
    nmo_object_id_t texture; /* the texture it reads, 0 for a system font's own */
    bool created;            /* SpacingProperties CREATED: it draws */
    float char_number[2];
    int32_t first_character;
    nmo_rect_t zone;         /* the part of the texture the characters are in, in pixels */
    float space_percentage;
    eng_glyph_t glyphs[256];
    float leading[2], scale[2], italic;
    uint32_t start_color, end_color, shadow_color;
    float shadow_offset[2], shadow_scale[2];
    nmo_object_id_t material;
    uint32_t properties;     /* FONT_GRADIENT, FONT_SHADOW, FONT_LIGHTING, FONT_DISABLEFILTER */
} eng_font_t;

/*
 * Text a 2D Text block draws this frame: on its 2D entity after the entity
 * (a temporary post render callback), or with Multiple, as it was when the
 * block ran (the font manager's text list, drawn after the sprites).
 */
typedef struct eng_text {
    int32_t node;
    nmo_object_id_t entity;
    bool multiple;
    int32_t font, align, flags;
    char *string;
    nmo_rect_t zone;
    nmo_object_id_t material;
} eng_text_t;

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

/* A file mesh's vertices with the normals CK rebuilds on load (the file stored none) */
typedef struct eng_normals {
    nmo_object_id_t mesh;
    nmo_vertex_t *vertices;
} eng_normals_t;

/* A TT Sky: its mesh around the camera, drawn first with its own projection */
typedef struct eng_sky {
    int32_t node;
    nmo_object_id_t mesh;
    nmo_object_id_t orientation;
    float distortion;
    bool shown;
} eng_sky_t;

/* A screen mode of the engine's one render driver (4:3, in 16 and 32 bits) */
typedef struct eng_screen_mode {
    int32_t width, height, bpp;
} eng_screen_mode_t;

#define ENG_SCREEN_MODE_COUNT 6
extern const eng_screen_mode_t eng_screen_modes[ENG_SCREEN_MODE_COUNT];

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

/* A CKLight's state the blocks change */
typedef struct eng_light {
    nmo_object_id_t id;
    float color[4];
    bool active;
} eng_light_t;

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
    eng_map_t video_textures; /* the textures TT_PreloadTextures put in video memory */
    ENG_VEC(eng_font_t) fonts;

    /* CKObject::Show at run time: object -> its CK_OBJECT_VISIBLE and HIERACHICALHIDE flags */
    eng_map_t shown;
    ENG_VEC(eng_material_t) materials;
    eng_map_t material_map;
    ENG_VEC(eng_2d_t) entities_2d;
    eng_map_t entity_2d_map;
    bool entities_2d_loaded;
    ENG_VEC(eng_light_t) lights;
    eng_map_t light_map;
    ENG_VEC(eng_image_t) images;
    uint32_t image_version;

    /* The render context (CKRenderContext / CKRenderedScene) */
    nmo_object_id_t camera; /* AttachViewpointToCamera */
    uint32_t background_color; /* the background material's diffuse and texture */
    nmo_object_id_t background_texture;
    int32_t fog_mode;
    uint32_t fog_color;
    float fog_start, fog_end, fog_density;
    uint32_t ambient_light;

    ENG_VEC(eng_text_t) texts; /* this frame's */
    ENG_VEC(eng_mesh_t) meshes;
    ENG_VEC(eng_sky_t) skies;
    ENG_VEC(eng_normals_t) normals;

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
/* The engine ID an object's reference names, in the object's document */
nmo_object_id_t eng_object_ref(const nmo_engine_t *engine, nmo_object_id_t owner,
                               const nmo_ref_t *ref);
/* CKObject::Show (CKSHOW, CKHIDE or CKHIERARCHICALHIDE); a group shows its objects too. */
void eng_show(nmo_engine_t *engine, nmo_object_id_t id, int32_t show);
/* The material as it is now, made from the file's the first time; NULL for no CKMaterial */
eng_material_t *eng_material(nmo_engine_t *engine, nmo_object_id_t id);
/* The 2D entity's run time state; NULL for no CK2dEntity */
eng_2d_t *eng_2d_entity(nmo_engine_t *engine, nmo_object_id_t id);
eng_light_t *eng_light(nmo_engine_t *engine, nmo_object_id_t id);
/* A texture's image changed (Texture Load, a slot change): decode it again */
void eng_texture_changed(nmo_engine_t *engine, nmo_object_id_t texture);

/* Forget the text the blocks asked for last frame (the temporary callbacks) */
void eng_render_begin_frame(nmo_engine_t *engine);
eng_mesh_t *eng_render_mesh(nmo_engine_t *engine, nmo_object_id_t id);
/* Add a draw to the frame being made; NULL out of memory */
nmo_engine_draw_t *eng_render_push(nmo_engine_t *engine);

/* blocks/interface.c (the font manager) */
void eng_fonts_clear(nmo_engine_t *engine);
/* The texts 2D Text draws on `entity` (0: the Multiple ones), into the frame */
void eng_draw_texts(nmo_engine_t *engine, nmo_object_id_t entity, float width, float height);

/* blocks/tt/gravity.c */
/* The skies TT Sky shows, placed at the camera: the frame's first draws */
void eng_draw_skies(nmo_engine_t *engine, const float view[16]);

#endif /* NMO_ENGINE_RENDER_H_INTERNAL */
