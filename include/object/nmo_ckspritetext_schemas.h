/**
 * @file nmo_ckspritetext_schemas.h
 * @brief CKSpriteText schema definitions
 */

#ifndef NMO_CKSPRITETEXT_SCHEMAS_H
#define NMO_CKSPRITETEXT_SCHEMAS_H

#include "nmo_types.h"
#include "object/nmo_ck2dentity_schemas.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

/** Serialization identifiers */
#define NMO_CKSPRITETEXT_IDENTIFIER_TEXT       0x01000000
#define NMO_CKSPRITETEXT_IDENTIFIER_FONT       0x02000000
#define NMO_CKSPRITETEXT_IDENTIFIER_COLOR      0x04000000

/** Font weight constants (Windows LOGFONT standard) */
#define NMO_FONT_WEIGHT_DONTCARE    0
#define NMO_FONT_WEIGHT_THIN        100
#define NMO_FONT_WEIGHT_EXTRALIGHT  200
#define NMO_FONT_WEIGHT_LIGHT       300
#define NMO_FONT_WEIGHT_NORMAL      400
#define NMO_FONT_WEIGHT_MEDIUM      500
#define NMO_FONT_WEIGHT_SEMIBOLD    600
#define NMO_FONT_WEIGHT_BOLD        700
#define NMO_FONT_WEIGHT_EXTRABOLD   800
#define NMO_FONT_WEIGHT_HEAVY       900

/** Font properties structure (matches VXFONTINFO serialization) */
typedef struct nmo_font_info {
    const char *font_name;
    int32_t size;
    int32_t weight;
    int32_t italic;
    int32_t underline;
} nmo_font_info_t;

/** CKSpriteText state (uses CK2dEntity base, matches Save/Load) */
typedef struct nmo_ck_spritetext_state {
    nmo_ck2dentity_state_t base;

    const char *text_content;
    nmo_font_info_t font;
    uint32_t font_color;
    uint32_t background_color;

    bool needs_redraw;
} nmo_ck_spritetext_state_t;

NMO_API nmo_result_t nmo_register_ckspritetext_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_ckspritetext_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck_spritetext_state_t *out_state);

NMO_API nmo_result_t nmo_ckspritetext_serialize(
    const nmo_ck_spritetext_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSPRITETEXT_SCHEMAS_H */
