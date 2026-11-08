/**
 * @file interface_layouts.c
 * @brief Ports of the Interface layout blocks (Interface/Layouts).
 *
 * Each function does what the block's function in Ballance's Interface.dll
 * does (read from the DLL's disassembly). The 2D layouts place 2D entities in
 * render context pixels; the 3D ones place 3D entities by world positions
 * and the model's world scale, its orientation copied, as the DLL does
 * (the offsets are along the world axes whatever the model's orientation).
 */

#include "virtools.h"

#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "type/nmo_type_guids.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LAYOUT_LEFT 1
#define LAYOUT_RIGHT 2
#define LAYOUT_TOP 4
#define LAYOUT_BOTTOM 8

static bool is_of_class(nmo_engine_t *engine, nmo_object_id_t id, uint32_t class_id)
{
    return id != 0 && eng_find_object(engine, id) != NULL &&
           nmo_type_registry_is_class_derived_from(engine->types, (uint32_t)nmo_engine_object_class(engine, id),
                                                   class_id);
}

/* CKGroup::GetCommonClassID: the closest class every object of the group is of */
static uint32_t group_common_class(nmo_engine_t *engine, nmo_object_id_t group)
{
    int32_t count = nmo_engine_group_count(engine, group);
    nmo_object_id_t first = count > 0 ? nmo_engine_group_object(engine, group, 0) : 0;
    uint32_t common = first != 0 ? (uint32_t)nmo_engine_object_class(engine, first) : NMO_CID_BEOBJECT;
    for (int32_t i = 1; i < count; i++) {
        nmo_object_id_t object = nmo_engine_group_object(engine, group, i);
        if (object == 0 || is_of_class(engine, object, common)) {
            continue;
        }
        uint32_t parent = common;
        while (parent != 0 && !is_of_class(engine, object, parent)) {
            parent = nmo_type_registry_get_class_parent(engine->types, parent);
        }
        common = parent != 0 ? parent : NMO_CID_OBJECT;
    }
    return common;
}

/* CK2dEntity::GetPosition and GetSize, in render context pixels */
static void entity_2d_place(nmo_engine_t *engine, nmo_object_id_t id, float position[2], float size[2])
{
    const eng_2d_t *entity = eng_2d_entity(engine, id);
    nmo_rect_t rect = entity != NULL ? entity->rect : (nmo_rect_t){0.0f, 0.0f, 0.0f, 0.0f};
    if (position != NULL) {
        position[0] = rect.left;
        position[1] = rect.top;
    }
    if (size != NULL) {
        size[0] = rect.right - rect.left;
        size[1] = rect.bottom - rect.top;
    }
}

static nmo_object_id_t array_object(nmo_engine_t *engine, nmo_object_id_t array, int32_t row, int32_t column)
{
    nmo_engine_cell_t cell;
    return nmo_engine_array_get_cell(engine, array, row, column, &cell) == NMO_OK ? (nmo_object_id_t)cell.value : 0;
}

/*
 * Align 2D: the target 2D entity moved to the left, right or middle (Center)
 * of the Model's rect (the render context's without one), less the
 * margins, and to its top, bottom or middle.
 */
static int bb_align_2d(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t frame = nmo_engine_block_target(b);
    if (eng_2d_entity(engine, frame) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_object_id_t model = nmo_engine_block_get_input_object(b, 0);
    nmo_rect_t model_rect = {0.0f, 0.0f, 0.0f, 0.0f};
    if (model != 0 && eng_2d_entity(engine, model) != NULL) {
        model_rect = eng_2d_entity(engine, model)->rect;
    } else if (model == 0) {
        const eng_screen_mode_t *mode = &eng_screen_modes[engine->render.screen_mode];
        model_rect = (nmo_rect_t){0.0f, 0.0f, (float)mode->width, (float)mode->height};
    }
    int32_t alignment = get_in_int(b, 1, 0);
    bool center = get_in_int(b, 2, 0) != 0;
    nmo_rect_t margin = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 3, &margin, sizeof(margin));
    model_rect.left += margin.left;
    model_rect.top += margin.top;
    model_rect.bottom -= margin.bottom;
    model_rect.right -= margin.right;
    nmo_rect_t rect = eng_2d_entity(engine, frame)->rect;
    float to;
    if (alignment & LAYOUT_LEFT) {
        rect.right = model_rect.left - rect.left + rect.right;
        rect.left = model_rect.left;
    } else if ((alignment & LAYOUT_RIGHT) || center) {
        to = (alignment & LAYOUT_RIGHT)
                 ? model_rect.right - (rect.right - rect.left)
                 : (float)((double)(model_rect.right - model_rect.left) * 0.5 + model_rect.left -
                           (double)(rect.right - rect.left) * 0.5);
        rect.right = to - rect.left + rect.right;
        rect.left = to;
    }
    if (alignment & LAYOUT_TOP) {
        rect.bottom = model_rect.top - rect.top + rect.bottom;
        rect.top = model_rect.top;
    } else if ((alignment & LAYOUT_BOTTOM) || center) {
        to = (alignment & LAYOUT_BOTTOM)
                 ? model_rect.bottom - (rect.bottom - rect.top)
                 : (float)((double)(model_rect.bottom - model_rect.top) * 0.5 + model_rect.top -
                           (double)(rect.bottom - rect.top) * 0.5);
        rect.bottom = to - rect.top + rect.bottom;
        rect.top = to;
    }
    eng_2d_set_rect(engine, frame, rect, false);
    return NMO_ENGINE_BR_OK;
}

/*
 * Align 3D: the target takes the Model's orientation and is moved against
 * its sides (the model's world scale being its half size), less the
 * margins: left or right, top or bottom, front or back (Support), or onto
 * the model's centre on an axis with Center.
 */
static int bb_align_3d(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    int32_t frame = target_entity(b);
    if (frame < 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    int32_t model = input_entity(b, 0);
    if (model < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    int32_t alignment = get_in_int(b, 1, 5);
    int32_t support = get_in_int(b, 2, 1);
    bool center = get_in_int(b, 3, 0) != 0;
    nmo_rect_t margin = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 4, &margin, sizeof(margin));
    float depth[2] = {0.0f, 0.0f};
    nmo_engine_block_get_input_value(b, 5, depth, sizeof(depth));
    eng_vector_t model_position = eng_entity_position(engine, model, -1);
    eng_vector_t frame_position = eng_entity_position(engine, frame, -1);
    eng_vector_t model_scale = eng_entity_scale(engine, model, false);
    eng_vector_t frame_scale = eng_entity_scale(engine, frame, false);
    eng_vector_t dir, up;
    eng_entity_orientation(engine, model, &dir, &up, NULL, -1);
    eng_entity_set_orientation(engine, frame, &dir, &up, NULL, -1, false);
    if (alignment & LAYOUT_LEFT) {
        frame_position.x = model_position.x - model_scale.x + margin.left + frame_scale.x;
    } else if (alignment & LAYOUT_RIGHT) {
        frame_position.x = model_scale.x + model_position.x - frame_scale.x - margin.right;
    } else if (center) {
        frame_position.x = model_position.x;
    }
    if (alignment & LAYOUT_TOP) {
        frame_position.y = model_scale.y + model_position.y - frame_scale.y - margin.top;
    } else if (alignment & LAYOUT_BOTTOM) {
        frame_position.y = model_position.y - model_scale.y + margin.bottom + frame_scale.y;
    } else if (center) {
        frame_position.y = model_position.y;
    }
    if (support & 1) {
        frame_position.z = model_position.z - model_scale.z + depth[0] + frame_scale.z;
    } else if (support & 4) {
        frame_position.z = model_scale.z + model_position.z - frame_scale.z - depth[1];
    } else if (center) {
        frame_position.z = model_position.z;
    }
    eng_entity_set_position(engine, frame, &frame_position, -1, false);
    return NMO_ENGINE_BR_OK;
}

/*
 * 2D Curve Layout: the 2D entities of the first column of Components spread
 * along the target curve (the i-th at i/(n-1) of it), projected into the
 * render context; each takes the point's depth in the camera's space as its
 * Z order, the second column's size when there is one, and is centred on
 * the point with Center.
 */
static int bb_curve_layout_2d(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    nmo_object_id_t curve = nmo_engine_block_target(b);
    int32_t curve_entity = eng_entity_index(engine, curve);
    if (curve_entity < 0 || !is_of_class(engine, curve, NMO_CID_CURVE)) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_object_id_t array = nmo_engine_block_get_input_object(b, 0);
    if (array == 0 || eng_find_array(engine, array) == NULL) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t rows = nmo_engine_array_row_count(engine, array);
    if (rows == 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t columns = nmo_engine_array_column_count(engine, array);
    if (columns == 0 || nmo_engine_array_column_type(engine, array, 0) != CKARRAYTYPE_OBJECT) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    if (columns > 1 && (nmo_engine_array_column_type(engine, array, 1) != CKARRAYTYPE_PARAMETER ||
                        !nmo_guid_equals(eng_find_array(engine, array)->columns[1].param_type, CKPGUID_2DVECTOR))) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    bool center = get_in_int(b, 1, 0) != 0;
    int32_t camera = engine->render.camera != 0 ? eng_entity_index(engine, engine->render.camera) : -1;
    for (int32_t i = 0; i < rows; i++) {
        nmo_object_id_t entity = array_object(engine, array, i, 0);
        if (eng_2d_entity(engine, entity) == NULL) {
            continue;
        }
        eng_vector_t local = {0.0f, 0.0f, 0.0f}, world, screen, in_camera;
        eng_curve_local_position(engine, curve, (float)((double)i / (double)(rows - 1)), &local);
        eng_entity_transform(engine, curve_entity, &world, &local, -1);
        eng_render_project(engine, &world, &screen);
        eng_entity_transform(engine, curve_entity, &in_camera, &local, camera);
        eng_2d_entity(engine, entity)->z_order = (int32_t)in_camera.z;
        float position[2] = {screen.x, screen.y};
        float size[2] = {0.0f, 0.0f};
        if (columns > 1) {
            nmo_engine_cell_t cell;
            if (nmo_engine_array_get_cell(engine, array, i, 1, &cell) == NMO_OK && cell.data != NULL &&
                cell.size >= sizeof(size)) {
                memcpy(size, cell.data, sizeof(size));
            }
            eng_2d_set_size(engine, entity, size[0], size[1], false, false);
        }
        if (center) {
            entity_2d_place(engine, entity, NULL, size);
            position[0] -= size[0] * 0.5f;
            position[1] -= size[1] * 0.5f;
        }
        eng_2d_set_position(engine, entity, position[0], position[1], false, false, 0);
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * 3D Curve Layout: the 3D entities of the target group spread along the
 * curve from A to B; with Follow each turns its Direction axis along the
 * curve there (BB_Orientate), rolled by Roll.
 */
static int bb_curve_layout_3d(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t curve = nmo_engine_block_get_input_object(b, 0);
    if (curve == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_object_id_t group = nmo_engine_block_target(b);
    int32_t count = group != 0 ? nmo_engine_group_count(engine, group) : 0;
    if (count <= 0 ||
        !nmo_type_registry_is_class_derived_from(engine->types, group_common_class(engine, group), NMO_CID_3DENTITY)) {
        return NMO_ENGINE_BR_OK;
    }
    float a = get_in_float(b, 1, 0.0f), end = get_in_float(b, 2, 1.0f);
    bool follow = get_in_int(b, 3, 1) != 0;
    float roll = get_in_float(b, 4, 0.0f);
    int32_t direction = get_in_int(b, 5, 5);
    bool hierarchy = get_in_int(b, 6, 1) != 0;
    float step = (float)((double)(end - a) / (double)(count == 1 ? 1 : count - 1));
    float t = a;
    for (int32_t i = 0; i < count; i++, t += step) {
        int32_t entity = eng_entity_index(engine, nmo_engine_group_object(engine, group, i));
        eng_vector_t position = {0.0f, 0.0f, 0.0f}, dir = {0.0f, 0.0f, 0.0f};
        eng_curve_position_dir(engine, curve, t, &position, &dir);
        if (entity < 0) {
            continue;
        }
        eng_entity_set_position(engine, entity, &position, -1, !hierarchy);
        if (follow) {
            const eng_vector_t up = {0.0f, 1.0f, 0.0f};
            eng_bb_orientate(engine, entity, dir, direction, roll, up, 1.0f, !hierarchy);
        }
    }
    return NMO_ENGINE_BR_OK;
}

#define FLOW_LEFT 1
#define FLOW_CENTER 2
#define FLOW_RIGHT 4

/*
 * 2D Flow Layout: the 2D entities of the Components group placed one after
 * the other in the target, from its top (Top Margin) and its left, right or
 * centre (Side Margin), Gap Size apart, in a row (Horizontal Flow) or a
 * column; with Set Parent each becomes the target's child.
 */
static int bb_flow_layout_2d(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    nmo_object_id_t frame = nmo_engine_block_target(b);
    if (eng_2d_entity(engine, frame) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_object_id_t group = nmo_engine_block_get_input_object(b, 0);
    if (group == 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t alignment = get_in_int(b, 1, 1);
    int32_t top = get_in_int(b, 2, 10), side = get_in_int(b, 3, 10), gap = get_in_int(b, 4, 10);
    bool horizontal = get_in_int(b, 5, 1) != 0;
    bool set_parent = get_in_int(b, 6, 0) != 0;
    float place[2], size[2];
    entity_2d_place(engine, frame, place, size);
    int32_t count = nmo_engine_group_count(engine, group);
    if (alignment & FLOW_LEFT) {
        place[0] = (float)((double)side + place[0]);
    } else if (!(alignment & FLOW_CENTER)) {
        place[0] = (float)((double)size[0] - (double)side + place[0]);
    } else {
        place[0] = (float)((double)size[0] * 0.5 + place[0]);
        if (horizontal) {
            int32_t n = 0;
            float total = 0.0f;
            for (int32_t i = 0; i < count; i++) {
                nmo_object_id_t entity = nmo_engine_group_object(engine, group, i);
                if (eng_2d_entity(engine, entity) != NULL) {
                    float s[2];
                    entity_2d_place(engine, entity, NULL, s);
                    n++;
                    total += s[0];
                }
            }
            place[0] = (float)((double)place[0] - ((double)(gap * (n - 1)) + total) * 0.5);
        }
    }
    place[1] = (float)((double)top + place[1]);
    for (int32_t i = 0; i < count; i++) {
        nmo_object_id_t entity = nmo_engine_group_object(engine, group, i);
        if (eng_2d_entity(engine, entity) == NULL) {
            continue;
        }
        entity_2d_place(engine, entity, NULL, size);
        float at[2] = {place[0], place[1]};
        if (!horizontal && (alignment & FLOW_CENTER)) {
            at[0] = (float)((double)place[0] - (double)size[0] * 0.5);
        } else if (alignment & FLOW_RIGHT) {
            at[0] = place[0] - size[0];
        }
        eng_2d_set_position(engine, entity, at[0], at[1], false, false, 0);
        if (set_parent) {
            eng_2d_set_parent(engine, entity, frame);
        }
        if (horizontal) {
            double advance = (double)gap + size[0];
            place[0] = (float)((alignment & (FLOW_LEFT | FLOW_CENTER)) ? advance + place[0] : place[0] - advance);
        } else {
            place[1] = (float)((double)gap + size[1] + place[1]);
        }
    }
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * 3D Flow Layout: the 3D entities of the target group take the Model's
 * orientation and are placed one after the other against its top and its
 * left, right or centre, Gap Size apart, along Flow Direction (left-right,
 * top-bottom or front-back), each becoming the model's child.
 */
static int bb_flow_layout_3d(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t group = nmo_engine_block_target(b);
    if (group == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (!nmo_type_registry_is_class_derived_from(engine->types, group_common_class(engine, group), NMO_CID_3DENTITY)) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t count = nmo_engine_group_count(engine, group);
    if (count == 0) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t model = input_entity(b, 0);
    if (model < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    int32_t flow = get_in_int(b, 1, 1), alignment = get_in_int(b, 2, 1), support = get_in_int(b, 3, 1);
    float top = get_in_float(b, 4, 0.5f), side = get_in_float(b, 5, 0.5f), offset = get_in_float(b, 6, 0.0f);
    float gap = get_in_float(b, 7, 0.5f);
    bool keep = get_in_int(b, 8, 0) != 0;
    eng_vector_t place = eng_entity_position(engine, model, -1);
    eng_vector_t dir, up;
    eng_entity_orientation(engine, model, &dir, &up, NULL, -1);
    eng_vector_t scale = eng_entity_scale(engine, model, false);
    place.y = scale.y - top + place.y;
    if (support & 1) {
        place.z = place.z - (scale.z + offset);
    } else if (support & 2) {
        place.z = place.z - offset;
    } else {
        place.z = scale.z - offset + place.z;
    }
    if (alignment & FLOW_LEFT) {
        place.x = side - scale.x + place.x;
    } else if (!(alignment & FLOW_CENTER)) {
        place.x = scale.x - side + place.x;
    } else if (flow & 1) {
        int32_t n = 0;
        float total = 0.0f;
        for (int32_t i = 0; i < count; i++) {
            int32_t entity = eng_entity_index(engine, nmo_engine_group_object(engine, group, i));
            if (entity >= 0) {
                n++;
                float width = eng_entity_scale(engine, entity, false).x; /* |world matrix row 0| */
                total = width + width + total;
            }
        }
        if (n == 0) {
            return NMO_ENGINE_BR_OK;
        }
        place.x = (float)((double)place.x - ((double)(n - 1) * gap + total) * 0.5);
    }
    nmo_object_id_t model_id = nmo_engine_block_get_input_object(b, 0);
    for (int32_t i = 0; i < count; i++) {
        int32_t entity = eng_entity_index(engine, nmo_engine_group_object(engine, group, i));
        if (entity < 0) {
            continue;
        }
        scale = eng_entity_scale(engine, entity, false);
        eng_vector_t at = place;
        if ((flow & 1) && (alignment & (FLOW_LEFT | FLOW_CENTER))) {
            at.x = place.x + scale.x;
        } else if (alignment & FLOW_RIGHT) {
            at.x = place.x - scale.x;
        }
        at.y = at.y - scale.y;
        eng_entity_set_position(engine, entity, &at, -1, keep);
        eng_entity_set_orientation(engine, entity, &dir, &up, NULL, -1, keep);
        eng_entity_set_parent(engine, entity, eng_entity_index(engine, model_id), true);
        if (flow & 1) {
            if (alignment & (FLOW_LEFT | FLOW_CENTER)) {
                place.x = scale.x + scale.x + gap + place.x;
            } else if (alignment & FLOW_RIGHT) {
                place.x = place.x - (scale.x + scale.x + gap);
            }
        } else if (flow & 2) {
            place.y = place.y - (scale.y + scale.y + gap);
        } else {
            place.z = place.z - (scale.z + scale.z + gap);
        }
    }
    return NMO_ENGINE_BR_OK;
}

/*
 * 2D Grid Layout: the 2D entities of the Components array (every column of
 * objects) placed in the target's cells, row by row, inside the margins and
 * gaps: cells of an equal share of the target (Resize, which sizes each
 * entity to its cell) or as wide and high as the widest of each column and
 * the highest of each row (in whole pixels). Set Parent makes each the
 * target's child.
 */
static int bb_grid_layout_2d(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    nmo_object_id_t frame = nmo_engine_block_target(b);
    if (eng_2d_entity(engine, frame) == NULL) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    nmo_object_id_t array = nmo_engine_block_get_input_object(b, 0);
    if (array == 0 || eng_find_array(engine, array) == NULL) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    int32_t rows = nmo_engine_array_row_count(engine, array);
    int32_t columns = nmo_engine_array_column_count(engine, array);
    if (rows == 0) {
        return NMO_ENGINE_BR_PARAMETERERROR; /* "Invalid target array" */
    }
    for (int32_t c = 0; c < columns; c++) {
        if (nmo_engine_array_column_type(engine, array, c) != CKARRAYTYPE_OBJECT) {
            return NMO_ENGINE_BR_PARAMETERERROR;
        }
    }
    nmo_rect_t margins = {10.0f, 10.0f, 10.0f, 10.0f};
    nmo_engine_block_get_input_value(b, 1, &margins, sizeof(margins));
    int32_t hgap = get_in_int(b, 2, 10), vgap = get_in_int(b, 3, 10);
    bool resize = get_in_int(b, 4, 1) != 0;
    bool set_parent = get_in_int(b, 5, 0) != 0;
    float place[2], size[2];
    entity_2d_place(engine, frame, place, size);
    place[0] += margins.left;
    place[1] += margins.top;
    float start_x = place[0];
    float cell[2] = {0.0f, 0.0f};
    int32_t *widths = NULL, *heights = NULL;
    if (resize) {
        cell[0] = (float)(((double)size[0] - ((double)(hgap * (columns - 1)) + margins.right + margins.left)) /
                          (double)columns);
        cell[1] = (float)(((double)size[1] - ((double)(vgap * (rows - 1)) + margins.bottom + margins.top)) /
                          (double)rows);
    } else {
        widths = (int32_t *)calloc((size_t)(columns > 0 ? columns : 1), sizeof(int32_t));
        heights = (int32_t *)calloc((size_t)rows, sizeof(int32_t));
        if (widths == NULL || heights == NULL) {
            free(widths);
            free(heights);
            return NMO_ENGINE_BR_GENERICERROR;
        }
        for (int32_t r = 0; r < rows; r++) {
            for (int32_t c = 0; c < columns; c++) {
                nmo_object_id_t entity = array_object(engine, array, r, c);
                if (eng_2d_entity(engine, entity) == NULL) {
                    continue;
                }
                float s[2];
                entity_2d_place(engine, entity, NULL, s);
                if ((double)widths[c] < s[0]) {
                    widths[c] = (int32_t)s[0];
                }
                if ((double)heights[r] < s[1]) {
                    heights[r] = (int32_t)s[1];
                }
            }
        }
    }
    for (int32_t r = 0; r < rows; r++) {
        for (int32_t c = 0; c < columns; c++) {
            nmo_object_id_t entity = array_object(engine, array, r, c);
            if (eng_2d_entity(engine, entity) != NULL) {
                if (resize) {
                    eng_2d_set_size(engine, entity, cell[0], cell[1], false, false);
                }
                eng_2d_set_position(engine, entity, place[0], place[1], false, false, 0);
                if (set_parent) {
                    eng_2d_set_parent(engine, entity, frame);
                }
            }
            place[0] = (float)((double)hgap + (resize ? cell[0] : (float)widths[c]) + place[0]);
        }
        place[0] = start_x;
        place[1] = (float)((double)vgap + (resize ? cell[1] : (float)heights[r]) + place[1]);
    }
    free(widths);
    free(heights);
    OUT_ON(b, 0);
    return NMO_ENGINE_BR_OK;
}

/*
 * 3D Grid Layout: each data array of the target group is a layer of cells
 * inside the Model (its world scale its half size), front to back; each 3D
 * entity of an array is placed at the centre of its cell, takes the model's
 * orientation and becomes its child.
 */
static int bb_grid_layout_3d(blk_t *b)
{
    nmo_engine_t *engine = b->engine;
    IN_OFF(b, 0);
    OUT_ON(b, 0);
    nmo_object_id_t group = nmo_engine_block_target(b);
    if (group == 0) {
        return NMO_ENGINE_BR_OWNERERROR;
    }
    if (!nmo_type_registry_is_class_derived_from(engine->types, group_common_class(engine, group), NMO_CID_DATAARRAY)) {
        return NMO_ENGINE_BR_OK;
    }
    int32_t count = nmo_engine_group_count(engine, group);
    if (count == 0) {
        return NMO_ENGINE_BR_OK;
    }
    nmo_object_id_t model_id = nmo_engine_block_get_input_object(b, 0);
    int32_t model = eng_entity_index(engine, model_id);
    if (model < 0) {
        return NMO_ENGINE_BR_PARAMETERERROR;
    }
    nmo_rect_t margins = {1.0f, 1.0f, 1.0f, 1.0f};
    nmo_engine_block_get_input_value(b, 1, &margins, sizeof(margins));
    float front = get_in_float(b, 2, 0.5f), rear = get_in_float(b, 3, 0.5f);
    int32_t support = get_in_int(b, 4, 1);
    float hgap = get_in_float(b, 5, 0.5f), vgap = get_in_float(b, 6, 0.5f), dgap = get_in_float(b, 7, 0.5f);
    eng_vector_t place = eng_entity_position(engine, model, -1);
    eng_vector_t dir, up;
    eng_entity_orientation(engine, model, &dir, &up, NULL, -1);
    eng_vector_t scale = eng_entity_scale(engine, model, false);
    place.x = margins.left - scale.x + place.x;
    place.y = scale.y - margins.top + place.y;
    if (support & 1) {
        place.z = front - (scale.z + scale.z) + place.z;
    } else if (support & 2) {
        place.z = front - scale.z + place.z;
    } else {
        place.z = place.z + front;
    }
    eng_vector_t start = place;
    for (int32_t g = 0; g < count; g++) {
        nmo_object_id_t array = nmo_engine_group_object(engine, group, g);
        if (!is_of_class(engine, array, NMO_CID_DATAARRAY) || eng_find_array(engine, array) == NULL) {
            continue;
        }
        int32_t rows = nmo_engine_array_row_count(engine, array);
        int32_t columns = nmo_engine_array_column_count(engine, array);
        float cell_x = (float)(((double)scale.x - ((double)(columns - 1) * hgap + margins.right + margins.left) * 0.5) /
                               (double)columns);
        float cell_y = (float)(((double)scale.y - ((double)(rows - 1) * vgap + margins.bottom + margins.top) * 0.5) /
                               (double)rows);
        float cell_z = (float)(((double)scale.z - ((double)(count - 1) * dgap + rear + front) * 0.5) / (double)count);
        for (int32_t r = 0; r < rows; r++) {
            for (int32_t c = 0; c < columns; c++) {
                int32_t entity = eng_entity_index(engine, array_object(engine, array, r, c));
                if (entity >= 0) {
                    eng_entity_set_parent(engine, entity, model, true);
                    eng_vector_t at = {cell_x + place.x, place.y - cell_y, place.z + cell_z};
                    eng_entity_set_position(engine, entity, &at, -1, false);
                    eng_entity_set_orientation(engine, entity, &dir, &up, NULL, -1, false);
                }
                place.x = cell_x + cell_x + place.x + hgap;
            }
            place.x = start.x;
            place.y = place.y - (cell_y + cell_y + vgap);
        }
        place.x = start.x;
        place.y = start.y;
        place.z = cell_z + cell_z + place.z + dgap;
    }
    return NMO_ENGINE_BR_OK;
}

static const nmo_engine_block_desc_t eng_interface_layout_blocks[] = {
    {NMO_GUID_INIT(0x03385bc3, 0x1034102f), "Align 2D", bb_align_2d, NULL, NULL},
    {NMO_GUID_INIT(0x0f384bc3, 0x1b34102f), "Align 3D", bb_align_3d, NULL, NULL},
    {NMO_GUID_INIT(0x2956342c, 0x95246746), "2D Curve Layout", bb_curve_layout_2d, NULL, NULL},
    {NMO_GUID_INIT(0x2557342c, 0x15246746), "3D Curve Layout", bb_curve_layout_3d, NULL, NULL},
    {NMO_GUID_INIT(0x295f342c, 0x95e467e6), "2D Flow Layout", bb_flow_layout_2d, NULL, NULL},
    {NMO_GUID_INIT(0x245fb72c, 0xa5e437e6), "3D Flow Layout", bb_flow_layout_3d, NULL, NULL},
    {NMO_GUID_INIT(0x265f342c, 0x95e466e6), "2D Grid Layout", bb_grid_layout_2d, NULL, NULL},
    {NMO_GUID_INIT(0x665f342c, 0x9e1366e6), "3D Grid Layout", bb_grid_layout_3d, NULL, NULL},
};

nmo_status_t eng_register_interface_layouts(nmo_engine_t *engine)
{
    return ENG_REGISTER_BLOCKS(engine, eng_interface_layout_blocks);
}
