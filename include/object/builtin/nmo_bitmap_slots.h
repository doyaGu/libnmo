/**
 * @file nmo_bitmap_slots.h
 * @brief The bitmap slots of a CKBitmapData, as textures and sprites store them
 *
 * RCKTexture and RCKSprite both pass their bitmap to CKBitmapData::DumpToChunk
 * and ReadFromChunk, which write the bitmap under identifiers the caller
 * names: the movie file name, the slots encoded by a bitmap reader, the slots
 * as raw planes, the slot file names and, in older files, slots as tagged
 * images. The two classes differ only in the identifiers.
 */

#ifndef NMO_BITMAP_SLOTS_H
#define NMO_BITMAP_SLOTS_H

#include "nmo_types.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_object_struct_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

/**
 * @brief The slots of a bitmap and the names that go with them.
 *
 * Exactly one of reader_slots, raw_slots and bitmap2_slots is set, as kind
 * says; each has slot_count entries. reader_width, reader_height and
 * reader_bpp are the header of the reader section.
 */
typedef struct nmo_bitmap_slots {
    CKTEXTURE_BITMAP_KIND kind;
    uint32_t slot_count;
    int32_t reader_width;
    int32_t reader_height;
    int32_t reader_bpp;
    nmo_texture_reader_slot_t *reader_slots;
    nmo_texture_raw_slot_t *raw_slots;
    nmo_texture_bitmap2_slot_t *bitmap2_slots;

    uint8_t has_slot_filenames;
    char **slot_filenames;          /**< slot_count names */
    uint8_t has_movie_filename;
    char *movie_filename;
} nmo_bitmap_slots_t;

/** The identifiers a class hands to CKBitmapData::DumpToChunk. */
typedef struct nmo_bitmap_slot_ids {
    uint32_t movie;      /**< movie file name */
    uint32_t reader;     /**< slots encoded by a bitmap reader */
    uint32_t raw;        /**< slots as raw planes */
    uint32_t filenames;  /**< slot file names */
    uint32_t bitmap2;    /**< obsolete slots as tagged images */
} nmo_bitmap_slot_ids_t;

/**
 * @brief Read the bitmap sections named by ids into slots.
 *
 * Reads the reader, raw or obsolete bitmap section (the first one present),
 * then the file names and the movie file name. The file name list sets the
 * slot count, adding empty slots or dropping surplus ones, as SetSlotCount.
 */
NMO_API nmo_status_t nmo_bitmap_slots_read(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    const nmo_bitmap_slot_ids_t *ids,
    nmo_bitmap_slots_t *slots);

/** @brief Write the bitmap sections named by ids. */
NMO_API nmo_status_t nmo_bitmap_slots_write(
    nmo_chunk_t *chunk,
    const nmo_bitmap_slot_ids_t *ids,
    const nmo_bitmap_slots_t *slots);

/** @brief Check that the pointers and counts of slots agree. */
NMO_API nmo_status_t nmo_bitmap_slots_validate(const nmo_bitmap_slots_t *slots);

/** @brief Deep copy slots into arena storage. */
NMO_API nmo_status_t nmo_bitmap_slots_copy(
    nmo_arena_t *arena,
    nmo_bitmap_slots_t *dst,
    const nmo_bitmap_slots_t *src);

NMO_API bool nmo_bitmap_slots_equals(
    const nmo_bitmap_slots_t *a,
    const nmo_bitmap_slots_t *b);

/** @brief Fold slots into an FNV-1a hash. */
NMO_API uint32_t nmo_bitmap_slots_hash(
    uint32_t hash,
    const nmo_bitmap_slots_t *slots);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BITMAP_SLOTS_H */
