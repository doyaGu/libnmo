/**
 * @file test_engine_sprites.c
 * @brief CKSprite bitmaps and the blocks that load, key, place and edit 2D
 *        entities (Narratives' sprite Load, the Visuals 2D blocks).
 */

#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"

#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_sprite_schemas.h"

#include <stdio.h>
#include <stdlib.h>

#define GUID_SPRITE_LOAD NMO_GUID(0x00e85ae1, 0x317a771c)
#define GUID_SHOW_2D NMO_GUID(0xababa123, 0x123ababa)
#define GUID_HIDE_2D NMO_GUID(0x13579753, 0x13579753)
#define GUID_SET_COLOR_KEY_2D NMO_GUID(0x7bda540c, 0x39782384)
#define GUID_CHANGE_SPRITE_SLOT NMO_GUID(0x70f8028c, 0x723215e7)
#define GUID_SET_RECTANGLES NMO_GUID(0x3bcf7052, 0x59f40417)
#define GUID_SET_2D_PARENT NMO_GUID(0x14c70f07, 0x4f2a100a)
#define GUID_SET_2D_POSITION NMO_GUID(0x32d30604, 0x23b163fc)
#define GUID_EDIT_2D_ENTITY NMO_GUID(0x7fc54c91, 0x3f486670)
#define GUID_SPRITE_MULTI_ANGLE NMO_GUID(0x1ef926bd, 0x01ad167f)
#define GUID_DISPLAY_PROGRESSION_BAR NMO_GUID(0x04cf37cd, 0x7a246b85)

/* The files the engine loads, from the scratch directory (the last path stays allocated) */
static const char *resolve_scratch(void *user_data, const char *name)
{
    static char *path = NULL;
    (void)user_data;
    free(path);
    size_t dir = strlen(NMO_TEST_SCRATCH_DIR), length = strlen(name);
    path = (char *)malloc(dir + 1 + length + 1);
    if (path != NULL) {
        memcpy(path, NMO_TEST_SCRATCH_DIR, dir);
        path[dir] = '/';
        memcpy(path + dir + 1, name, length + 1);
    }
    return path;
}

static void put_u32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, 4);
}

/* "sprite.bmp" in the scratch folder: 4 x 2 at 24 bits, the top row black, white, black,
   white, the bottom row white (bottom-up BGR rows, 12 bytes each) */
static void write_sprite_bmp(void)
{
    uint8_t bmp[54 + 24];
    memset(bmp, 0, sizeof(bmp));
    bmp[0] = 'B';
    bmp[1] = 'M';
    put_u32(bmp + 2, sizeof(bmp));
    put_u32(bmp + 10, 54);
    put_u32(bmp + 14, 40);
    put_u32(bmp + 18, 4);
    put_u32(bmp + 22, 2);
    put_u32(bmp + 26, 1u | 24u << 16);
    memset(bmp + 54, 0xFF, 12);      /* bottom row */
    memset(bmp + 54 + 12 + 3, 0xFF, 3); /* top row, x = 1 */
    memset(bmp + 54 + 12 + 9, 0xFF, 3); /* x = 3 */
    const char *name = resolve_scratch(NULL, "sprite.bmp");
    FILE *out = name != NULL ? fopen(name, "wb") : NULL;
    ASSERT_NOT_NULL(out);
    ASSERT_EQ(sizeof(bmp), fwrite(bmp, 1, sizeof(bmp), out));
    fclose(out);
}

/* A block run when the script starts, after `*previous` */
static nmo_object_id_t chain(engine_fixture_t *f, nmo_guid_t guid, const char *name, nmo_object_id_t *previous)
{
    nmo_object_id_t id = fixture_block(f, guid, name);
    if (id != 0) {
        fixture_link(f, *previous != 0 ? BLOCK_OUT(f, *previous, 0) : f->script_in, BLOCK_IN(f, id, 0), 0);
        *previous = id;
    }
    return id;
}

/* A sprite with no bitmap at (10, 20), one pixel wide and high */
static nmo_object_id_t make_sprite(engine_fixture_t *f, uint32_t flags)
{
    nmo_object_id_t sprite = fixture_create(f, NMO_CID_SPRITE, "Sprite");
    nmo_sprite_state_t *state = (nmo_sprite_state_t *)fixture_state(f, sprite);
    if (state == NULL) {
        return 0;
    }
    state->entity.rect = (nmo_rect_t){10.0f, 20.0f, 11.0f, 21.0f};
    state->entity.flags = flags;
    return sprite;
}

/* The frame's quad of `object`; NULL for none */
static const nmo_engine_draw_t *quad_of(const nmo_engine_frame_t *frame, nmo_object_id_t object)
{
    for (int32_t i = 0; i < frame->draw_count; i++) {
        if (frame->draws[i].kind == NMO_ENGINE_DRAW_QUAD && frame->draws[i].object == object) {
            return &frame->draws[i];
        }
    }
    return NULL;
}

static nmo_object_id_t load_block(engine_fixture_t *f, nmo_object_id_t sprite, int32_t slot,
                                  nmo_object_id_t *previous)
{
    nmo_object_id_t load = chain(f, GUID_SPRITE_LOAD, "sprite Load", previous);
    if (load != 0) {
        fixture_input_int(f, fixture_target(f, load), (int32_t)sprite);
        fixture_input_value(f, BLOCK_PIN(f, load, 0), "sprite.bmp", sizeof("sprite.bmp"));
        fixture_input_int(f, BLOCK_PIN(f, load, 1), slot);
    }
    return load;
}

TEST(engine_sprites, sprite_load_sizes_the_sprite_which_draws_its_keyed_bitmap)
{
    write_sprite_bmp();
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t sprite = make_sprite(&f, 0);
    ASSERT_TRUE(sprite != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t previous = 0;
    nmo_object_id_t show = chain(&f, GUID_SHOW_2D, "Show 2D Entity", &previous);
    nmo_object_id_t load = load_block(&f, sprite, 0, &previous);
    nmo_object_id_t key = chain(&f, GUID_SET_COLOR_KEY_2D, "Set Color Key 2D", &previous);
    ASSERT_TRUE(show != 0 && load != 0 && key != 0);
    fixture_input_int(&f, fixture_target(&f, show), (int32_t)sprite);
    fixture_input_int(&f, fixture_target(&f, key), (int32_t)sprite);
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(1u, runs(&f, key));

    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    const nmo_engine_draw_t *draw = quad_of(&frame, sprite);
    ASSERT_NOT_NULL(draw);
    /* the bitmap's size from where it was, all of it */
    ASSERT_FLOAT_EQ(10.0f, draw->rect[0], 0.0f);
    ASSERT_FLOAT_EQ(20.0f, draw->rect[1], 0.0f);
    ASSERT_FLOAT_EQ(14.0f, draw->rect[2], 0.0f);
    ASSERT_FLOAT_EQ(22.0f, draw->rect[3], 0.0f);
    ASSERT_FLOAT_EQ(0.0f, draw->uv[0], 0.0f);
    ASSERT_FLOAT_EQ(1.0f, draw->uv[2], 0.0f);
    ASSERT_FLOAT_EQ(1.0f, draw->uv[3], 0.0f);
    ASSERT_EQ(sprite, draw->texture);
    ASSERT_EQ(0u, draw->material);
    ASSERT_TRUE(draw->nearest && draw->blend && draw->alpha_test);
    ASSERT_EQ(VXBLEND_SRCALPHA, draw->source_blend);
    ASSERT_EQ(VXBLEND_INVSRCALPHA, draw->dest_blend);

    nmo_engine_image_t image;
    ASSERT_TRUE(nmo_engine_texture_image(f.engine, sprite, &image));
    ASSERT_EQ(4, image.width);
    ASSERT_EQ(2, image.height);
    ASSERT_EQ(0, image.rgba[3]);     /* black keyed out */
    ASSERT_EQ(255, image.rgba[4 + 3]); /* white kept */
    fixture_end(&f);
}

TEST(engine_sprites, set_rectangles_and_change_sprite_slot_pick_what_the_sprite_shows)
{
    write_sprite_bmp();
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t sprite = make_sprite(&f, CK_2DENTITY_USESRCRECT);
    ASSERT_TRUE(sprite != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t previous = 0;
    nmo_object_id_t show = chain(&f, GUID_SHOW_2D, "Show 2D Entity", &previous);
    nmo_object_id_t first = load_block(&f, sprite, 0, &previous);
    nmo_object_id_t second = load_block(&f, sprite, 1, &previous);
    nmo_object_id_t slot = chain(&f, GUID_CHANGE_SPRITE_SLOT, "Change Sprite Slot", &previous);
    nmo_object_id_t bad_slot = chain(&f, GUID_CHANGE_SPRITE_SLOT, "Change Sprite Slot", &previous);
    nmo_object_id_t rects = chain(&f, GUID_SET_RECTANGLES, "Set Rectangles", &previous);
    ASSERT_TRUE(show != 0 && first != 0 && second != 0 && slot != 0 && bad_slot != 0 && rects != 0);
    fixture_input_int(&f, fixture_target(&f, show), (int32_t)sprite);
    fixture_input_int(&f, fixture_target(&f, slot), (int32_t)sprite);
    fixture_input_int(&f, BLOCK_PIN(&f, slot, 0), 1);
    fixture_input_int(&f, fixture_target(&f, bad_slot), (int32_t)sprite);
    fixture_input_int(&f, BLOCK_PIN(&f, bad_slot, 0), 2); /* no such slot: ignored */
    fixture_input_int(&f, fixture_target(&f, rects), (int32_t)sprite);
    const nmo_rect_t destination = {0.0f, 0.0f, 8.0f, 4.0f}, source = {2.0f, 0.0f, 4.0f, 2.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, rects, 0), &destination, sizeof(destination));
    fixture_input_value(&f, BLOCK_PIN(&f, rects, 1), &source, sizeof(source));
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(1u, runs(&f, rects));

    eng_texture_t *bitmap = eng_texture(f.engine, sprite);
    ASSERT_NOT_NULL(bitmap);
    ASSERT_EQ(2, bitmap->slot_count);
    ASSERT_EQ(1, bitmap->current_slot);
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    const nmo_engine_draw_t *draw = quad_of(&frame, sprite);
    ASSERT_NOT_NULL(draw);
    ASSERT_FLOAT_EQ(8.0f, draw->rect[2], 0.0f);
    ASSERT_FLOAT_EQ(4.0f, draw->rect[3], 0.0f);
    ASSERT_FLOAT_EQ(0.5f, draw->uv[0], 0.0f); /* source pixels over the bitmap's size */
    ASSERT_FLOAT_EQ(1.0f, draw->uv[2], 0.0f);
    ASSERT_FLOAT_EQ(1.0f, draw->uv[3], 0.0f);
    /* opaque: no blending, no alpha test */
    ASSERT_TRUE(draw->blend && !draw->alpha_test);
    ASSERT_EQ(VXBLEND_ONE, draw->source_blend);
    ASSERT_EQ(VXBLEND_ZERO, draw->dest_blend);
    fixture_end(&f);
}

/* A 2D entity at `rect` */
static nmo_object_id_t make_frame(engine_fixture_t *f, const char *name, nmo_rect_t rect)
{
    nmo_object_id_t id = fixture_create(f, NMO_CID_2DENTITY, name);
    nmo_2dentity_state_t *state = (nmo_2dentity_state_t *)fixture_state(f, id);
    if (state == NULL) {
        return 0;
    }
    state->rect = rect;
    return id;
}

TEST(engine_sprites, set_2d_parent_and_position_move_children_and_edit_2d_entity_sets_fields)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t parent = make_frame(&f, "Parent", (nmo_rect_t){0.0f, 0.0f, 100.0f, 100.0f});
    nmo_object_id_t child = make_frame(&f, "Child", (nmo_rect_t){10.0f, 10.0f, 20.0f, 20.0f});
    ASSERT_TRUE(parent != 0 && child != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t previous = 0;
    nmo_object_id_t adopt = chain(&f, GUID_SET_2D_PARENT, "Set 2D Parent", &previous);
    nmo_object_id_t move = chain(&f, GUID_SET_2D_POSITION, "Set 2D Position", &previous);
    nmo_object_id_t edit = chain(&f, GUID_EDIT_2D_ENTITY, "Edit 2D Entity", &previous);
    ASSERT_TRUE(adopt != 0 && move != 0 && edit != 0);
    fixture_input_int(&f, fixture_target(&f, adopt), (int32_t)child);
    fixture_input_int(&f, BLOCK_PIN(&f, adopt, 0), (int32_t)parent);
    fixture_input_int(&f, fixture_target(&f, move), (int32_t)parent);
    const float position[2] = {50.0f, 50.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, move, 0), position, sizeof(position));
    fixture_input_int(&f, fixture_target(&f, edit), (int32_t)child);
    const float x = 1.0f, y = 2.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, edit, 0), &x, sizeof(x));
    fixture_input_value(&f, BLOCK_PIN(&f, edit, 1), &y, sizeof(y));
    fixture_input_int(&f, BLOCK_PIN(&f, edit, 2), 3); /* Z Order */
    fixture_input_int(&f, BLOCK_PIN(&f, edit, 3), 1); /* Background: a child keeps its parent's */
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(1u, runs(&f, move));

    const eng_2d_t *moved = eng_2d_entity(f.engine, parent);
    ASSERT_FLOAT_EQ(50.0f, moved->rect.left, 0.0f);
    ASSERT_FLOAT_EQ(150.0f, moved->rect.right, 0.0f);
    /* the child kept its centre's place in the parent (15%), then Edit 2D Entity moved it */
    const eng_2d_t *edited = eng_2d_entity(f.engine, child);
    ASSERT_EQ(parent, edited->parent);
    ASSERT_FLOAT_EQ(1.0f, edited->rect.left, 0.0f);
    ASSERT_FLOAT_EQ(2.0f, edited->rect.top, 0.0f);
    ASSERT_FLOAT_EQ(11.0f, edited->rect.right, 0.0f);
    ASSERT_EQ(3, edited->z_order);
    ASSERT_EQ(0u, edited->flags & CK_2DENTITY_BACKGROUND);
    fixture_end(&f);
}

TEST(engine_sprites, set_2d_position_keeps_a_stuck_child_at_its_offset)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t parent = make_frame(&f, "Parent", (nmo_rect_t){0.0f, 0.0f, 100.0f, 100.0f});
    nmo_object_id_t child = make_frame(&f, "Child", (nmo_rect_t){10.0f, 10.0f, 20.0f, 20.0f});
    ASSERT_TRUE(parent != 0 && child != 0);
    nmo_2dentity_state_t *state = (nmo_2dentity_state_t *)fixture_state(&f, child);
    state->flags = CK_2DENTITY_STICKLEFT | CK_2DENTITY_STICKTOP;
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t previous = 0;
    nmo_object_id_t adopt = chain(&f, GUID_SET_2D_PARENT, "Set 2D Parent", &previous);
    nmo_object_id_t move = chain(&f, GUID_SET_2D_POSITION, "Set 2D Position", &previous);
    nmo_object_id_t hide = chain(&f, GUID_HIDE_2D, "Hide 2D Entity", &previous);
    ASSERT_TRUE(adopt != 0 && move != 0 && hide != 0);
    fixture_input_int(&f, fixture_target(&f, adopt), (int32_t)child);
    fixture_input_int(&f, BLOCK_PIN(&f, adopt, 0), (int32_t)parent);
    fixture_input_int(&f, fixture_target(&f, move), (int32_t)parent);
    const float position[2] = {50.0f, 40.0f};
    fixture_input_value(&f, BLOCK_PIN(&f, move, 0), position, sizeof(position));
    fixture_input_int(&f, fixture_target(&f, hide), (int32_t)child);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(1u, runs(&f, hide));

    const eng_2d_t *stuck = eng_2d_entity(f.engine, child);
    ASSERT_FLOAT_EQ(60.0f, stuck->rect.left, 0.0f);
    ASSERT_FLOAT_EQ(50.0f, stuck->rect.top, 0.0f);
    ASSERT_FLOAT_EQ(70.0f, stuck->rect.right, 0.0f);
    ASSERT_EQ(0, nmo_engine_object_is_visible(f.engine, child) ? 1 : 0);
    fixture_end(&f);
}

/* The active camera at (0, 0, -10) */
static nmo_object_id_t camera_behind(engine_fixture_t *f)
{
    nmo_object_id_t camera = fixture_create(f, NMO_CID_CAMERA, "Camera");
    nmo_camera_state_t *state = (nmo_camera_state_t *)fixture_state(f, camera);
    if (state == NULL) {
        return 0;
    }
    const float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -10, 1};
    memcpy(state->entity.world_matrix, world, sizeof(world));
    state->entity.has_entityndata_chunk = state->has_cameraonly_chunk = 1;
    state->projection_type = CK_PERSPECTIVEPROJECTION;
    state->fov = 1.0f;
    state->aspect_width = state->aspect_height = 1;
    state->near_plane = 1;
    state->far_plane = 100;
    return camera;
}

TEST(engine_sprites, sprite_multi_angle_stands_a_keyed_3d_sprite_in_for_its_owner)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t camera = camera_behind(&f);
    nmo_object_id_t front = fixture_create(&f, NMO_CID_TEXTURE, "Front");
    ASSERT_TRUE(camera != 0 && front != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t previous = 0;
    nmo_object_id_t multi = chain(&f, GUID_SPRITE_MULTI_ANGLE, "Sprite Multi Angle", &previous);
    ASSERT_TRUE(multi != 0);
    const float scale_x = 2.0f;
    fixture_input_value(&f, BLOCK_PIN(&f, multi, 0), &scale_x, sizeof(scale_x));
    fixture_input_int(&f, BLOCK_PIN(&f, multi, 5), (int32_t)front);
    fixture_commit(&f, NULL);
    nmo_object_id_t owner = f.owner_id;
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    f.engine->render.camera = camera;
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(1u, runs(&f, multi));

    ASSERT_FALSE(nmo_engine_object_is_visible(f.engine, owner)); /* the sprite stands in for it */
    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    const nmo_engine_draw_t *draw = NULL;
    for (int32_t i = 0; i < frame.draw_count; i++) {
        if (frame.draws[i].kind == NMO_ENGINE_DRAW_SPRITE3D) {
            draw = &frame.draws[i];
        }
    }
    ASSERT_NOT_NULL(draw);
    nmo_engine_material_t material;
    ASSERT_TRUE(nmo_engine_material(f.engine, draw->material, &material));
    ASSERT_EQ(front, material.texture);
    ASSERT_FLOAT_EQ(1.0f, material.diffuse[0], 0.0f);
    const eng_sprite3d_t *sprite = eng_sprite3d(f.engine, draw->object);
    ASSERT_NOT_NULL(sprite);
    ASSERT_EQ((uint32_t)VXSPRITE3D_YROTATE, sprite->mode);
    ASSERT_FLOAT_EQ(2.0f, sprite->max.x - sprite->min.x, 1e-6f);
    ASSERT_TRUE(eng_texture(f.engine, front)->transparent); /* Transparent TRUE, black key */
    fixture_end(&f);
}

static nmo_object_id_t block_local(engine_fixture_t *f, nmo_object_id_t block, size_t pos)
{
    const nmo_behavior_state_t *state = (const nmo_behavior_state_t *)fixture_state(f, block);
    return state != NULL && pos < state->local_parameters.count
               ? nmo_behavior_ref_array_get_id(&state->local_parameters, pos)
               : 0;
}

TEST(engine_sprites, display_progression_bar_draws_the_full_bar_cut_over_the_empty_one)
{
    write_sprite_bmp();
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t empty = make_sprite(&f, 0);
    nmo_object_id_t full = make_sprite(&f, 0);
    ASSERT_TRUE(empty != 0 && full != 0);
    f.script_id = fixture_add_script(&f, "Script", &f.script_in);
    fixture_begin_edit(&f);
    nmo_object_id_t previous = 0;
    nmo_object_id_t load_empty = load_block(&f, empty, 0, &previous);
    nmo_object_id_t load_full = load_block(&f, full, 0, &previous);
    nmo_object_id_t bar = chain(&f, GUID_DISPLAY_PROGRESSION_BAR, "Display Progression Bar", &previous);
    ASSERT_TRUE(load_empty != 0 && load_full != 0 && bar != 0);
    fixture_input_int(&f, BLOCK_PIN(&f, bar, 0), 5);
    fixture_input_int(&f, BLOCK_PIN(&f, bar, 1), 6);
    const float half = 0.5f;
    fixture_input_value(&f, BLOCK_PIN(&f, bar, 2), &half, sizeof(half));
    nmo_object_id_t locals[3] = {block_local(&f, bar, 0), block_local(&f, bar, 1), block_local(&f, bar, 2)};
    ASSERT_TRUE(locals[0] != 0 && locals[1] != 0 && locals[2] != 0);
    fixture_commit(&f, NULL);
    nmo_engine_set_file_resolver(f.engine, resolve_scratch, NULL);
    const int32_t values[3] = {(int32_t)empty, (int32_t)full, 1}; /* the settings, set (Active) */
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(NMO_OK, nmo_engine_set_param_value(f.engine, locals[i], &values[i], sizeof(values[i])));
    }
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    ASSERT_EQ(NMO_OK, nmo_engine_step(f.engine, 10.0f));
    ASSERT_EQ(1u, runs(&f, bar));

    nmo_engine_frame_t frame;
    ASSERT_EQ(NMO_OK, nmo_engine_render_frame(f.engine, &frame));
    int at_empty = -1, at_full = -1;
    for (int32_t i = 0; i < frame.draw_count; i++) {
        if (frame.draws[i].kind == NMO_ENGINE_DRAW_QUAD && frame.draws[i].object == empty) {
            at_empty = i;
        } else if (frame.draws[i].kind == NMO_ENGINE_DRAW_QUAD && frame.draws[i].object == full) {
            at_full = i;
        }
    }
    ASSERT_TRUE(at_empty >= 0 && at_full > at_empty);
    const nmo_engine_draw_t *e = &frame.draws[at_empty], *u = &frame.draws[at_full];
    ASSERT_FLOAT_EQ(5.0f, e->rect[0], 0.0f);
    ASSERT_FLOAT_EQ(9.0f, e->rect[2], 0.0f);
    ASSERT_FLOAT_EQ(8.0f, e->rect[3], 0.0f);
    ASSERT_FLOAT_EQ(7.0f, u->rect[2], 0.0f); /* half of 4 pixels */
    ASSERT_FLOAT_EQ(0.5f, u->uv[2], 0.0f);
    ASSERT_FALSE(nmo_engine_object_is_visible(f.engine, full)); /* hidden again */
    fixture_end(&f);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(engine_sprites, sprite_load_sizes_the_sprite_which_draws_its_keyed_bitmap);
    REGISTER_TEST(engine_sprites, set_rectangles_and_change_sprite_slot_pick_what_the_sprite_shows);
    REGISTER_TEST(engine_sprites, set_2d_parent_and_position_move_children_and_edit_2d_entity_sets_fields);
    REGISTER_TEST(engine_sprites, set_2d_position_keeps_a_stuck_child_at_its_offset);
    REGISTER_TEST(engine_sprites, sprite_multi_angle_stands_a_keyed_3d_sprite_in_for_its_owner);
    REGISTER_TEST(engine_sprites, display_progression_bar_draws_the_full_bar_cut_over_the_empty_one);
TEST_MAIN_END()
