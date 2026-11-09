#include "../engine_internal.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>

bool eng_camera_view_projection(nmo_engine_t *engine, nmo_object_id_t camera,
    eng_matrix_t *out, eng_vector_t *near_origin)
{
    int32_t entity = eng_entity_index(engine,camera);
    const nmo_camera_state_t *state = entity >= 0 ? nmo_type_query_object_get_ancestor_state_by_guid(engine->types,eng_find_object(engine,camera),CKPGUID_CAMERA) : NULL;
    if (state == NULL) return false;
    float front, back, fov = state->fov, zoom = state->orthographic_zoom;
    eng_camera_planes(engine,camera,&front,&back); eng_camera_fov(engine,camera,&fov);
    eng_camera_orthographic_zoom(engine,camera,&zoom);
    double aspect = state->aspect_width > 0 && state->aspect_height > 0 ? (double)state->aspect_width/state->aspect_height : 1;
    double depth = (double)back-front;
    eng_matrix_t projection = {{{0}}}, inverse; eng_matrix_identity(&inverse);
    uint32_t projection_type = state->projection_type; eng_camera_projection_type(engine,camera,&projection_type);
    if (projection_type & 1u) {
        double scale = cos((double)fov*.5)/sin((double)fov*.5);
        projection.m[0][0] = (float)scale; projection.m[1][1] = (float)(scale*aspect);
        projection.m[2][2] = (float)(back/depth); projection.m[3][2] = (float)(-(double)front*back/depth); projection.m[2][3] = 1;
    } else {
        projection.m[0][0] = zoom; projection.m[1][1] = (float)(zoom*aspect);
        projection.m[2][2] = (float)(1/depth); projection.m[3][2] = (float)(-front/depth); projection.m[3][3] = 1;
    }
    eng_matrix_inverse(&inverse,&engine->entities.data[entity].world);
    eng_matrix_multiply4(out,&projection,&inverse);
    if (near_origin != NULL) eng_entity_transform(engine,entity,near_origin,&(eng_vector_t){0,0,front},-1);
    return true;
}

int eng_box_visibility(const eng_matrix_t *matrix, const eng_vector_t *min,
    const eng_vector_t *max, nmo_rect_t *out)
{
    if (!(min->x <= max->x && min->y <= max->y && min->z <= max->z)) return 0;
    double vertices[8][3]; unsigned combined = 0, common = 63;
    bool flat = max->z-min->z == 0; int count = flat ? 4 : 8;
    for (int i = 0; i < count; i++) {
        double p[3] = {i & (flat ? 1 : 4) ? max->x : min->x,i & 2 ? max->y : min->y,i & (flat ? 0 : 1) ? max->z : min->z}, h[4];
        for (int c = 0; c < 4; c++) {
            h[c] = p[0]*matrix->m[0][c]+p[1]*matrix->m[1][c]+p[2]*matrix->m[2][c]+matrix->m[3][c];
            if (!isfinite(h[c])) return 0;
        }
        unsigned flags = (h[0] < -h[3] ? 1u : 0) | (h[0] > h[3] ? 2u : 0) |
            (h[1] < -h[3] ? 4u : 0) | (h[1] > h[3] ? 8u : 0) | (h[2] < 0 ? 16u : 0) | (h[2] > h[3] ? 32u : 0);
        combined |= flags; common &= flags;
        vertices[i][0] = h[0]; vertices[i][1] = h[1]; vertices[i][2] = h[3];
    }
    if (common != 0) return 0;
    if (out == NULL) return combined != 0 ? 1 : 2;
    nmo_rect_t rect = {1e10f,1e10f,-1e10f,-1e10f};
    for (int i = 0; i < count; i++) {
        double denominator = fabs(vertices[i][2]);
        float x = (float)(vertices[i][0]/denominator*.5+.5), y = (float)(vertices[i][1]/denominator*-.5+.5);
        rect.left = rect.left < x ? rect.left : x; rect.right = rect.right > x ? rect.right : x;
        rect.top = rect.top < y ? rect.top : y; rect.bottom = rect.bottom > y ? rect.bottom : y;
    }
    if (combined & 16u) {
        if (combined & 1u) rect.left = 0;
        if (combined & 2u) rect.right = 1;
        if (combined & 8u) rect.top = 0;
        if (combined & 4u) rect.bottom = 1;
    }
    *out = rect; return combined != 0 ? 1 : 2;
}

bool eng_project_box(const eng_matrix_t *matrix, const eng_vector_t *min,
    const eng_vector_t *max, nmo_rect_t *out)
{
    return eng_box_visibility(matrix, min, max, out) != 0;
}
