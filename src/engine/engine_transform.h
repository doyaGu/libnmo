/**
 * @file engine_transform.h
 * @brief 3D entities' matrices and hierarchy (VxMath's row convention), and
 *        the geometry the blocks ask of them.
 */

#ifndef NMO_ENGINE_TRANSFORM_H
#define NMO_ENGINE_TRANSFORM_H

#include "engine_base.h"

typedef struct eng_range eng_range_t;

/* A VxMatrix: rows 0 to 2 the X, Y and Z axes, row 3 the position. */
typedef struct eng_matrix {
    float m[4][4];
} eng_matrix_t;

typedef struct eng_vector {
    float x, y, z;
} eng_vector_t;

/*
 * A CK3dEntity's matrices and place in the hierarchy: its parent and its
 * children, in order (indexes into the engine's entities, -1 for none).
 */
typedef struct eng_entity {
    nmo_object_id_t id;
    eng_matrix_t world;
    eng_matrix_t local;
    uint32_t moveable_flags;
    float animation_lod; /* CKCharacter's runtime animation LOD; initially one */
    nmo_object_id_t exclusive_animation; /* BodyPart's secondary animation gate */
    int32_t parent;
    int32_t first_child, last_child;
    int32_t next_sibling, prev_sibling;
    bool destroyed;
} eng_entity_t;

/* A ray's hit on an entity's mesh, in the entity's space */
typedef struct eng_ray_hit {
    eng_vector_t point;
    eng_vector_t normal;     /* the vertex normals interpolated, not normalized */
    int32_t face;
    int32_t vertices[3];     /* the face's vertex indices */
    eng_vector_t corners[3]; /* and their positions */
} eng_ray_hit_t;

void eng_matrix_identity(eng_matrix_t *m);
void eng_matrix_multiply(eng_matrix_t *result, const eng_matrix_t *a, const eng_matrix_t *b);
void eng_matrix_inverse(eng_matrix_t *result, const eng_matrix_t *a);
/* Recover missing animation channels. Principal axes use polar/spectral
 * decomposition; otherwise use CK's normalized-row decomposition. Outputs
 * may be NULL and are left unchanged if no finite decomposition is found. */
bool eng_matrix_decompose(const eng_matrix_t *matrix, bool principal_axes,
                            eng_matrix_t *rotation, eng_vector_t *scale);
void eng_vector_transform(eng_vector_t *result, const eng_matrix_t *m, const eng_vector_t *v);
void eng_vector_rotate(eng_vector_t *result, const eng_matrix_t *m, const eng_vector_t *v);
float eng_vector_magnitude(const eng_vector_t *v);
void eng_vector_normalize(eng_vector_t *v);
eng_vector_t eng_vector_cross(const eng_vector_t *a, const eng_vector_t *b);
void eng_matrix_from_rotation(eng_matrix_t *result, const eng_vector_t *axis, float angle);
void eng_matrix_from_euler(eng_matrix_t *result, float ax, float ay, float az);
void eng_matrix_to_euler(const eng_matrix_t *m, float *ax, float *ay, float *az);
nmo_status_t eng_load_entities(nmo_engine_t *engine, const eng_range_t *range);
void eng_free_entities(nmo_engine_t *engine);
nmo_status_t eng_add_entity(nmo_engine_t *engine, nmo_object_id_t id);
int32_t eng_entity_index(const nmo_engine_t *engine, nmo_object_id_t id);
void eng_entity_set_world_matrix(nmo_engine_t *engine, int32_t index, const eng_matrix_t *m,
                                 bool keep_children);
void eng_entity_set_local_matrix(nmo_engine_t *engine, int32_t index, const eng_matrix_t *m,
                                 bool keep_children);
void eng_entity_transform(nmo_engine_t *engine, int32_t index, eng_vector_t *dest,
                          const eng_vector_t *src, int32_t ref);
void eng_entity_inverse_transform(nmo_engine_t *engine, int32_t index, eng_vector_t *dest,
                                  const eng_vector_t *src, int32_t ref);
void eng_entity_transform_vector(nmo_engine_t *engine, int32_t index, eng_vector_t *dest,
                                 const eng_vector_t *src, int32_t ref, bool inverse_transform);
void eng_entity_set_position(nmo_engine_t *engine, int32_t index, const eng_vector_t *position,
                             int32_t ref, bool keep_children);
eng_vector_t eng_entity_position(nmo_engine_t *engine, int32_t index, int32_t ref);
void eng_entity_translate(nmo_engine_t *engine, int32_t index, const eng_vector_t *vector,
                          int32_t ref, bool keep_children);
void eng_entity_rotate(nmo_engine_t *engine, int32_t index, const eng_vector_t *axis, float angle,
                       int32_t ref, bool keep_children);
eng_vector_t eng_entity_scale(nmo_engine_t *engine, int32_t index, bool local);
void eng_entity_set_scale(nmo_engine_t *engine, int32_t index, const eng_vector_t *scale,
                          bool keep_children, bool local);
void eng_entity_add_scale(nmo_engine_t *engine, int32_t index, const eng_vector_t *scale,
                          bool keep_children, bool local);
void eng_entity_set_orientation(nmo_engine_t *engine, int32_t index, const eng_vector_t *dir,
                                const eng_vector_t *up, const eng_vector_t *right, int32_t ref,
                                bool keep_children);
void eng_entity_orientation(nmo_engine_t *engine, int32_t index, eng_vector_t *dir,
                            eng_vector_t *up, eng_vector_t *right, int32_t ref);
void eng_entity_look_at(nmo_engine_t *engine, int32_t index, const eng_vector_t *position,
                        int32_t ref, bool keep_children);
bool eng_entity_set_parent(nmo_engine_t *engine, int32_t index, int32_t parent,
                           bool keep_world_position);
void eng_entity_destroy(nmo_engine_t *engine, nmo_object_id_t id);
bool eng_entity_bary_center(nmo_engine_t *engine, int32_t index, eng_vector_t *out);
bool eng_entity_has_mesh(nmo_engine_t *engine, int32_t index);
float eng_entity_radius(nmo_engine_t *engine, int32_t index);
bool eng_entity_local_box(nmo_engine_t *engine, int32_t index, eng_vector_t *min,
                          eng_vector_t *max);
bool eng_entity_world_box(nmo_engine_t *engine, int32_t index, eng_vector_t *min,
                          eng_vector_t *max);
bool eng_ray_box(const eng_vector_t *origin, const eng_vector_t *dir, const eng_vector_t *min,
                 const eng_vector_t *max);
int eng_entity_ray_intersection(nmo_engine_t *engine, int32_t index, const eng_vector_t *pos1,
                                const eng_vector_t *pos2, eng_ray_hit_t *hit);
/* FloorManager's vertical line query, using upward faces within its 80-degree limit. */
bool eng_entity_floor_height(nmo_engine_t *engine, int32_t index, const eng_vector_t *position,
    bool box, bool first, float *height);

/* core/animation.c */
/* CKObjectAnimation::SetStep: its entity's local pose at `step` (0..1);
 * false when no target or a missing transform channel cannot be recovered. */
bool eng_object_animation_set_step(nmo_engine_t *engine, nmo_object_id_t animation, float step);
bool eng_object_animation_apply_step(nmo_engine_t *engine, nmo_object_id_t animation, float step, nmo_object_id_t keyed);
bool eng_object_animation_set_frame(nmo_engine_t *engine, nmo_object_id_t animation, float frame, bool force);
void eng_entity_ignore_animations(nmo_engine_t *engine, int32_t entity, bool ignore, bool hierarchy);
/* CKAnimation::SetStep: an object animation's, or each of a keyed animation's */
bool eng_animation_set_step(nmo_engine_t *engine, nmo_object_id_t animation, float step);

/* CKCurve::GetPos: the world position at `step` (0..1) of the curve's length */
bool eng_curve_position(nmo_engine_t *engine, nmo_object_id_t curve, float step, eng_vector_t *out);
/* CKCurve::GetLength, in the curve's local space after fitting and tangents. */
bool eng_curve_length(nmo_engine_t *engine, nmo_object_id_t curve, float *out);

#endif /* NMO_ENGINE_TRANSFORM_H */
