#include "../engine_internal.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>

static eng_vector_t row(const eng_matrix_t *matrix, int index)
{
    return (eng_vector_t){matrix->m[index][0],matrix->m[index][1],matrix->m[index][2]};
}

static const nmo_camera_state_t *camera_state(nmo_engine_t *engine)
{
    nmo_object_t *object = eng_entity_index(engine,engine->render.camera) >= 0 ? eng_find_object(engine,engine->render.camera) : NULL;
    return object != NULL ? nmo_type_query_object_get_ancestor_state_by_guid(engine->types,object,CKPGUID_CAMERA) : NULL;
}

void eng_render_viewpoint(nmo_engine_t *engine, eng_matrix_t *out)
{
    eng_matrix_identity(out);
    if (camera_state(engine) != NULL) *out = engine->entities.data[eng_entity_index(engine,engine->render.camera)].world;
}

void eng_render_project(nmo_engine_t *engine, const eng_vector_t *world, eng_vector_t *screen)
{
    eng_matrix_t eye, inverse;
    eng_render_viewpoint(engine,&eye); eng_matrix_identity(&inverse); eng_matrix_inverse(&inverse,&eye);
    eng_vector_t point; eng_vector_transform(&point,&inverse,world);
    const nmo_camera_state_t *camera = camera_state(engine);
    bool perspective = camera == NULL || (camera->projection_type & 1u) != 0;
    float fov = camera != NULL ? camera->fov : .5f, zoom = camera != NULL ? camera->orthographic_zoom : 1;
    float near_plane = 1, far_plane = 4000;
    if (camera != NULL) eng_camera_planes(engine,engine->render.camera,&near_plane,&far_plane);
    const eng_screen_mode_t *mode = &eng_screen_modes[engine->render.screen_mode];
    float aspect = (float)mode->width/(float)mode->height, depth = far_plane-near_plane;
    float w = perspective ? point.z : 1;
    if (w == 0) { *screen = (eng_vector_t){0}; return; }
    double half = (double)fov*.5;
    float scale = perspective ? (float)(cos(half)/sin(half)) : zoom;
    *screen = (eng_vector_t){
        (point.x*scale/w*.5f+.5f)*(float)mode->width,
        (.5f-point.y*scale*aspect/w*.5f)*(float)mode->height,
        (perspective ? (point.z*far_plane-near_plane*far_plane)/depth : (point.z-near_plane)/depth)/w};
}

void eng_entity_billboard_matrix(nmo_engine_t *engine, int32_t entity,
    const eng_vector_t *normal, const eng_vector_t *up, eng_matrix_t *out)
{
    eng_matrix_t eye, local, view;
    eng_render_viewpoint(engine,&eye); eng_matrix_identity(&local); eng_matrix_identity(&view);
    eng_vector_t dir = row(&eye,2), vu = row(&eye,1), right = row(&eye,0);
    eng_vector_normalize(&dir); eng_vector_normalize(&vu); eng_vector_normalize(&right);
    eng_vector_t cross = eng_vector_cross(up,normal);
    const eng_vector_t columns[] = {*normal,*up,cross};
    for (int i = 0; i < 3; i++) {
        local.m[0][i] = columns[i].x; local.m[1][i] = columns[i].y; local.m[2][i] = columns[i].z;
    }
    dir = (eng_vector_t){-dir.x,-dir.y,-dir.z}; right = (eng_vector_t){-right.x,-right.y,-right.z};
    memcpy(view.m[0],&dir,sizeof(dir)); memcpy(view.m[1],&vu,sizeof(vu)); memcpy(view.m[2],&right,sizeof(right));
    eng_matrix_multiply(out,&view,&local);
    memcpy(out->m[3],engine->entities.data[entity].world.m[3],3*sizeof(float));
    /* Ballance's 3DTransfo.dll applies this matrix without restoring mesh scale. */
}
