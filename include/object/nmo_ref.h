/**
 * @file nmo_ref.h
 * @brief Lossless object-reference values used by built-in object schemas.
 */

#ifndef NMO_REF_H
#define NMO_REF_H

#include "nmo_types.h"
#include "core/nmo_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Object reference retaining both serialized and runtime identities.
 *
 * `raw_id` is authoritative when state is not NMO_REF_RESOLVED. `id` is
 * authoritative after successful resolution and is remapped on save.
 */
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_object_repository nmo_object_repository_t;
typedef struct nmo_type_registry nmo_type_registry_t;

static inline nmo_ref_t nmo_ref_from_raw(nmo_object_id_t raw_id)
{
    nmo_ref_t ref;
    ref.raw_id = raw_id;
    ref.id = NMO_OBJECT_ID_NONE;
    ref.state = (raw_id == NMO_OBJECT_ID_NONE || raw_id == NMO_OBJECT_ID_INVALID)
        ? NMO_REF_NONE : NMO_REF_UNRESOLVED;
    return ref;
}

static inline nmo_ref_t nmo_ref_from_id(nmo_object_id_t id)
{
    nmo_ref_t ref;
    ref.raw_id = id;
    ref.id = id;
    ref.state = (id == NMO_OBJECT_ID_NONE || id == NMO_OBJECT_ID_INVALID)
        ? NMO_REF_NONE : NMO_REF_RESOLVED;
    return ref;
}

static inline nmo_object_id_t nmo_ref_runtime_id(const nmo_ref_t *ref)
{
    return ref != NULL && ref->state == NMO_REF_RESOLVED
        ? ref->id : NMO_OBJECT_ID_NONE;
}

/** Read a reference without losing an unresolved encoded ID. */
NMO_API nmo_status_t nmo_ref_read(nmo_chunk_t *chunk, nmo_ref_t *out_ref);

/** Write a resolved reference through remapping, or preserve its raw ID. */
NMO_API nmo_status_t nmo_ref_write(nmo_chunk_t *chunk, const nmo_ref_t *ref);

/** Write a reference within an object sequence without adding scalar ID metadata. */
NMO_API nmo_status_t nmo_ref_write_sequence_item(
    nmo_chunk_t *chunk,
    const nmo_ref_t *ref);

/** Read a counted object sequence into lossless reference records. */
NMO_API nmo_status_t nmo_ref_read_sequence(
    nmo_chunk_t *chunk,
    nmo_ref_t **out_refs,
    size_t *out_count,
    nmo_arena_t *arena);

/** Write a counted object sequence from lossless reference records. */
NMO_API nmo_status_t nmo_ref_write_sequence(
    nmo_chunk_t *chunk,
    const nmo_ref_t *refs,
    size_t count);

/** Mark a resolved reference as a class mismatch when its target has the wrong type. */
NMO_API void nmo_ref_check_class(
    nmo_ref_t *ref,
    const nmo_object_repository_t *repository,
    const nmo_type_registry_t *types,
    nmo_class_id_t expected_class_id);

static inline nmo_object_id_t nmo_ref_serialized_id(const nmo_ref_t *ref)
{
    if (ref == NULL) {
        return NMO_OBJECT_ID_NONE;
    }
    if (ref->state == NMO_REF_NONE) {
        return NMO_OBJECT_ID_NONE;
    }
    return ref->state == NMO_REF_RESOLVED ? ref->id : ref->raw_id;
}

#ifdef __cplusplus
}
#endif

#endif /* NMO_REF_H */
