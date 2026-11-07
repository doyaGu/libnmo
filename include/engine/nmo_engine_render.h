/**
 * @file nmo_engine_render.h
 * @brief What the engine's render context would draw, for a host to draw it.
 */

#ifndef NMO_ENGINE_RENDER_H
#define NMO_ENGINE_RENDER_H

#include "engine/nmo_engine.h"
#include "object/nmo_object_struct_defs.h"

#define NMO_ENGINE_RENDER_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_ENGINE_RENDER_API_TIER NMO_API_TIER_ADVANCED_C

/*
 * The engine keeps the render state the blocks change, as CK2_3D keeps it:
 * what is shown (CKObject::Show), the camera the render context looks
 * through, materials, 2D entities, lights, the background and the fog. A
 * host draws it: nmo_engine_render_frame() walks the scene the way
 * CKRenderedScene::Draw does and lists what to draw in order, and the
 * meshes, materials and textures it names are read with the functions
 * below. Nothing here rasterizes.
 *
 * The render context is the size of the screen mode (640x480 until TT
 * Change ScreenMode changes it); 2D positions are in its pixels, and a host
 * scales the whole frame to its window. Colors are ARGB, as VxColor packs
 * them. Matrices are 16 floats, VxMatrix's rows.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum nmo_engine_draw_kind {
    NMO_ENGINE_DRAW_MESH = 0,     /* a 3D entity's current mesh, at its world matrix */
    NMO_ENGINE_DRAW_QUAD = 1,     /* a 2D entity, or a character of text */
    NMO_ENGINE_DRAW_TRIANGLES = 2, /* world space triangles a particle system draws */
    NMO_ENGINE_DRAW_SPRITE_TEXT = 3, /* a CKSpriteText: text a host draws with a system font */
    NMO_ENGINE_DRAW_SPRITE3D = 4,  /* a CKSprite3D: a world space quad with its material, unlit */
    NMO_ENGINE_DRAW_LINES = 5,     /* world space lines a particle system draws */
    NMO_ENGINE_DRAW_PRIMITIVE = 6  /* world space triangles or lines a render callback draws */
} nmo_engine_draw_kind_t;

/* A vertex of NMO_ENGINE_DRAW_TRIANGLES, _LINES and _PRIMITIVE: world position, texture
   coordinates, ARGB color, and the world normal a lit primitive has */
typedef struct nmo_engine_vertex {
    float x, y, z;
    float u, v;
    uint32_t color;
    float nx, ny, nz;
} nmo_engine_vertex_t;

/* The stencil test a draw renders with, as the render context's VXRENDERSTATE_STENCIL* states
   were when it was made: VXCMP `func`, VXSTENCILOP `fail`, `zfail` and `pass` */
typedef struct nmo_engine_stencil {
    bool enabled;
    int32_t func, fail, zfail, pass;
    uint32_t ref, mask, write_mask;
} nmo_engine_stencil_t;

/*
 * Something to draw. A mesh uses its materials; a quad uses `material`
 * (CKMaterial::SetAsCurrent, vertex color its diffuse), or when it has
 * none, `texture` alpha blended (srcalpha, invsrcalpha), clamped and
 * modulated by `color` at its top and `color2` at its bottom: a character
 * of text, whose top edge leans by `skew` pixels (the font's italic).
 */
typedef struct nmo_engine_draw {
    nmo_engine_draw_kind_t kind;
    nmo_object_id_t object; /* the 3D or 2D entity; 0 for text */
    nmo_object_id_t mesh;
    float world[16];
    float rect[4];          /* quad: left, top, right, bottom in render context pixels */
    float uv[4];            /* quad: its texture coordinates, the same way */
    nmo_object_id_t material;
    nmo_object_id_t texture;
    uint32_t color, color2;
    float skew;
    bool nearest;           /* text of a font with Disable Filter */
    bool background;        /* a background 2D entity: no depth test or write */
    bool transparent;       /* a mesh drawn after the opaque ones, back to front */
    bool no_z_test, no_z_write; /* VX_MOVEABLE_NOZBUFFERTEST / NOZBUFFERWRITE */
    bool z_only;            /* VX_MOVEABLE_ZBUFONLY: depth written, no color */
    /* A mesh drawn with a projection of its own (TT Sky's): fov > 0 */
    float fov, near_plane, far_plane;
    /* Triangles: the frame's vertices from first_vertex, textured with `texture`
       (modulated, clamped), blended source_blend/dest_blend (VXBLEND_MODE)
       unless `blend` is false, no lighting, no culling, alpha tested (alpha > 0)
       when `alpha_test`; world[12..14] is their emitter's position, to sort by.
       Lines: pairs of the frame's vertices from first_vertex, untextured, blended
       the same way, no z write */
    int32_t first_vertex, vertex_count;
    bool alpha_test;
    /* A 3D sprite: two triangles of the frame's vertices from first_vertex, colored with its
       material's diffuse, drawn with `material` and no lighting */
    /* Triangles, and a quad with no material that sets `blend` (Planar Filter, a sprite): its
       blending; such a quad is alpha tested (alpha > 0) too when `alpha_test` (a transparent
       sprite). A sprite's quad textures `texture`, the sprite itself, with nearest filtering. */
    int32_t source_blend, dest_blend;
    bool blend;
    /* A primitive (a block's DrawPrimitive in a render callback): triangles of the frame's
       vertices from first_vertex, or pairs of them when `lines`, drawn with `material` set as
       current (none: untextured and opaque) in the vertices' colors without lighting, or lit
       with their normals when `lit`; lines are untextured. No culling when `two_sided`. A
       render context's post render callback's are `post`: drawn after the transparent draws,
       in order. A primitive with `projection_z_scale` (not 0) is drawn with the projection's
       depth translation (VxMatrix [3][2]) scaled by it: a block's trick against z fighting
       (Mark System's 1.001). One with `no_fog` is drawn without the fog
       (VXRENDERSTATE_FOGENABLE off). */
    bool lines, lit, two_sided, post;
    float projection_z_scale;
    bool no_fog;
    /* A mesh with `fade` (a trail Motion Blur draws in its render callback) draws its
       materials alpha blended (srcalpha, invsrcalpha) at `fade_alpha` times their diffuse
       alpha. A transparent draw with `has_sort_position` sorts at `sort_position` instead of
       world[12..14]: all the draws an entity's render callback makes sort where it is. */
    bool fade;
    float fade_alpha;
    bool has_sort_position;
    float sort_position[3];
    /* A 3D draw renders with `stencil` (Planar Reflection's and Planar Shadow's plane masks).
       A mesh with `stencil_only` (VX_MOVEABLE_STENCILONLY) writes 1 to every stencil bit where
       it passes a less-or-equal depth test, and no color or depth. One `mirrored` has its
       winding inverted (VX_MOVEABLE_INDIRECTMATRIX: a reflection). A primitive with no
       material is blended source_blend/dest_blend when `blend`, without z write when
       `no_z_write`. */
    nmo_engine_stencil_t stencil;
    bool stencil_only;
    bool mirrored;
    /* A mesh `blackened` draws its materials with black diffuse, emissive and ambient
       (ShadowCaster's shadow) */
    bool blackened;
    /* Sprite text: `text` in `rect`, in `color` on nothing (the sprite's transparent
       color), with the font `font_name` of `font_size` (points; pixels when negative,
       as CreateFont takes it), `font_weight` and `italic`, placed by `text_align`
       (CKSPRITETEXT_ALIGNMENT); valid until the next frame */
    const char *text;
    const char *font_name;
    int32_t font_size, font_weight;
    bool italic;
    uint32_t text_align;
    /* A Place's viewport clipping: normalized left, top, right, bottom.
       Hosts clip this draw to that part of the render viewport when clipped. */
    bool clipped;
    float clip_rect[4];
} nmo_engine_draw_t;

typedef struct nmo_engine_light {
    nmo_object_id_t id;
    int32_t type;          /* VXLIGHT_TYPE: 1 point, 2 spot, 3 directional */
    float color[4];        /* r, g, b, a: the color times the power */
    float position[3];
    float direction[3];    /* the light's Z axis */
    float range;
    float attenuation[3];  /* constant, linear, quadratic */
    float inner_cone, outer_cone, falloff;
    bool specular;
} nmo_engine_light_t;

typedef struct nmo_engine_pass nmo_engine_pass_t;

/* A frame: the view, then what to draw (the engine's until the next call). */
typedef struct nmo_engine_frame {
    int32_t width, height;   /* the render context */
    nmo_object_id_t camera;  /* the attached camera; 0 for none */
    float view[16];          /* the eye's world matrix */
    bool orthographic;
    float fov;               /* horizontal, radians */
    float zoom;
    float near_plane, far_plane;
    uint32_t background_color;
    nmo_object_id_t background_texture;
    int32_t fog_mode;        /* VXFOG_MODE: 0 none, 1 exp, 2 exp2, 3 linear */
    uint32_t fog_color;
    float fog_start, fog_end, fog_density;
    uint32_t ambient_light;
    const nmo_engine_light_t *lights;
    int32_t light_count;
    const nmo_engine_draw_t *draws;
    int32_t draw_count;
    const nmo_engine_vertex_t *vertices; /* what triangle draws use */
    /* The render context's options (CK_RENDER_FLAGS: CK_RENDER_CLEARBACK clears the color
       buffer, the sprite flags are applied already) and global render mode: shading 0 draws
       wireframe, 1 flat, others as the materials say; no textures unless use_textures; a
       wireframe layer over the meshes when wireframe_layer */
    uint32_t render_options;
    int32_t shading;
    bool use_textures, wireframe_layer;
    /* The render passes to draw first, in order (a pass has none of its own) */
    const nmo_engine_pass_t *passes;
    int32_t pass_count;
} nmo_engine_frame_t;

/*
 * A render pass (CKRenderContext::SetRenderTarget): `frame` drawn as a frame
 * is, into `viewport` (left, top, width, height in pixels; the frame is its
 * size) of the texture's slot `slot` (a cube map's face, else 0), width x
 * height: Texture Render's view of the scene, ShadowCaster's shadow. The
 * texture then shows what was drawn, until a pass draws it again. The
 * texture's image the engine keeps does not change (CK leaves the system
 * memory copy as it was).
 */
struct nmo_engine_pass {
    nmo_object_id_t texture;
    int32_t slot;
    int32_t width, height;
    int32_t viewport[4];
    nmo_engine_frame_t frame;
};

/**
 * The frame as the render context draws it now, after the render passes the
 * blocks asked for this frame: background 2D entities,
 * then the visible meshes of the level scene and the particle systems'
 * triangles, then the foreground 2D entities with the text drawn on them,
 * each 2D tree in Z order. A target camera turns to its target first
 * (PrepareCameras).
 */
NMO_API nmo_status_t nmo_engine_render_frame(nmo_engine_t *engine, nmo_engine_frame_t *out);

/** CKObject::IsVisible as the blocks left it. */
NMO_API bool nmo_engine_object_is_visible(const nmo_engine_t *engine, nmo_object_id_t id);

/*
 * A material channel of a mesh (CKMesh::AddChannel): drawn over the mesh's
 * faces with its own material, blended source_blend/dest_blend.
 */
typedef struct nmo_engine_channel {
    nmo_object_id_t material;
    int32_t source_blend, dest_blend; /* VXBLEND_MODE */
    bool active;
    bool lit;          /* not VXCHANNEL_NOTLIT */
    const float *uvs;  /* two a vertex; NULL: the mesh's own */
    uint16_t mask;     /* the faces whose channel_mask has it; 0: every face */
} nmo_engine_channel_t;

typedef struct nmo_engine_mesh {
    int32_t vertex_count;
    const nmo_vertex_t *vertices; /* position, normal, uv */
    const uint32_t *colors;       /* prelit vertex colors (ARGB); NULL for none */
    int32_t face_count;
    const uint16_t *indices;      /* three a face */
    const nmo_face_t *faces;      /* each face's material group */
    int32_t material_count;       /* the material groups */
    bool prelit;                  /* VXMESH_PRELITMODE: colors, not lighting */
    bool wrap_u, wrap_v;
    uint32_t version;             /* changes when blocks change the vertices or colors */
    int32_t channel_count;
    const nmo_engine_channel_t *channels;
} nmo_engine_mesh_t;

/**
 * A mesh as it is now: the file's, with the normals CK rebuilds from the
 * faces when the file saved none, and the UVs and colors blocks changed
 * (Texture Scroller, Set Prelit Color); false when `mesh` is none.
 */
NMO_API bool nmo_engine_mesh(const nmo_engine_t *engine, nmo_object_id_t mesh,
                             nmo_engine_mesh_t *out);
/** The material of a mesh's material group; 0 for none. */
NMO_API nmo_object_id_t nmo_engine_mesh_material(const nmo_engine_t *engine,
                                                 nmo_object_id_t mesh, int32_t group);

/* A material as it is now (CKMaterial's members); enums are VX* values. */
typedef struct nmo_engine_material {
    float diffuse[4], ambient[4], specular[4], emissive[4]; /* r, g, b, a */
    float power;
    nmo_object_id_t texture;
    int32_t texture_blend;  /* VXTEXTURE_BLENDMODE */
    int32_t min_filter, mag_filter;
    int32_t address_mode;
    uint32_t border_color;
    int32_t source_blend, dest_blend;
    int32_t shade_mode, fill_mode;
    bool two_sided;
    bool z_write;
    int32_t z_func;
    bool alpha_blend;
    bool alpha_test;
    int32_t alpha_func;
    uint8_t alpha_ref;
    /* VX_EFFECTTEXGEN the texture coordinates come from (VXEFFECT_TEXGEN, VXEFFECT_TEXGENREF):
       0 the mesh's, 1 transform, 2 reflect, 3 chrome, 4 planar, 5+ cube maps */
    int32_t texgen;
    nmo_object_id_t texgen_referential; /* VXEFFECT_TEXGENREF's 3D entity; 0 for the scene's root */
} nmo_engine_material_t;

/** False when `material` is no CKMaterial. */
NMO_API bool nmo_engine_material(const nmo_engine_t *engine, nmo_object_id_t material,
                                 nmo_engine_material_t *out);

typedef struct nmo_engine_image {
    int32_t width, height;
    const uint8_t *rgba;  /* the engine's until the texture changes */
    uint32_t version;     /* changes when the image does */
    bool mipmapped;       /* CKTexture::GetMipmapCount() != 0: the device builds its mipmaps */
} nmo_engine_image_t;

/**
 * The image of the texture's current slot, RGBA, from the file's own data
 * or the file it names (read through the host's resolver), with its
 * transparent color made transparent. False when there is none to read.
 */
NMO_API bool nmo_engine_texture_image(nmo_engine_t *engine, nmo_object_id_t texture,
                                      nmo_engine_image_t *out);
/** The mipmap levels the texture's file saves (CKTexture's user mipmaps); 0 for none. */
NMO_API int32_t nmo_engine_texture_user_mipmap_count(const nmo_engine_t *engine,
                                                     nmo_object_id_t texture);
/**
 * A saved mipmap level (1 is half the image), RGBA with the transparent
 * color made transparent; false when the texture saves no such level.
 */
NMO_API bool nmo_engine_texture_user_mipmap(nmo_engine_t *engine, nmo_object_id_t texture,
                                            int32_t level, nmo_engine_image_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NMO_ENGINE_RENDER_H */
