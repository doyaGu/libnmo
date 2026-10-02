/**
 * @file test_corpus_semantics_media.c
 * @brief Decoded media, material and 2D values of the corpus satisfy what the engine guarantees
 *
 * test_corpus_chunk_roundtrip proves that no bytes are lost; it cannot tell a
 * field that was read into the wrong member from a correct one. These checks
 * decode every object of the corpus that belongs to CKMaterial, CKTexture,
 * CKSprite, CK2dEntity, CKSprite3D and CKWaveSound and assert relationships
 * that the engine's setters, loaders or its own Save always satisfy, so a
 * swapped, misread or mis-scaled field shows up as a violation.
 *
 * Every invariant has a counter of the values checked (the test requires it to
 * be non-zero, so a check cannot silently stop applying) and a counter of the
 * violations (which must be zero). The comment at each check names the engine
 * function it follows (CK2_3D.dll for the render classes, CK2.dll for the
 * bitmap and sound classes, DX7SoundManager.dll for what a sound setting reads
 * back as).
 */

#include "../test_framework.h"

#include "core/nmo_arena.h"
#include "core/nmo_color.h"
#include "format/nmo_image.h"
#include "format/nmo_stb_adapter.h"
#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/builtin/nmo_bitmap_slots.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/builtin/nmo_sprite_schemas.h"
#include "object/builtin/nmo_spritetext_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_context.h"
#include "session/nmo_session.h"
#include "type/nmo_type_query.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REPORTED_VIOLATIONS 30

/* X(identifier, description) */
#define INVARIANTS(X) \
    X(MAT_COLORS, "material: the four colors unpack to finite channels in [0,1]") \
    X(MAT_POWER, "material: specular power is finite and not negative") \
    X(MAT_TEXBLEND, "material: texture blend mode is VXTEXTUREBLEND_DECAL..DOTPRODUCT3") \
    X(MAT_FILTER, "material: min and mag filters are VXTEXTUREFILTER_NEAREST..ANISOTROPIC") \
    X(MAT_BLEND, "material: source and dest blend are VXBLEND_ZERO..BOTHINVSRCALPHA") \
    X(MAT_SHADE, "material: shade mode is FLAT or GOURAUD") \
    X(MAT_FILL, "material: fill mode is VXFILL_POINT..SOLID") \
    X(MAT_ADDRESS, "material: address mode is VXTEXTURE_ADDRESSWRAP..MIRRORONCE") \
    X(MAT_ZFUNC, "material: z function is VXCMP_NEVER..ALWAYS") \
    X(MAT_ALPHAFUNC, "material: alpha function is VXCMP_NEVER..ALWAYS") \
    X(MAT_FLAGBITS, "material: only the flag bits the engine's setters touch are set") \
    X(MAT_EFFECT_RANGE, "material: effect fits the six bits SetEffect keeps") \
    X(MAT_EFFECT_PARAM, "material: an effect parameter or flag implies an effect") \
    X(MAT_EXTRA_TEX, "material: textures 1..3 are only present together with an effect") \
    X(MAT_TEX_REFS, "material: every texture reference resolves to a CKTexture") \
    X(MAT_EFFECT_REF, "material: the effect parameter resolves to a CKParameter") \
    X(BMP_NAMES, "bitmap: slots with sections carry the slot file name list") \
    X(BMP_ARRAYS, "bitmap: slot arrays agree with kind and slot count") \
    X(BMP_NONE_NAMES, "bitmap: included-file slots saved without pixels all name a file") \
    X(BMP_READER_HEADER, "bitmap: reader section is 32 bits per pixel with positive size") \
    X(BMP_READER_FORMAT, "bitmap: reader slot format is 0, 1 or 2") \
    X(BMP_READER_DECODE, "bitmap: every reader slot decodes to the stated width x height") \
    X(BMP_READER_ALPHA, "bitmap: reader alpha is one value or a width*height plane") \
    X(BMP_RAW_SHAPE, "bitmap: raw slot is 32 bits per pixel with positive size and canonical masks") \
    X(BMP_RAW_COMPRESSION, "bitmap: raw slot compression is 0 or 1") \
    X(BMP_RAW_PLANES, "bitmap: stored raw planes are width*height bytes") \
    X(BMP_RAW_DECODE, "bitmap: every raw slot reconstructs to width x height pixels") \
    X(TEX_CUBEMAP, "texture: a cube map has six slots") \
    X(TEX_SAVE_OPTIONS, "texture: save options are CKTEXTURE_RAWDATA..INCLUDEORIGINALFILE") \
    X(TEX_KIND_SAVE, "texture: the bitmap section kind follows the save options") \
    X(TEX_MIP_LEVELS, "texture: mipmap level is automatic or fits the size of the image") \
    X(TEX_VIDEO_FORMAT, "texture: effective desired video format is a VX_PIXELFORMAT (<= 27)") \
    X(TEX_VIDEO_FORMAT_SET, "texture: a stored desired video format is not zero") \
    X(TEX_CURRENT_SLOT, "texture: current slot is inside the slot count") \
    X(TEX_USER_MIPS, "texture: user mipmaps halve down to 1 pixel") \
    X(TEX_MOVIE, "texture: a movie texture stores no slot sections") \
    X(E2D_FLAGS, "2d entity: reserved and update flags are cleared on load") \
    X(E2D_HOMOGENEOUS, "2d entity: the homogeneous rect is stored exactly with its flag") \
    X(E2D_FINITE, "2d entity: the pixel rect and source rect are finite") \
    X(E2D_RECT_ORDER, "2d entity: the active finite rectangle has right >= left and bottom >= top") \
    X(E2D_SOURCE_RECT, "2d entity: a stored source rect is not empty, an absent one is the default") \
    X(E2D_ZORDER, "2d entity: z order is stored only when it is not zero") \
    X(E2D_PARENT, "2d entity: the parent is another 2d entity and the chain has no cycle") \
    X(E2D_BACKGROUND, "2d entity: a child is background exactly when its parent is") \
    X(E2D_MATERIAL, "2d entity: only a plain CK2dEntity keeps a material, and it is a CKMaterial") \
    X(SPR_SLOT, "sprite: current slot is inside the slot count of a bitmap sprite") \
    X(SPR_SAVE_OPTIONS, "sprite: save options are CKTEXTURE_RAWDATA..INCLUDEORIGINALFILE") \
    X(SPR_SOURCE_RECT, "sprite: an active source rect lies inside the bitmap") \
    X(SPR_TRANSPARENCY, "sprite: file mode always stores transparency, slot and save options") \
    X(S3D_DATA, "sprite3d: the data section is always stored") \
    X(S3D_MODE, "sprite3d: mode is VXSPRITE3D_BILLBOARD..ORIENTABLE") \
    X(S3D_SIZE, "sprite3d: half width and half height are finite and positive") \
    X(S3D_MAPPING, "sprite3d: offset and uv mapping are finite") \
    X(S3D_MATERIAL, "sprite3d: the material reference resolves to a CKMaterial") \
    X(SND_SAVE_OPTIONS, "wave sound: save options are CKSOUND_EXTERNAL..USEGLOBAL") \
    X(SND_FILE_NAME, "wave sound: the sound names a file") \
    X(SND_SECTIONS, "wave sound: duration and settings sections are always stored") \
    X(SND_DURATION, "wave sound: duration is not negative") \
    X(SND_TYPE, "wave sound: type bits are BACKGROUND, POINT or CONE") \
    X(SND_GAIN, "wave sound: gain is in [0,1]") \
    X(SND_PAN, "wave sound: pan is in [-1,1]") \
    X(SND_PITCH, "wave sound: pitch is finite and positive") \
    X(SND_CONE, "wave sound: cone angles are whole degrees in [0,360] with inner <= outer") \
    X(SND_CONE_GAIN, "wave sound: gain outside the cone is in [0,1]") \
    X(SND_DISTANCE, "wave sound: min and max distance are finite and positive") \
    X(SND_MUTE, "wave sound: mute-after-max is a 16-bit value") \
    X(SND_ATTACHED, "wave sound: the attached object resolves to a CK3dEntity") \
    X(SND_VECTORS, "wave sound: position and direction are finite")

typedef enum invariant_id {
#define X(id, text) INV_##id,
    INVARIANTS(X)
#undef X
    INV_COUNT
} invariant_id_t;

static const char *const invariant_text[INV_COUNT] = {
#define X(id, text) text,
    INVARIANTS(X)
#undef X
};

typedef struct invariant_counter {
    size_t checked;
    size_t violated;
} invariant_counter_t;

typedef struct corpus_semantics {
    nmo_context_t *ctx;
    nmo_arena_t *arena;
    size_t files;
    size_t load_errors;
    size_t reported;
    invariant_counter_t inv[INV_COUNT];

    size_t materials;
    size_t textures;
    size_t reader_slots;
    size_t raw_slots;
    size_t entities;
    size_t sprites;
    size_t sprite3ds;
    size_t wavesounds;
} corpus_semantics_t;

typedef struct site {
    corpus_semantics_t *stats;
    const char *path;
    const nmo_object_t *object;
    const nmo_object_repository_t *repository;
} site_t;

static void invariant_violation(const site_t *site, invariant_id_t id, const char *format, ...)
{
    site->stats->inv[id].violated++;
    if (site->stats->reported++ >= MAX_REPORTED_VIOLATIONS) {
        return;
    }
    printf("  %s: class %u object %u: [%s] ", site->path,
           (unsigned)site->object->class_id, (unsigned)site->object->file_id,
           invariant_text[id]);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

/* Count a checked value; on failure report it (the format arguments are only
   evaluated for a failure). */
#define CHECK(site, id, condition, ...) \
    do { \
        (site)->stats->inv[id].checked++; \
        if (!(condition)) { \
            invariant_violation((site), (id), __VA_ARGS__); \
        } \
    } while (0)

static int in_range_u32(uint32_t value, uint32_t low, uint32_t high)
{
    return value >= low && value <= high;
}

static int finite_rect(const nmo_rect_t *rect)
{
    return isfinite(rect->left) && isfinite(rect->top) &&
           isfinite(rect->right) && isfinite(rect->bottom);
}

static int rect_is_zero(const nmo_rect_t *rect)
{
    return rect->left == 0.0f && rect->top == 0.0f &&
           rect->right == 0.0f && rect->bottom == 0.0f;
}

static const nmo_object_t *find_target(const site_t *site, const nmo_ref_t *ref)
{
    nmo_object_id_t id = nmo_ref_runtime_id(ref);
    return id == NMO_OBJECT_ID_NONE
        ? NULL : nmo_object_repository_find_by_id(site->repository, id);
}

/* A non-empty reference is resolved and names an object of the expected class
   (the loaders mark a wrong class with NMO_REF_CLASS_MISMATCH). */
static int reference_is_class(const site_t *site, const nmo_ref_t *ref, nmo_class_id_t class_id)
{
    const nmo_object_t *target = find_target(site, ref);
    if (ref->state != NMO_REF_RESOLVED || target == NULL) {
        return 0;
    }
    return nmo_type_query_object_is_derived_from_class(
        nmo_context_get_type_registry(site->stats->ctx), target, class_id);
}

static int reference_present(const nmo_ref_t *ref)
{
    return ref->state != NMO_REF_NONE;
}

static int floor_log2(int32_t value)
{
    int log = 0;
    while (value > 1) {
        value >>= 1;
        log++;
    }
    return log;
}

/* ------------------------------------------------------------------------- */
/* CKMaterial                                                                */
/* ------------------------------------------------------------------------- */

static void check_material(const site_t *site, const nmo_material_state_t *m)
{
    site->stats->materials++;

    /* RCKMaterial::Save writes each color through VxColor::GetRGBA (0xAARRGGBB);
       unpacking a stored color must give back channels in [0,1]. The pre-v5
       layout stores floats, converted when read. */
    const uint32_t colors[4] = {m->diffuse_color, m->ambient_color, m->specular_color, m->emissive_color};
    for (int i = 0; i < 4; i++) {
        nmo_color_t c;
        nmo_color_from_argb32(colors[i], &c);
        int ok = isfinite(c.r) && isfinite(c.g) && isfinite(c.b) && isfinite(c.a) &&
                 c.r >= 0.0f && c.r <= 1.0f && c.g >= 0.0f && c.g <= 1.0f &&
                 c.b >= 0.0f && c.b <= 1.0f && c.a >= 0.0f && c.a <= 1.0f;
        CHECK(site, INV_MAT_COLORS, ok, "color %d = %08x", i, (unsigned)colors[i]);
    }

    /* RCKMaterial::SetPower (CK2_3D 0x100664c0) keeps the D3D specular exponent
       as given and switches the specular off below 0.05, so a stored power is a
       small non-negative number; a color word read as a float would not be. */
    CHECK(site, INV_MAT_POWER, isfinite(m->specular_power) && m->specular_power >= 0.0f,
          "specular power %g", (double)m->specular_power);

    /* RCKMaterial::Load (0x100655e1) unpacks m_Texture*Mode, m_SourceBlend,
       m_DestBlend, m_ShadeMode, m_FillMode and m_TextureAddressMode from the
       eight nibbles of the modes word; the setters take the VX enums. */
    const uint32_t blend = m->packed_modes & 0xFu;
    const uint32_t min_filter = (m->packed_modes >> 4) & 0xFu;
    const uint32_t mag_filter = (m->packed_modes >> 8) & 0xFu;
    const uint32_t src_blend = (m->packed_modes >> 12) & 0xFu;
    const uint32_t dst_blend = (m->packed_modes >> 16) & 0xFu;
    const uint32_t shade = (m->packed_modes >> 20) & 0xFu;
    const uint32_t fill = (m->packed_modes >> 24) & 0xFu;
    const uint32_t address = (m->packed_modes >> 28) & 0xFu;
    CHECK(site, INV_MAT_TEXBLEND, in_range_u32(blend, VXTEXTUREBLEND_DECAL, VXTEXTUREBLEND_DOTPRODUCT3),
          "texture blend %u", (unsigned)blend);
    CHECK(site, INV_MAT_FILTER,
          in_range_u32(min_filter, VXTEXTUREFILTER_NEAREST, VXTEXTUREFILTER_ANISOTROPIC) &&
          in_range_u32(mag_filter, VXTEXTUREFILTER_NEAREST, VXTEXTUREFILTER_ANISOTROPIC),
          "min %u mag %u", (unsigned)min_filter, (unsigned)mag_filter);
    CHECK(site, INV_MAT_BLEND,
          in_range_u32(src_blend, VXBLEND_ZERO, VXBLEND_BOTHINVSRCALPHA) &&
          in_range_u32(dst_blend, VXBLEND_ZERO, VXBLEND_BOTHINVSRCALPHA),
          "source %u dest %u", (unsigned)src_blend, (unsigned)dst_blend);
    /* Load: "if (m_ShadeMode > VXSHADE_GOURAUD) m_ShadeMode = VXSHADE_GOURAUD". */
    CHECK(site, INV_MAT_SHADE, in_range_u32(shade, VXSHADE_FLAT, VXSHADE_GOURAUD), "shade %u", (unsigned)shade);
    CHECK(site, INV_MAT_FILL, in_range_u32(fill, VXFILL_POINT, VXFILL_SOLID), "fill %u", (unsigned)fill);
    CHECK(site, INV_MAT_ADDRESS, in_range_u32(address, VXTEXTURE_ADDRESSWRAP, VXTEXTURE_ADDRESSMIRRORONCE),
          "address %u", (unsigned)address);

    /* SetZFunc / SetAlphaFunc keep a VXCMPFUNC; Load turns an alpha function
       of 0 into VXCMP_ALWAYS. The flags word packs zfunc in byte 1 and the
       alpha function in byte 2. */
    const uint32_t zfunc = (m->packed_flags >> 8) & 0xFu;
    const uint32_t alpha_func = (m->packed_flags >> 16) & 0xFu;
    CHECK(site, INV_MAT_ZFUNC, in_range_u32(zfunc, VXCMP_NEVER, VXCMP_ALWAYS), "zfunc %u", (unsigned)zfunc);
    CHECK(site, INV_MAT_ALPHAFUNC, in_range_u32(alpha_func, VXCMP_NEVER, VXCMP_ALWAYS),
          "alpha func %u", (unsigned)alpha_func);

    /* The setters (SetTwoSided, EnableZWrite, EnablePerpectiveCorrection,
       EnableAlphaBlend, EnableAlphaTest) only touch bits 0..4 of the low
       byte; the effect sits in its own six bits (kept apart in the state). */
    CHECK(site, INV_MAT_FLAGBITS, (m->packed_flags & 0xE0u) == 0u && (m->packed_flags & 0xF000u) == 0u &&
          (m->packed_flags & 0xF00000u) == 0u,
          "flags %08x", (unsigned)m->packed_flags);

    /* RCKMaterial::SetEffect: "m_Flags = ((effect & 0x3F) << 8) | ...". */
    CHECK(site, INV_MAT_EFFECT_RANGE, m->effect <= 0x3Fu, "effect %u", (unsigned)m->effect);

    /* RCKMaterial::Save writes MATDATA3 / MATDATA5 and MATDATA2 only inside
       "if (effect)", and SetEffect(0) destroys the effect parameter. */
    int has_param = m->has_effect_param || reference_present(&m->effect_parameter);
    CHECK(site, INV_MAT_EFFECT_PARAM,
          (!has_param && !m->has_effect) || m->effect != 0u,
          "effect parameter/flag without effect (has_effect %d, parameter %d)",
          m->has_effect, has_param);
    int extra_textures = reference_present(&m->textures[1]) || reference_present(&m->textures[2]) ||
                         reference_present(&m->textures[3]);
    CHECK(site, INV_MAT_EXTRA_TEX, !extra_textures || m->effect != 0u,
          "textures 1..3 present with effect 0");

    /* RCKMaterial::Load: "if (CKIsChildClassOf(Object, CKCID_TEXTURE))" for
       slot 0; the others are read as CKTexture. */
    for (size_t i = 0; i < 4; i++) {
        if (reference_present(&m->textures[i])) {
            CHECK(site, INV_MAT_TEX_REFS, reference_is_class(site, &m->textures[i], NMO_CID_TEXTURE),
                  "texture %zu: state %d id %u", i, (int)m->textures[i].state,
                  (unsigned)m->textures[i].raw_id);
        }
    }
    /* SetEffect creates the parameter with CKContext::CreateObject(46, ...). */
    if (reference_present(&m->effect_parameter)) {
        CHECK(site, INV_MAT_EFFECT_REF, reference_is_class(site, &m->effect_parameter, NMO_CID_PARAMETER),
              "effect parameter: state %d", (int)m->effect_parameter.state);
    }
}

/* ------------------------------------------------------------------------- */
/* Bitmaps (CKTexture and CKSprite share CKBitmapData)                       */
/* ------------------------------------------------------------------------- */

static int slot_name_present(const nmo_bitmap_slots_t *b, uint32_t slot)
{
    return b->has_slot_filenames && b->slot_filenames != NULL &&
           b->slot_filenames[slot] != NULL && b->slot_filenames[slot][0] != '\0';
}

/* Returns the size of slot 0 when the bitmap holds pixels, else 0 x 0. */
static void check_bitmap(const site_t *site, const nmo_bitmap_slots_t *b, uint32_t save_options,
                         int32_t *out_width, int32_t *out_height)
{
    corpus_semantics_t *stats = site->stats;
    *out_width = 0;
    *out_height = 0;

    const int has_movie = b->has_movie_filename && b->movie_filename != NULL;
    if (has_movie) {
        return; /* CKBitmapData::DumpToChunk writes only the movie name. */
    }

    /* DumpToChunk always ends with "WriteIdentifier(Identifiers[3]); WriteInt(SlotCount);
       one name per slot". */
    if (b->kind != CKTEXTURE_BITMAP_BITMAP2 && b->slot_count > 0) {
        CHECK(site, INV_BMP_NAMES, b->has_slot_filenames && b->slot_filenames != NULL,
              "%u slots without file names", (unsigned)b->slot_count);
    }
    if (b->slot_count > 0) {
        CHECK(site, INV_BMP_ARRAYS, nmo_bitmap_slots_validate(b) == NMO_OK,
              "kind %d slots %u", (int)b->kind, (unsigned)b->slot_count);
    }

    /* With CKTEXTURE_INCLUDEORIGINALFILE DumpToChunk writes no pixels only when
       "CKFile::IncludeFile(f, GetSlotFileName(i), 0)" succeeded for every slot,
       and IncludeFile returns 0 for an empty name (EXTERNAL has no such test:
       GetSlotFileName gives "" for an unnamed slot). */
    if (b->kind == CKTEXTURE_BITMAP_NONE && b->slot_count > 0 &&
        save_options == NMO_CKTEXTURE_INCLUDEORIGINALFILE) {
        int all_named = 1;
        for (uint32_t i = 0; i < b->slot_count; i++) {
            all_named = all_named && slot_name_present(b, i);
        }
        CHECK(site, INV_BMP_NONE_NAMES, all_named, "%u slots, a name is empty", (unsigned)b->slot_count);
    }

    if (b->kind == CKTEXTURE_BITMAP_READER && b->reader_slots != NULL) {
        /* DumpToChunk writes SlotCount, then desc.Width, desc.Height and
           desc.BitsPerPixel of CKBitmapData::GetImageDesc (always 32 bpp). */
        CHECK(site, INV_BMP_READER_HEADER,
              b->reader_width > 0 && b->reader_height > 0 && b->reader_bpp == 32,
              "%d x %d x %d", (int)b->reader_width, (int)b->reader_height, (int)b->reader_bpp);
        *out_width = b->reader_width;
        *out_height = b->reader_height;
        for (uint32_t i = 0; i < b->slot_count; i++) {
            const nmo_texture_reader_slot_t *slot = &b->reader_slots[i];
            stats->reader_slots++;
            /* WriteReaderBitmap writes 0 (no image), 1 (image) or 2 (image and alpha plane). */
            CHECK(site, INV_BMP_READER_FORMAT, slot->format_type <= 2u,
                  "slot %u format %u", (unsigned)i, (unsigned)slot->format_type);
            if (slot->format_type == 0) {
                continue;
            }
            /* ReadReaderBitmap blits the decoded image into the slot's
               width x height surface, and Save encodes that very surface. */
            int w = 0, h = 0, ch = 0;
            nmo_arena_reset(stats->arena);
            uint8_t *pixels = slot->data != NULL && slot->data_size > 0
                ? nmo_stbi_load_from_memory(stats->arena, slot->data, (int)slot->data_size, &w, &h, &ch, 4)
                : NULL;
            CHECK(site, INV_BMP_READER_DECODE,
                  pixels != NULL && w == b->reader_width && h == b->reader_height,
                  "slot %u: %u bytes decode to %d x %d (stated %d x %d)", (unsigned)i,
                  (unsigned)slot->data_size, w, h, (int)b->reader_width, (int)b->reader_height);
            if (slot->format_type == 2) {
                /* WriteReaderBitmap writes the number of distinct alpha values;
                   one value is stored alone (0..255), otherwise a Height*Width plane. */
                const uint64_t plane = (uint64_t)b->reader_width * (uint64_t)b->reader_height;
                CHECK(site, INV_BMP_READER_ALPHA,
                      slot->alpha_count == 1u
                          ? slot->alpha_value <= 255u
                          : (slot->alpha_plane != NULL && slot->alpha_plane_size == plane),
                      "slot %u: %u distinct alphas, value %u, plane %u bytes", (unsigned)i,
                      (unsigned)slot->alpha_count, (unsigned)slot->alpha_value,
                      (unsigned)slot->alpha_plane_size);
            }
        }
    }

    if (b->kind == CKTEXTURE_BITMAP_RAW && b->raw_slots != NULL) {
        for (uint32_t i = 0; i < b->slot_count; i++) {
            const nmo_texture_raw_slot_t *slot = &b->raw_slots[i];
            stats->raw_slots++;
            if (slot->bits_per_pixel == 0) {
                continue; /* WriteRawBitmap writes just 0 for a slot without an image. */
            }
            if (i == 0) {
                *out_width = slot->width;
                *out_height = slot->height;
            }
            /* WriteRawBitmap writes the GetImageDesc descriptor: 32 bpp, the
               canonical ARGB masks and the surface size. */
            CHECK(site, INV_BMP_RAW_SHAPE,
                  slot->bits_per_pixel == 32 && slot->width > 0 && slot->height > 0 &&
                  slot->alpha_mask == 0xFF000000u && slot->red_mask == 0x00FF0000u &&
                  slot->green_mask == 0x0000FF00u && slot->blue_mask == 0x000000FFu,
                  "slot %u: %d bpp %d x %d masks a%08x r%08x g%08x b%08x", (unsigned)i,
                  (int)slot->bits_per_pixel, (int)slot->width, (int)slot->height,
                  (unsigned)slot->alpha_mask, (unsigned)slot->red_mask,
                  (unsigned)slot->green_mask, (unsigned)slot->blue_mask);
            /* ReadRawBitmap keeps the low nibble: 0 planes as stored, 1 the DCT codec. */
            const uint32_t compression = slot->compression & 0xFu;
            CHECK(site, INV_BMP_RAW_COMPRESSION, compression <= 1u,
                  "slot %u: compression %08x", (unsigned)i, (unsigned)slot->compression);
            const uint64_t plane = (uint64_t)(uint32_t)slot->width * (uint64_t)(uint32_t)slot->height;
            if (compression == 0u) {
                /* WriteRawBitmap writes blue, green, red (and alpha) as
                   Width*Height byte buffers; ReadRawBitmap indexes them by pixel. */
                CHECK(site, INV_BMP_RAW_PLANES,
                      slot->blue_size == plane && slot->green_size == plane &&
                      slot->red_size == plane &&
                      (slot->alpha_size == plane || slot->alpha_size == 0u),
                      "slot %u: planes b%u g%u r%u a%u for %u pixels", (unsigned)i,
                      (unsigned)slot->blue_size, (unsigned)slot->green_size,
                      (unsigned)slot->red_size, (unsigned)slot->alpha_size, (unsigned)plane);
                uint8_t *pixels = NULL;
                int channels = 0;
                nmo_arena_reset(stats->arena);
                nmo_status_t status = nmo_image_reconstruct_pixels(
                    slot->red_data, slot->green_data, slot->blue_data, slot->alpha_data,
                    slot->red_size, slot->green_size, slot->blue_size, slot->alpha_size,
                    slot->width, slot->height, slot->bits_per_pixel,
                    stats->arena, &pixels, &channels);
                CHECK(site, INV_BMP_RAW_DECODE, status == NMO_OK && pixels != NULL,
                      "slot %u: reconstruct status %d", (unsigned)i, (int)status);
            } else {
                int ok = 1;
                const uint8_t *encoded[3] = {slot->blue_data, slot->green_data, slot->red_data};
                const uint32_t sizes[3] = {slot->blue_size, slot->green_size, slot->red_size};
                nmo_arena_reset(stats->arena);
                for (int p = 0; p < 3 && ok; p++) {
                    int w = 0, h = 0;
                    uint8_t *decoded = NULL;
                    ok = encoded[p] != NULL &&
                         nmo_image_decode_dct_plane(encoded[p], sizes[p], stats->arena, &w, &h, &decoded) == NMO_OK &&
                         w == slot->width && h == slot->height;
                }
                CHECK(site, INV_BMP_RAW_DECODE, ok, "slot %u: DCT planes do not decode to %d x %d",
                      (unsigned)i, (int)slot->width, (int)slot->height);
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* CKTexture                                                                 */
/* ------------------------------------------------------------------------- */

static void check_texture(const site_t *site, const nmo_texture_state_t *t)
{
    site->stats->textures++;

    nmo_bitmap_slots_t bitmap;
    memset(&bitmap, 0, sizeof(bitmap));
    bitmap.kind = t->bitmap_kind;
    bitmap.slot_count = t->slot_count;
    bitmap.reader_width = t->reader_width;
    bitmap.reader_height = t->reader_height;
    bitmap.reader_bpp = t->reader_bpp;
    bitmap.reader_slots = t->reader_slots;
    bitmap.raw_slots = t->raw_slots;
    bitmap.bitmap2_slots = t->bitmap2_slots;
    bitmap.has_slot_filenames = t->has_slot_filenames;
    bitmap.slot_filenames = t->slot_filenames;
    bitmap.has_movie_filename = t->has_movie_filename;
    bitmap.movie_filename = t->movie_filename;

    int32_t width = 0, height = 0;
    check_bitmap(site, &bitmap, t->save_options, &width, &height);

    /* RCKTexture::Load: the cube flag (0x400) calls SetCubeMap, which does
       "SetSlotCount(6)". */
    if (t->is_cubemap) {
        CHECK(site, INV_TEX_CUBEMAP, t->slot_count == 6u, "cube map with %u slots", (unsigned)t->slot_count);
    }

    /* Texture and sprite share this: DumpToChunk branches on the save options
       0..4 of CK_BITMAP_SAVEOPTIONS (3 = use the context's). */
    CHECK(site, INV_TEX_SAVE_OPTIONS, t->save_options <= NMO_CKTEXTURE_INCLUDEORIGINALFILE,
          "save options %u", (unsigned)t->save_options);

    /* DumpToChunk: reader section only for IMAGEFORMAT (or USEGLOBAL resolved
       to it); pixel-less (names only) only for EXTERNAL / INCLUDEORIGINALFILE
       (or USEGLOBAL resolved to one of them). RAWDATA and every fall-back write raw planes. */
    if (t->slot_count > 0 && !(t->has_movie_filename && t->movie_filename != NULL)) {
        int ok = 1;
        if (t->bitmap_kind == CKTEXTURE_BITMAP_READER) {
            ok = t->save_options == NMO_CKTEXTURE_IMAGEFORMAT || t->save_options == NMO_CKTEXTURE_USEGLOBAL;
        } else if (t->bitmap_kind == CKTEXTURE_BITMAP_NONE) {
            ok = t->save_options == NMO_CKTEXTURE_EXTERNAL || t->save_options == NMO_CKTEXTURE_USEGLOBAL ||
                 t->save_options == NMO_CKTEXTURE_INCLUDEORIGINALFILE;
        }
        CHECK(site, INV_TEX_KIND_SAVE, ok, "bitmap kind %d with save options %u", (int)t->bitmap_kind,
              (unsigned)t->save_options);
    }

    /* Mipmap level: UseMipmap stores -1 (automatic) or 0, and
       RCKTexture::SystemToVideoMemory stores the level count the rasterizer
       created, a chain of at most log2(size) + 1 images for the size the
       rasterizer rounds the image up to (a power of two). */
    if (width > 0 && height > 0) {
        const int32_t longest = width > height ? width : height;
        CHECK(site, INV_TEX_MIP_LEVELS,
              t->mipmap_level == 0xFFu || (int)t->mipmap_level <= floor_log2(longest - 1) + 2,
              "mipmap level %u for %d x %d", (unsigned)t->mipmap_level, (int)width, (int)height);
    }

    /* RCKTexture::Load ends with "if (m_DesiredVideoFormat > _32_X8L8V8U8)
       m_DesiredVideoFormat = _16_ARGB1555". */
    if (t->has_desired_video_format) {
        CHECK(site, INV_TEX_VIDEO_FORMAT, nmo_texture_effective_desired_video_format(t) <= 27u,
              "effective desired video format %u", (unsigned)nmo_texture_effective_desired_video_format(t));
        /* Save sets the 0x200 flag and writes the dword only "if (m_DesiredVideoFormat)". */
        CHECK(site, INV_TEX_VIDEO_FORMAT_SET, t->desired_video_format != 0u,
              "stored desired video format 0");
    }

    /* CKBitmapData::SetCurrentSlot refuses a slot past the count, and Save
       writes GetCurrentSlot. */
    if (t->has_current_slot && t->slot_count > 0) {
        CHECK(site, INV_TEX_CURRENT_SLOT, t->current_slot >= 0 && (uint32_t)t->current_slot < t->slot_count,
              "current slot %d of %u", (int)t->current_slot, (unsigned)t->slot_count);
    }

    /* RCKTexture::SetUserMipMapMode builds floor(log2(min(w, h))) levels of
       size (w >> (i+1)) x (h >> (i+1)), 32 bpp; Load only reads them back when
       the count matches, and Save writes each level with WriteRawBitmap. */
    if (t->has_user_mipmaps && t->user_mipmap_count > 0 && width > 0 && height > 0) {
        const int32_t shortest = width < height ? width : height;
        int ok = (int)t->user_mipmap_count == floor_log2(shortest) && t->user_mipmaps != NULL;
        for (uint32_t i = 0; ok && i < t->user_mipmap_count; i++) {
            const nmo_texture_raw_slot_t *level = &t->user_mipmaps[i];
            const uint64_t plane = (uint64_t)(uint32_t)level->width * (uint64_t)(uint32_t)level->height;
            ok = level->bits_per_pixel == 32 &&
                 level->width == (width >> (i + 1)) && level->height == (height >> (i + 1)) &&
                 level->blue_size == plane && level->green_size == plane && level->red_size == plane;
        }
        CHECK(site, INV_TEX_USER_MIPS, ok, "%u user mipmaps for %d x %d", (unsigned)t->user_mipmap_count,
              (int)width, (int)height);
    }

    /* DumpToChunk returns after the movie file name, before any slot section. */
    if (t->has_movie_filename && t->movie_filename != NULL) {
        CHECK(site, INV_TEX_MOVIE, t->bitmap_kind == CKTEXTURE_BITMAP_NONE && !t->has_slot_filenames,
              "movie texture with slot sections (kind %d)", (int)t->bitmap_kind);
    }
}

/* ------------------------------------------------------------------------- */
/* CK2dEntity, CKSprite, CKSpriteText                                        */
/* ------------------------------------------------------------------------- */

static void check_entity(const site_t *site, const nmo_2dentity_state_t *e)
{
    site->stats->entities++;
    const nmo_class_id_t class_id = site->object->class_id;
    const int homogeneous = (e->flags & NMO_CK2DENTITY_FLAG_HOMOGENEOUS) != 0;

    if (!e->data_is_legacy) {
        /* RCK2dEntity::Load: "m_Flags = flags & ~0x70800" (the three reserved
           bits that mark the optional blocks and UPDATEHOMOGENEOUSCOORD). */
        CHECK(site, INV_E2D_FLAGS, (e->flags & ~NMO_CK2DENTITY_FLAGS_MASK) == 0u, "flags %08x", (unsigned)e->flags);
        /* Load reads the homogeneous rect into m_HomogeneousRect exactly when
           CK_2DENTITY_USEHOMOGENEOUSCOORD is set, else m_Rect. */
        CHECK(site, INV_E2D_HOMOGENEOUS, (e->has_homogeneous_rect != 0) == homogeneous,
              "flags %08x, homogeneous rect %d", (unsigned)e->flags, (int)e->has_homogeneous_rect);
    }

    /* A homogeneous rect may legitimately be NaN: SetHomogeneousCoordinates /
       SetRect store "TransformToHomogeneous(rect, relRect)", which divides by
       the size of the parent rectangle (SceneManagement.cmo stores 0xffc00000
       for a frame under a zero-size parent). The pixel rect and the source rect
       are plain copies of what the caller gave. */
    CHECK(site, INV_E2D_FINITE, finite_rect(&e->rect) && finite_rect(&e->source_rect),
          "rect or source rect is not finite");

    /* RCK2dEntity::UpdateExtents clamps "if (m_Rect.right < m_Rect.left)
       m_Rect.right = m_Rect.left" (same for bottom): the engine never works
       with a negative extent. With the homogeneous flag the stored rect is
       the homogeneous one. */
    const nmo_rect_t *active = homogeneous && e->has_homogeneous_rect ? &e->homogeneous_rect : &e->rect;
    if (finite_rect(active)) {
        CHECK(site, INV_E2D_RECT_ORDER, active->right >= active->left && active->bottom >= active->top,
              "rect %g,%g,%g,%g", (double)active->left, (double)active->top,
              (double)active->right, (double)active->bottom);
    }

    if (!e->data_is_legacy) {
        /* RCK2dEntity::Save sets the 0x10000 block only when m_SourceRect is not
           all zero; Load otherwise takes (0,0,1,1) for a plain CK2dEntity and
           zeros for the sprite classes. */
        if (e->has_source_rect) {
            CHECK(site, INV_E2D_SOURCE_RECT, !rect_is_zero(&e->source_rect), "stored source rect is empty");
        } else {
            const nmo_rect_t *r = &e->source_rect;
            int is_default = class_id == NMO_CID_2DENTITY
                ? (r->left == 0.0f && r->top == 0.0f && r->right == 1.0f && r->bottom == 1.0f)
                : rect_is_zero(r);
            CHECK(site, INV_E2D_SOURCE_RECT, is_default, "absent source rect %g,%g,%g,%g",
                  (double)r->left, (double)r->top, (double)r->right, (double)r->bottom);
        }
        /* Save sets the 0x20000 block only "if (m_ZOrder)"; Load otherwise sets 0. */
        CHECK(site, INV_E2D_ZORDER, e->has_z_order ? e->z_order != 0 : e->z_order == 0,
              "z order %d stored %d", (int)e->z_order, (int)e->has_z_order);
    }

    if (e->has_parent) {
        /* RCK2dEntity::SetParent refuses a parent whose chain reaches the entity
           ("for (i = parent; i; i = i->m_Parent) if (i == this) return 0"). */
        const nmo_object_t *parent = find_target(site, &e->parent);
        int ok = reference_is_class(site, &e->parent, NMO_CID_2DENTITY) && parent != site->object;
        const nmo_object_t *cursor = parent;
        size_t guard = nmo_object_repository_get_count(site->repository) + 1;
        while (ok && cursor != NULL) {
            if (guard-- == 0) {
                ok = 0;
                break;
            }
            const nmo_2dentity_state_t *up = NULL;
            if (cursor->class_id == NMO_CID_2DENTITY) {
                up = (const nmo_2dentity_state_t *)nmo_object_get_state(cursor);
            } else if (cursor->class_id == NMO_CID_SPRITE) {
                up = &((const nmo_sprite_state_t *)nmo_object_get_state(cursor))->entity;
            } else if (cursor->class_id == NMO_CID_SPRITETEXT) {
                up = &((const nmo_spritetext_state_t *)nmo_object_get_state(cursor))->base.entity;
            }
            cursor = up != NULL && up->has_parent ? find_target(site, &up->parent) : NULL;
            if (cursor == site->object) {
                ok = 0;
            }
        }
        CHECK(site, INV_E2D_PARENT, ok, "parent state %d id %u", (int)e->parent.state, (unsigned)e->parent.raw_id);

        /* SetParent copies the parent's CK_2DENTITY_BACKGROUND bit onto the
           child, and HierarchySetBackground keeps a hierarchy uniform. */
        const nmo_object_t *p = find_target(site, &e->parent);
        if (p != NULL) {
            uint32_t parent_flags = 0;
            int known = 1;
            if (p->class_id == NMO_CID_2DENTITY) {
                parent_flags = ((const nmo_2dentity_state_t *)nmo_object_get_state(p))->flags;
            } else if (p->class_id == NMO_CID_SPRITE) {
                parent_flags = ((const nmo_sprite_state_t *)nmo_object_get_state(p))->entity.flags;
            } else if (p->class_id == NMO_CID_SPRITETEXT) {
                parent_flags = ((const nmo_spritetext_state_t *)nmo_object_get_state(p))->base.entity.flags;
            } else {
                known = 0;
            }
            if (known) {
                CHECK(site, INV_E2D_BACKGROUND,
                      ((e->flags ^ parent_flags) & CK_2DENTITY_BACKGROUND) == 0u,
                      "flags %08x, parent flags %08x", (unsigned)e->flags, (unsigned)parent_flags);
            }
        }
    }

    /* RCK2dEntity::Load keeps a material only for CKCID_2DENTITY itself
       ("else this->m_Material = 0"); SetMaterial takes a CKMaterial. */
    CHECK(site, INV_E2D_MATERIAL,
          !e->has_material ||
          (class_id == NMO_CID_2DENTITY && reference_is_class(site, &e->material, NMO_CID_MATERIAL)),
          "material (class %u) state %d", (unsigned)class_id, (int)e->material.state);
}

static void check_sprite(const site_t *site, const nmo_sprite_state_t *s)
{
    site->stats->sprites++;
    check_entity(site, &s->entity);

    int32_t width = 0, height = 0;
    if (s->has_bitmap_data) {
        check_bitmap(site, &s->bitmap, s->has_save_options ? s->save_options : 0xFFFFFFFFu, &width, &height);
    }
    const int has_movie = s->bitmap.has_movie_filename && s->bitmap.movie_filename != NULL;

    /* CKBitmapData::SetCurrentSlot returns 0 for "Slot >= slot count" (a movie
       sprite indexes frames instead); RCKSprite::Save writes GetCurrentSlot. */
    if (s->has_slot && s->has_bitmap_data && !has_movie) {
        CHECK(site, INV_SPR_SLOT, s->bitmap.slot_count > 0 && s->current_slot < s->bitmap.slot_count,
              "current slot %u of %u", (unsigned)s->current_slot, (unsigned)s->bitmap.slot_count);
    }
    if (s->has_save_options) {
        CHECK(site, INV_SPR_SAVE_OPTIONS, s->save_options <= NMO_CKTEXTURE_INCLUDEORIGINALFILE,
              "save options %u", (unsigned)s->save_options);
    }

    /* RCK2dEntity::UpdateExtents uses m_SourceRect as the pixel window of the
       sprite's width x height video surface when CK_2DENTITY_USESRCRECT is set. */
    if ((s->entity.flags & CK_2DENTITY_USESRCRECT) != 0u && width > 0 && height > 0) {
        const nmo_rect_t *r = &s->entity.source_rect;
        CHECK(site, INV_SPR_SOURCE_RECT,
              r->left >= 0.0f && r->top >= 0.0f && r->left <= r->right && r->top <= r->bottom &&
              r->right <= (float)width && r->bottom <= (float)height,
              "source rect %g,%g,%g,%g in %d x %d", (double)r->left, (double)r->top,
              (double)r->right, (double)r->bottom, (int)width, (int)height);
    }

    /* RCKSprite::Save (file mode) writes the 0x20000 block (color and flag)
       and the 0x10000 block (slot) unconditionally. */
    CHECK(site, INV_SPR_TRANSPARENCY, s->has_transparency && s->has_slot && s->has_save_options,
          "transparency %d slot %d save options %d", (int)s->has_transparency, (int)s->has_slot,
          (int)s->has_save_options);
}

/* ------------------------------------------------------------------------- */
/* CKSprite3D                                                                */
/* ------------------------------------------------------------------------- */

static void check_sprite3d(const site_t *site, const nmo_sprite3d_state_t *s)
{
    site->stats->sprite3ds++;

    /* RCKSprite3D::Save writes CK_STATESAVE_SPRITE3DDATA unconditionally. */
    CHECK(site, INV_S3D_DATA, s->has_data != 0, "no data section");
    if (!s->has_data) {
        return;
    }
    /* VXSPRITE3D_TYPE; UpdateOrientation handles BILLBOARD, XROTATE and YROTATE. */
    CHECK(site, INV_S3D_MODE, s->mode <= VXSPRITE3D_ORIENTABLE, "mode %u", (unsigned)s->mode);
    /* Save writes half the box extent "(Max.x - Min.x) * 0.5", which SetSize
       builds as size * 0.5 for the quad FillBatch lays out. */
    CHECK(site, INV_S3D_SIZE,
          isfinite(s->half_width) && isfinite(s->half_height) && s->half_width > 0.0f && s->half_height > 0.0f,
          "half size %g x %g", (double)s->half_width, (double)s->half_height);
    CHECK(site, INV_S3D_MAPPING,
          isfinite(s->offset.x) && isfinite(s->offset.y) && finite_rect(&s->uv_rect),
          "offset %g,%g uv %g,%g,%g,%g", (double)s->offset.x, (double)s->offset.y,
          (double)s->uv_rect.left, (double)s->uv_rect.top, (double)s->uv_rect.right, (double)s->uv_rect.bottom);
    /* RCKSprite3D::SetMaterial takes a CKMaterial; Load reads it with ReadObject. */
    if (reference_present(&s->material)) {
        CHECK(site, INV_S3D_MATERIAL, reference_is_class(site, &s->material, NMO_CID_MATERIAL),
              "material state %d id %u", (int)s->material.state, (unsigned)s->material.raw_id);
    }
}

/* ------------------------------------------------------------------------- */
/* CKWaveSound                                                               */
/* ------------------------------------------------------------------------- */

static int whole_degrees(float value)
{
    return isfinite(value) && value >= 0.0f && value <= 360.0f && value == floorf(value);
}

static void check_wavesound(const site_t *site, const nmo_wavesound_state_t *w)
{
    site->stats->wavesounds++;

    /* CKSound::Save writes m_SaveOptions, a CK_SOUND_SAVEOPTIONS. */
    CHECK(site, INV_SND_SAVE_OPTIONS, w->base.save_options <= CKSOUND_USEGLOBAL,
          "save options %u", (unsigned)w->base.save_options);
    /* CKWaveSound::Load takes the sound from CKSound's file name (or the 0x100000
       override) through SetSoundFileName and Recreate; Save stores no samples. */
    CHECK(site, INV_SND_FILE_NAME,
          (w->base.file_name != NULL && w->base.file_name[0] != '\0') ||
          (w->has_wave_file_name && w->wave_file_name != NULL && w->wave_file_name[0] != '\0'),
          "no file name");
    /* CKWaveSound::Save in file mode writes WAVSOUNDDURATION and WAVSOUNDDATA2. */
    CHECK(site, INV_SND_SECTIONS, w->has_duration && w->has_data2,
          "duration section %d settings section %d", (int)w->has_duration, (int)w->has_data2);
    if (!w->has_data2) {
        return;
    }
    /* m_Duration is the length in milliseconds (constructor 0, filled by Recreate). */
    CHECK(site, INV_SND_DURATION, w->duration >= 0, "duration %d", (int)w->duration);
    /* CKWaveSound::SetState / SetType take "state & 7" as a CK_WAVESOUND_TYPE. */
    const uint32_t type = w->state_flags & CK_WAVESOUND_ALLTYPE;
    CHECK(site, INV_SND_TYPE, in_range_u32(type, CK_WAVESOUND_BACKGROUND, CK_WAVESOUND_CONE),
          "state %08x", (unsigned)w->state_flags);
    /* DX7SoundManager::UpdateSettings maps gain through log10 (0 or less is
       -10000 mB, the least) and reads it back with sub_24BC2907, which is in
       [0,1]; pan is read back as sub_24BC2995, in [-1,1]; pitch is read back as
       the frequency over the original one. Save writes m_FinalGain as given. */
    CHECK(site, INV_SND_GAIN, isfinite(w->gain) && w->gain >= 0.0f && w->gain <= 1.0f, "gain %g", (double)w->gain);
    CHECK(site, INV_SND_PAN, isfinite(w->pan) && w->pan >= -1.0f && w->pan <= 1.0f, "pan %g", (double)w->pan);
    CHECK(site, INV_SND_PITCH, isfinite(w->pitch) && w->pitch > 0.0f, "pitch %g", (double)w->pitch);
    /* CKWaveSound::GetCone reads the DirectSound buffer back through
       DX7SoundManager::Update3DSettings, whose angles are DWORD degrees
       ((__int64)m_InAngle) and whose outside gain comes from sub_24BC2907. */
    CHECK(site, INV_SND_CONE,
          whole_degrees(w->cone_in_angle) && whole_degrees(w->cone_out_angle) && w->cone_in_angle <= w->cone_out_angle,
          "cone %g,%g", (double)w->cone_in_angle, (double)w->cone_out_angle);
    CHECK(site, INV_SND_CONE_GAIN, isfinite(w->cone_out_gain) && w->cone_out_gain >= 0.0f && w->cone_out_gain <= 1.0f,
          "gain outside cone %g", (double)w->cone_out_gain);
    /* GetMinMaxDistance reads DirectSound's distances back; the buffer only
       accepts positive ones. (Min <= max is NOT guaranteed: 10 / 0.5 occurs.) */
    CHECK(site, INV_SND_DISTANCE,
          isfinite(w->min_distance) && isfinite(w->max_distance) && w->min_distance > 0.0f && w->max_distance > 0.0f,
          "distance %g,%g", (double)w->min_distance, (double)w->max_distance);
    /* SetMinMaxDistance takes the mute flag as a CKWORD. */
    CHECK(site, INV_SND_MUTE, w->distance_behavior <= 0xFFFFu, "mute-after-max %u", (unsigned)w->distance_behavior);
    /* GetAttachedEntity returns a CK3dEntity. */
    if (reference_present(&w->attached_object)) {
        CHECK(site, INV_SND_ATTACHED, reference_is_class(site, &w->attached_object, NMO_CID_3DENTITY),
              "attached object state %d id %u", (int)w->attached_object.state, (unsigned)w->attached_object.raw_id);
    }
    CHECK(site, INV_SND_VECTORS,
          isfinite(w->position.x) && isfinite(w->position.y) && isfinite(w->position.z) &&
          isfinite(w->direction.x) && isfinite(w->direction.y) && isfinite(w->direction.z),
          "position %g,%g,%g direction %g,%g,%g", (double)w->position.x, (double)w->position.y,
          (double)w->position.z, (double)w->direction.x, (double)w->direction.y, (double)w->direction.z);
}

/* ------------------------------------------------------------------------- */
/* Corpus walk                                                               */
/* ------------------------------------------------------------------------- */

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
    size_t count = nmo_object_repository_get_count(repository);
    for (size_t i = 0; i < count; i++) {
        const nmo_object_t *object = nmo_object_repository_get_by_index(repository, i);
        if (object == NULL) {
            continue;
        }
        const void *state = nmo_object_get_state(object);
        if (state == NULL) {
            continue;
        }
        site_t site = {stats, path, object, repository};
        switch (object->class_id) {
        case NMO_CID_MATERIAL:
            check_material(&site, (const nmo_material_state_t *)state);
            break;
        case NMO_CID_TEXTURE:
            check_texture(&site, (const nmo_texture_state_t *)state);
            break;
        case NMO_CID_2DENTITY:
            check_entity(&site, (const nmo_2dentity_state_t *)state);
            break;
        case NMO_CID_SPRITE:
            check_sprite(&site, (const nmo_sprite_state_t *)state);
            break;
        case NMO_CID_SPRITETEXT:
            check_sprite(&site, &((const nmo_spritetext_state_t *)state)->base);
            break;
        case NMO_CID_SPRITE3D:
            check_sprite3d(&site, (const nmo_sprite3d_state_t *)state);
            break;
        case NMO_CID_WAVESOUND:
            check_wavesound(&site, (const nmo_wavesound_state_t *)state);
            break;
        default:
            break;
        }
    }

    nmo_session_destroy(session);
}

TEST(corpus_semantics_media, decoded_values_satisfy_engine_guarantees)
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
    stats->arena = nmo_arena_create(NULL, 1u << 20);
    ASSERT_NOT_NULL(stats->arena);

    int walk_status = test_corpus_walk(NMO_TEST_DATA_DIR, check_file, stats);
    nmo_arena_destroy(stats->arena);
    nmo_context_release(ctx);

    printf("  Corpus semantics: files=%zu load_errors=%zu materials=%zu textures=%zu "
           "reader_slots=%zu raw_slots=%zu 2d_entities=%zu sprites=%zu sprite3d=%zu wavesounds=%zu\n",
           stats->files, stats->load_errors, stats->materials, stats->textures,
           stats->reader_slots, stats->raw_slots, stats->entities, stats->sprites,
           stats->sprite3ds, stats->wavesounds);
    size_t vacuous = 0;
    size_t violated = 0;
    for (int id = 0; id < INV_COUNT; id++) {
        printf("  checked=%-7zu violated=%-5zu %s\n", stats->inv[id].checked,
               stats->inv[id].violated, invariant_text[id]);
        vacuous += stats->inv[id].checked == 0 ? 1u : 0u;
        violated += stats->inv[id].violated;
    }
    printf("  Invariants: %d, vacuous: %zu, violations: %zu\n", (int)INV_COUNT, vacuous, violated);

    ASSERT_EQ(0, walk_status);
    ASSERT_GE(stats->files, 1u);
    ASSERT_EQ(0u, stats->load_errors);
    ASSERT_EQ(0u, vacuous);
    ASSERT_EQ(0u, violated);
    free(stats);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(corpus_semantics_media, decoded_values_satisfy_engine_guarantees);
TEST_MAIN_END()
