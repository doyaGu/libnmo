#include "virtools.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

typedef struct billboard_surface {
    bool mesh, face_mode, triangle;
    nmo_object_id_t texture;
    eng_vector_t normal, up, positions[3];
    nmo_vector2_t uvs[3];
} billboard_surface_t;

static nmo_object_id_t material_texture(nmo_engine_t *engine, nmo_object_id_t id)
{
    eng_material_t *material = eng_map_get(&engine->destroyed,id) < 0 ? eng_material(engine,id) : NULL;
    nmo_object_id_t texture = material != NULL ? material->desc.texture : 0;
    nmo_object_t *object = eng_map_get(&engine->destroyed,texture) < 0 ? eng_find_object(engine,texture) : NULL;
    return object != NULL && nmo_type_query_object_get_ancestor_state_by_guid(engine->types,object,CKPGUID_TEXTURE) != NULL ? texture : 0;
}

static billboard_surface_t surface(nmo_engine_t *engine, nmo_object_id_t id, int32_t face)
{
    billboard_surface_t result = {.normal = {0,0,1}, .up = {0,1,0}};
    const nmo_3dentity_state_t *entity = nmo_type_query_object_get_ancestor_state_by_guid(engine->types,eng_find_object(engine,id),CKPGUID_3DENTITY);
    nmo_object_id_t mesh_id = entity != NULL ? eng_object_ref(engine,id,&entity->current_mesh) : 0;
    nmo_engine_mesh_t mesh;
    result.mesh = eng_map_get(&engine->destroyed,mesh_id) < 0 && nmo_engine_mesh(engine,mesh_id,&mesh);
    result.face_mode = result.mesh && face >= 0;
    if (result.face_mode) {
        if (face >= mesh.face_count || mesh.indices == NULL || mesh.vertices == NULL) return result;
        for (int i = 0; i < 3; i++) {
            uint16_t vertex = mesh.indices[(size_t)face*3+(size_t)i];
            if (vertex >= mesh.vertex_count) return result;
            const nmo_vertex_t *v = &mesh.vertices[vertex]; result.positions[i] = (eng_vector_t){v->position.x,v->position.y,v->position.z}; result.uvs[i] = v->uv;
        }
        result.triangle = true;
        eng_vector_t a = {result.positions[1].x-result.positions[0].x,result.positions[1].y-result.positions[0].y,result.positions[1].z-result.positions[0].z};
        eng_vector_t b = {result.positions[2].x-result.positions[0].x,result.positions[2].y-result.positions[0].y,result.positions[2].z-result.positions[0].z};
        result.normal = eng_vector_cross(&a,&b); eng_vector_normalize(&result.normal);
        if (mesh.faces != NULL) result.texture = material_texture(engine,nmo_engine_mesh_material(engine,mesh_id,(int32_t)mesh.faces[face].material_group_idx));
        if (result.texture != 0) {
            float u10 = result.uvs[1].x-result.uvs[0].x, u20 = result.uvs[2].x-result.uvs[0].x;
            float v10 = result.uvs[1].y-result.uvs[0].y, v20 = result.uvs[2].y-result.uvs[0].y, divisor = v10*u20-v20*u10;
            result.up = (eng_vector_t){(u20*a.x-u10*b.x)/divisor,(u20*a.y-u10*b.y)/divisor,(u20*a.z-u10*b.z)/divisor}; eng_vector_normalize(&result.up);
            result.up = (eng_vector_t){-result.up.x,-result.up.y,-result.up.z};
        } else {
            result.up = (eng_vector_t){-a.x,-a.y,-a.z}; eng_vector_normalize(&result.up);
        }
    } else {
        const eng_sprite3d_t *sprite = eng_sprite3d(engine,id);
        if (sprite != NULL) {
            result.texture = material_texture(engine,sprite->material);
            float x = (sprite->max.x-sprite->min.x)*.5f, y = (sprite->max.y-sprite->min.y)*.5f;
            result.positions[0] = (eng_vector_t){-x,-y,0}; result.positions[1] = (eng_vector_t){-x,y,0}; result.positions[2] = (eng_vector_t){x,y,0};
            result.uvs[0] = (nmo_vector2_t){0,1}; result.uvs[1] = (nmo_vector2_t){0,0}; result.uvs[2] = (nmo_vector2_t){1,0}; result.triangle = !result.mesh;
        }
    }
    return result;
}

static float texture_height(nmo_engine_t *engine, nmo_object_id_t id)
{
    nmo_engine_image_t image;
    if (nmo_engine_texture_image(engine,id,&image)) return (float)image.height;
    const nmo_texture_state_t *texture = nmo_type_query_object_get_ancestor_state_by_guid(engine->types,eng_find_object(engine,id),CKPGUID_TEXTURE);
    eng_texture_t *runtime = eng_texture(engine,id);
    if (texture == NULL) return 0;
    if (runtime != NULL && runtime->current_slot >= 0 && (uint32_t)runtime->current_slot < texture->slot_count && texture->raw_slots != NULL) return (float)texture->raw_slots[runtime->current_slot].height;
    return (float)texture->reader_height;
}

static float projected_size(nmo_engine_t *engine, int32_t entity, const billboard_surface_t *s)
{
    eng_vector_t screens[3];
    for (int i = 0; i < 3; i++) { eng_vector_t world; eng_entity_transform(engine,entity,&world,&s->positions[i],-1); eng_render_project(engine,&world,&screens[i]); }
    float min = screens[0].y, max = min, minv = s->uvs[0].y, maxv = minv;
    for (int i = 1; i < 3; i++) {
        if (screens[i].y < min) { min = screens[i].y; minv = s->uvs[i].y; }
        else if (screens[i].y > max) { max = screens[i].y; maxv = s->uvs[i].y; }
    }
    return (max-min)/(maxv-minv);
}

static int bb_billboard(blk_t *b)
{
    IN_OFF(b,0); OUT_ON(b,0);
    nmo_object_id_t target = nmo_engine_block_target(b); int32_t entity = eng_entity_index(b->engine,target);
    if (entity < 0) return NMO_ENGINE_BR_OWNERERROR;
    int32_t face = get_in_int(b,0,-1); float factor = get_in_float(b,3,1);
    billboard_surface_t s = surface(b->engine,target,face);
    if (!s.face_mode) { s.normal = get_in_vector(b,1,0,0,1); s.up = get_in_vector(b,2,0,1,0); }
    eng_matrix_t matrix; eng_entity_billboard_matrix(b->engine,entity,&s.normal,&s.up,&matrix);
    bool apply = get_in_int(b,4,1) != 0;
    if (apply) eng_entity_set_world_matrix(b->engine,entity,&matrix,false);
    if (s.texture != 0 && factor > 0 && s.triangle) {
        float old_size = projected_size(b->engine,entity,&s), desired_size = factor*texture_height(b->engine,s.texture);
        if (get_in_int(b,5,0) == 0) {
            eng_matrix_t eye, inverse; eng_render_viewpoint(b->engine,&eye); eng_matrix_identity(&inverse); eng_matrix_inverse(&inverse,&eye);
            eng_vector_t world = eng_entity_position(b->engine,entity,-1), position; eng_vector_transform(&position,&inverse,&world);
            position.z *= old_size/desired_size; eng_vector_transform(&world,&eye,&position);
            if (apply) eng_entity_set_position(b->engine,entity,&world,-1,false);
            memcpy(matrix.m[3],&world,sizeof(world));
        } else if (!s.mesh) {
            const eng_sprite3d_t *sprite = eng_sprite3d(b->engine,target);
            if (sprite != NULL) eng_sprite3d_set_size(b->engine,target,(nmo_vector2_t){(sprite->max.x-sprite->min.x)*desired_size/old_size,(sprite->max.y-sprite->min.y)*desired_size/old_size});
        }
    }
    if (!apply) nmo_engine_block_set_output_value(b,0,matrix.m,sizeof(matrix.m));
    return NMO_ENGINE_BR_OK;
}

nmo_status_t eng_register_billboard(nmo_engine_t *engine)
{
    static const nmo_engine_block_desc_t block = {NMO_GUID_INIT(0x641822b0,0x15aa42d7),"Billboard",bb_billboard,NULL,NULL};
    return nmo_engine_register_block(engine,&block);
}
