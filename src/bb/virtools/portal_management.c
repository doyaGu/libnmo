/* 3DTransfo's Portals Manager runs once per rendered frame, independently of its script. */
#include "virtools.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"

#include <math.h>

typedef struct portal_ids { nmo_object_id_t *data; size_t count, capacity; } portal_ids_t;
typedef struct portal_visit {
    nmo_object_id_t place, from;
    size_t level, link;
    nmo_rect_t extents;
    bool entered;
} portal_visit_t;
typedef struct portal_manager {
    bool active;
    int32_t depth;
    nmo_object_id_t behavior, cameras, trans_group;
    portal_ids_t places, path, current, rendered, trans;
    ENG_VEC(portal_visit_t) visits;
    eng_matrix_t projection;
} portal_manager_t;

static void reset(nmo_engine_t *engine, void *data);
static void destroy(nmo_engine_t *engine, void *data);
static void render(nmo_engine_t *engine, void *data, eng_render_stage_t stage, const eng_render_args_t *args);
static const eng_manager_desc_t portal_manager = {.name = "Portals Manager",.state_size = sizeof(portal_manager_t),.reset = reset,.destroy = destroy,.render = render};

static bool contains(const portal_ids_t *ids, nmo_object_id_t id)
{
    for (size_t i = 0; i < ids->count; i++) if (ids->data[i] == id) return true;
    return false;
}

static void append(portal_ids_t *ids, nmo_object_id_t id, bool unique)
{
    if (unique && contains(ids,id)) return;
    nmo_object_id_t *slot = NULL; if (ENG_VEC_PUSH(*ids,slot)) *slot = id;
}

static void collect_places(nmo_engine_t *engine, portal_ids_t *ids)
{
    ids->count = 0;
    for (size_t i = 0; i < engine->entities.count; i++) {
        nmo_object_id_t id = engine->entities.data[i].id;
        if (!engine->entities.data[i].destroyed && nmo_engine_object_class(engine,id) == NMO_CID_PLACE) append(ids,id,false);
    }
}

static bool connected(nmo_engine_t *engine, nmo_object_id_t place)
{
    const eng_portal_list_t *links = eng_place_portals(engine,place); return links != NULL && links->count != 0;
}

static nmo_object_id_t group(nmo_engine_t *engine, nmo_object_id_t id)
{
    return eng_map_get(&engine->destroyed,id) < 0 && nmo_engine_object_class(engine,id) == NMO_CID_GROUP ? id : 0;
}

static void show_group(nmo_engine_t *engine, nmo_object_id_t id, int show)
{
    int32_t count = nmo_engine_group_count(engine,id);
    for (int32_t i = 0; i < count; i++) eng_show(engine,nmo_engine_group_object(engine,id,i),show);
}

static bool bounds(nmo_engine_t *engine, nmo_object_id_t id, eng_vector_t *min, eng_vector_t *max)
{
    return eng_entity_hierarchical_box(engine,eng_entity_index(engine,id),min,max);
}

static bool overlap(eng_vector_t a, eng_vector_t b, eng_vector_t c, eng_vector_t d)
{
    return a.x <= d.x && c.x <= b.x && a.y <= d.y && c.y <= b.y && a.z <= d.z && c.z <= b.z;
}

static bool clip(nmo_rect_t *rect, nmo_rect_t through)
{
    if (!(rect->left < through.right && rect->right >= through.left && rect->top < through.bottom && rect->bottom >= through.top)) return false;
    rect->left = fmaxf(rect->left,through.left); rect->right = fminf(rect->right,through.right);
    rect->top = fmaxf(rect->top,through.top); rect->bottom = fminf(rect->bottom,through.bottom); return true;
}

/* Mutating a Collection does not call DataChanged; the native manager only
 * notifies Current Places and Rendered Places, once after each camera. */
static void output(nmo_engine_t *engine, portal_manager_t *manager, int pos, const portal_ids_t *ids, bool notify)
{
    int32_t index = eng_map_get(&engine->node_map,manager->behavior);
    if (index < 0 || eng_map_get(&engine->destroyed,manager->behavior) >= 0) return;
    const eng_node_t *node = &engine->nodes.data[index]; eng_param_t *param = eng_node_param(engine,node->first_pout,node->pout_count,pos);
    if (param == NULL || !eng_param_resize(param,ids->count*sizeof(nmo_object_id_t))) return;
    if (ids->count != 0) memcpy(param->data,ids->data,param->size);
    if (notify) eng_param_copy(engine,param,param,false);
}

static void show_trans(nmo_engine_t *engine, portal_manager_t *manager, nmo_object_id_t place, nmo_rect_t extents)
{
    eng_vector_t min, max; if (!bounds(engine,place,&min,&max)) return;
    int32_t count = nmo_engine_group_count(engine,manager->trans_group);
    for (int32_t i = 0; i < count; i++) {
        nmo_object_id_t id = nmo_engine_group_object(engine,manager->trans_group,i); eng_vector_t low, high; nmo_rect_t screen;
        if (nmo_engine_object_is_visible(engine,id) || !bounds(engine,id,&low,&high) || !overlap(min,max,low,high)) continue;
        if (eng_project_box(&manager->projection,&low,&high,&screen) && clip(&screen,extents)) { eng_show(engine,id,1); append(&manager->trans,id,false); }
    }
}

static void visit(nmo_engine_t *engine, portal_manager_t *manager, nmo_object_id_t root)
{
    portal_visit_t *slot = NULL; manager->visits.count = 0;
    if (!ENG_VEC_PUSH(manager->visits,slot)) return;
    *slot = (portal_visit_t){.place = root,.extents = {0,0,1,1}};
    while (manager->visits.count != 0) {
        portal_visit_t *current = &manager->visits.data[manager->visits.count-1];
        if (!current->entered) {
            if (contains(&manager->path,current->place)) { manager->visits.count--; continue; }
            current->from = manager->path.count != 0 ? manager->path.data[manager->path.count-1] : 0;
            size_t before = manager->path.count; append(&manager->path,current->place,false);
            if (manager->path.count == before) { manager->visits.count--; continue; }
            if (manager->cameras == 0) {
                nmo_rect_t rect = eng_place_viewport_clip(engine,current->place);
                if (rect.left == rect.right || rect.top == rect.bottom) rect = current->extents;
                else { rect.left = fminf(rect.left,current->extents.left); rect.top = fminf(rect.top,current->extents.top); rect.right = fmaxf(rect.right,current->extents.right); rect.bottom = fmaxf(rect.bottom,current->extents.bottom); }
                eng_place_set_viewport_clip(engine,current->place,rect);
            }
            eng_show(engine,current->place,1); append(&manager->rendered,current->place,true); show_trans(engine,manager,current->place,current->extents);
            if (manager->depth > 0 && current->level == (size_t)manager->depth-1) {
                manager->visits.count--; continue; /* Original DLL leaves this place on the caller stack. */
            }
            current->entered = true;
        }
        const eng_portal_list_t *links = eng_place_portals(engine,current->place);
        if (links == NULL || current->link >= links->count) { manager->visits.count--; if (manager->path.count != 0) manager->path.count--; continue; }
        eng_portal_link_t link = links->data[current->link++];
        if (link.place == current->from || eng_entity_index(engine,link.place) < 0) continue;
        nmo_rect_t extents = current->extents;
        if (link.portal != 0) {
            int32_t entity = eng_entity_index(engine,link.portal);
            if (entity < 0 || !nmo_engine_object_is_active(engine,link.portal) || !eng_in_scene(engine,link.portal)) continue;
            eng_vector_t min = {-1,-1,-1}, max = {1,1,1};
            if (eng_entity_has_mesh(engine,entity)) eng_entity_local_box(engine,entity,&min,&max);
            eng_matrix_t projection; eng_matrix_multiply4(&projection,&manager->projection,&engine->entities.data[entity].world);
            if (!eng_project_box(&projection,&min,&max,&extents) || !clip(&extents,current->extents)) continue;
        }
        size_t level = current->level+1;
        if (ENG_VEC_PUSH(manager->visits,slot)) *slot = (portal_visit_t){.place = link.place,.level = level,.extents = extents};
    }
}

static void camera(nmo_engine_t *engine, portal_manager_t *manager, nmo_object_id_t id)
{
    eng_vector_t origin; if (!eng_camera_view_projection(engine,id,&manager->projection,&origin)) return;
    bool inside = false;
    for (size_t i = 0; i < manager->places.count; i++) {
        nmo_object_id_t place = manager->places.data[i]; eng_vector_t min, max;
        if (!connected(engine,place) || !bounds(engine,place,&min,&max)) continue;
        if (origin.x >= min.x && origin.x <= max.x && origin.y >= min.y && origin.y <= max.y && origin.z >= min.z && origin.z <= max.z) {
            append(&manager->current,place,false); inside = true; visit(engine,manager,place);
        }
    }
    if (!inside) {
        for (size_t i = 0; i < manager->places.count; i++) if (connected(engine,manager->places.data[i])) { eng_show(engine,manager->places.data[i],1); append(&manager->rendered,manager->places.data[i],false); }
        int32_t count = nmo_engine_group_count(engine,manager->trans_group);
        for (int32_t i = 0; i < count; i++) { nmo_object_id_t object = nmo_engine_group_object(engine,manager->trans_group,i); eng_show(engine,object,1); append(&manager->trans,object,false); }
    }
    output(engine,manager,0,&manager->current,true); output(engine,manager,1,&manager->rendered,true); output(engine,manager,2,&manager->trans,false);
}

static void render(nmo_engine_t *engine, void *data, eng_render_stage_t stage, const eng_render_args_t *args)
{
    (void)args; portal_manager_t *manager = data;
    if (stage != ENG_RENDER_PREPARE || !manager->active || !engine->started) return;
    manager->current.count = manager->rendered.count = manager->trans.count = manager->path.count = 0;
    for (int i = 0; i < 3; i++) output(engine,manager,i,&manager->current,false);
    int32_t node = eng_map_get(&engine->node_map,manager->behavior); manager->trans_group = 0;
    if (node >= 0 && eng_map_get(&engine->destroyed,manager->behavior) < 0) {
        blk_t block = {engine,node}; manager->trans_group = group(engine,nmo_engine_block_get_input_object(&block,2));
        int32_t count = nmo_engine_group_count(engine,manager->trans_group);
        for (int32_t i = 0; i < count; i++) if (eng_entity_index(engine,nmo_engine_group_object(engine,manager->trans_group,i)) < 0) { manager->trans_group = 0; break; }
    }
    collect_places(engine,&manager->places);
    for (size_t i = 0; i < manager->places.count; i++) if (connected(engine,manager->places.data[i])) { eng_show(engine,manager->places.data[i],2); eng_place_set_viewport_clip(engine,manager->places.data[i],(nmo_rect_t){0}); }
    show_group(engine,manager->trans_group,2);
    if (manager->cameras != 0) {
        int32_t count = nmo_engine_group_count(engine,manager->cameras);
        for (int32_t i = 0; i < count; i++) camera(engine,manager,nmo_engine_group_object(engine,manager->cameras,i));
    } else camera(engine,manager,engine->render.camera);
}

static void reset(nmo_engine_t *engine, void *data)
{
    portal_manager_t *manager = data;
    if (manager->active) {
        collect_places(engine,&manager->places);
        for (size_t i = 0; i < manager->places.count; i++) if (connected(engine,manager->places.data[i])) { eng_show(engine,manager->places.data[i],1); eng_place_set_viewport_clip(engine,manager->places.data[i],(nmo_rect_t){0}); }
        show_group(engine,manager->trans_group,1);
    }
    manager->active = false; manager->depth = 0; manager->behavior = manager->cameras = manager->trans_group = 0;
}

static void destroy(nmo_engine_t *engine, void *data)
{
    (void)engine; portal_manager_t *manager = data;
    free(manager->places.data); free(manager->path.data); free(manager->current.data); free(manager->rendered.data); free(manager->trans.data); free(manager->visits.data);
}

static void generate(nmo_engine_t *engine)
{
    portal_ids_t places = {0}; collect_places(engine,&places);
    for (size_t i = 0; i < places.count; i++) {
        eng_vector_t min, max; if (!bounds(engine,places.data[i],&min,&max)) continue;
        for (size_t j = 0; j < places.count; j++) {
            eng_vector_t low, high; if (i == j || !bounds(engine,places.data[j],&low,&high)) continue;
            eng_vector_t expanded = {max.x+.1f,max.y+.1f,max.z+.1f}, other = {high.x+.1f,high.y+.1f,high.z+.1f};
            if (!overlap(min,expanded,low,other)) continue;
            const eng_portal_list_t *links = eng_place_portals(engine,places.data[j]); bool exists = false;
            for (size_t k = 0; links != NULL && k < links->count; k++) if (links->data[k].place == places.data[i]) { exists = true; break; }
            eng_matrix_t fit; if (exists || !eng_place_best_fit(engine,places.data[i],places.data[j],&fit)) continue;
            const char *a = nmo_engine_object_name(engine,places.data[i]), *b = nmo_engine_object_name(engine,places.data[j]); char *name = eng_text_printf("Portal %s-%s",a != NULL ? a : "",b != NULL ? b : "");
            nmo_object_id_t id = name != NULL ? eng_create_object(engine,NMO_CID_3DENTITY,name) : 0; free(name);
            int32_t entity = eng_entity_index(engine,id); if (entity < 0) continue;
            engine->entities.data[entity].entity_flags |= CK_3DENTITY_PORTAL; eng_entity_set_world_matrix(engine,entity,&fit,false);
            eng_place_add_portal(engine,places.data[i],places.data[j],id); nmo_engine_activate_object(engine,id,true);
        }
    }
    free(places.data);
}

static int bb_portal_management(blk_t *b)
{
    portal_manager_t *manager = eng_manager_state(b->engine,&portal_manager); if (manager == NULL) return NMO_ENGINE_BR_GENERICERROR;
    manager->cameras = group(b->engine,nmo_engine_block_get_input_object(b,0)); manager->behavior = block_node(b)->id; manager->depth = get_in_int(b,1,0);
    if (IN_ACTIVE(b,0)) { IN_OFF(b,0); OUT_ON(b,0); if (get_local_int(b,0,0) != 0) generate(b->engine); manager->active = true; }
    if (IN_ACTIVE(b,1)) {
        IN_OFF(b,1); OUT_ON(b,1); manager->active = false; collect_places(b->engine,&manager->places);
        for (size_t i = 0; i < manager->places.count; i++) if (connected(b->engine,manager->places.data[i])) eng_show(b->engine,manager->places.data[i],1);
        return NMO_ENGINE_BR_OK;
    }
    return NMO_ENGINE_BR_ACTIVATENEXTFRAME;
}

nmo_status_t eng_register_portal_management(nmo_engine_t *engine)
{
    nmo_status_t status = eng_add_manager(engine,&portal_manager,NULL);
    static const nmo_engine_block_desc_t block = {NMO_GUID_INIT(0x55041717,0x30817370),"Portal Management",bb_portal_management,NULL,NULL};
    return status == NMO_OK ? nmo_engine_register_block(engine,&block) : status;
}
