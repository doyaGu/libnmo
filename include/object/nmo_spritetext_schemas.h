/**
 * @file nmo_spritetext_schemas.h
 * @brief CKSpriteText schema definitions
 */

#ifndef NMO_CKSPRITETEXT_SCHEMAS_H
#define NMO_CKSPRITETEXT_SCHEMAS_H

#include "nmo_types.h"
#include "object/nmo_2dentity_schemas.h"
#include "object/nmo_object_struct_defs.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_statesave_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

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

/** CKSpriteText state (uses CK2dEntity base, matches Save/Load) */
typedef struct nmo_spritetext_state {
    nmo_2dentity_state_t base;

    const char *text_content;
    nmo_font_info_t font;
    uint32_t font_color;
    uint32_t background_color;

    bool needs_redraw;
} nmo_spritetext_state_t;

NMO_API nmo_status_t nmo_spritetext_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_spritetext_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_spritetext_vtable, nmo_register_spritetext_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSPRITETEXT_SCHEMAS_H */
