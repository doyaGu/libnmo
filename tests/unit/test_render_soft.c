/**
 * @file test_render_soft.c
 * @brief A renderer backend through render_backend.h: coverage, the
 *        fragment tests, blending, textures, render targets and read-back.
 *
 * Built twice: test_render_soft drives the software backend, and
 * test_render_gpu (RENDER_TEST_GPU) the SDL_GPU one, skipped where there is
 * no GPU device. Pixels are read back through the backend, within a level
 * of what is expected (a GPU rounds to bytes as it likes).
 */

#include "test_framework.h"
#include "../../src/render/render_backend.h"
#ifdef RENDER_TEST_GPU
#include "../../src/render/gpu/render_gpu.h"
#define RENDER_TEST_SUITE render_gpu
#else
#include "../../src/render/soft/render_soft.h"
#define RENDER_TEST_SUITE render_soft
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The suite's name expanded before the framework's macros stringize it */
#define RENDER_TEST_EXPAND(suite, name) TEST(suite, name)
#define RENDER_TEST(name) RENDER_TEST_EXPAND(RENDER_TEST_SUITE, name)
#define REGISTER_RENDER_TEST_EXPAND(suite, name) REGISTER_TEST(suite, name)
#define REGISTER_RENDER_TEST(name) REGISTER_RENDER_TEST_EXPAND(RENDER_TEST_SUITE, name)

#define SIZE 16

typedef struct soft_fixture {
    render_backend_t *backend;
    render_state_t state;
    uint8_t pixels[SIZE * SIZE * 4];
} soft_fixture_t;

static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

/* A SIZE x SIZE screen, cleared black, drawn in its own pixels with nothing tested */
static render_backend_t *backend_create(void)
{
#ifdef RENDER_TEST_GPU
    return render_gpu_backend_create_offscreen();
#else
    return render_soft_backend_create();
#endif
}

static void fixture_open(soft_fixture_t *f)
{
    f->backend = backend_create();
    ASSERT_NOT_NULL(f->backend);
    render_state_t *s = &f->state;
    memset(s, 0, sizeof(*s));
    s->viewport[2] = s->viewport[3] = SIZE;
    s->projection_kind = RENDER_PROJECTION_2D;
    s->projection_rect[0] = s->projection_rect[1] = (float)SIZE;
    memcpy(s->projection, identity, sizeof(identity));
    memcpy(s->model_view, identity, sizeof(identity));
    memcpy(s->texture_matrix, identity, sizeof(identity));
    s->sampler.address = 3;
    s->color[0] = s->color[1] = s->color[2] = s->color[3] = 1.0f;
    s->depth_func = 4;
    s->depth_write = true;
    s->color_write = true;
    s->stencil_func = 8;
    s->stencil_mask = s->stencil_write_mask = 0xFF;
    s->stencil_fail = s->stencil_zfail = s->stencil_pass = 1;
    s->fog_mode = 3;
    f->backend->ops->begin_screen(f->backend, SIZE, SIZE, false);
    const float black[4] = {0, 0, 0, 1};
    f->backend->ops->clear(f->backend, s, true, true, true, black);
}

static void fixture_close(soft_fixture_t *f)
{
    f->backend->ops->destroy(f->backend);
}

/* What the screen shows now: the frame ended and read back, then the screen begun again, kept */
static void fixture_read(soft_fixture_t *f)
{
    const int32_t whole[4] = {0, 0, SIZE, SIZE};
    f->backend->ops->end_screen(f->backend);
    ASSERT_EQ(f->backend->ops->read_pixels(f->backend, whole, f->pixels), NMO_OK);
    ASSERT_TRUE(f->backend->ops->begin_screen(f->backend, SIZE, SIZE, true));
}

#define ASSERT_PIXEL(f, x, y, r, g, b)                                                             \
    do {                                                                                           \
        fixture_read(f);                                                                           \
        const uint8_t *p_ = &(f)->pixels[((size_t)(y) * SIZE + (size_t)(x)) * 4u];                 \
        ASSERT_TRUE(abs((int)p_[0] - (r)) <= 1);                                                   \
        ASSERT_TRUE(abs((int)p_[1] - (g)) <= 1);                                                   \
        ASSERT_TRUE(abs((int)p_[2] - (b)) <= 1);                                                   \
    } while (0)

/* A quad from x0, y0 to x1, y1 at depth z, clockwise on screen, its texture across it */
static void draw_quad(soft_fixture_t *f, float x0, float y0, float x1, float y1, float z)
{
    const float positions[12] = {x0, y0, z, x1, y0, z, x1, y1, z, x0, y1, z};
    const float uvs[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    render_geometry_t g;
    memset(&g, 0, sizeof(g));
    g.primitive = RENDER_PRIMITIVE_TRIANGLE_FAN;
    g.vertex_count = 4;
    g.positions = positions;
    g.position_stride = 3 * sizeof(float);
    g.uvs = uvs;
    g.uv_stride = 2 * sizeof(float);
    f->backend->ops->draw(f->backend, &f->state, &g);
}

static void set_color(soft_fixture_t *f, float r, float g, float b, float a)
{
    f->state.color[0] = r;
    f->state.color[1] = g;
    f->state.color[2] = b;
    f->state.color[3] = a;
}

/* A texture of `w` x `h` texels, one level */
static uintptr_t make_texture(soft_fixture_t *f, int32_t w, int32_t h, const uint8_t *rgba)
{
    uintptr_t texture = f->backend->ops->texture_create(f->backend);
    f->backend->ops->texture_image(f->backend, texture, RENDER_TEXTURE_2D, 0, 0, w, h, rgba);
    f->backend->ops->texture_levels(f->backend, texture, RENDER_TEXTURE_2D, 0);
    return texture;
}

static void use_texture(soft_fixture_t *f, uintptr_t texture)
{
    f->state.texture = texture;
    f->state.texture_kind = RENDER_TEXTURE_2D;
    f->state.texenv = RENDER_TEXENV_MODULATE;
}

RENDER_TEST(a_clear_writes_only_inside_the_scissor)
{
    soft_fixture_t f;
    fixture_open(&f);
    f.state.scissor = true;
    const int32_t rect[4] = {4, 4, 8, 8};
    memcpy(f.state.scissor_rect, rect, sizeof(rect));
    const float red[4] = {1, 0, 0, 1};
    f.backend->ops->clear(f.backend, &f.state, true, false, false, red);
    ASSERT_PIXEL(&f, 5, 5, 255, 0, 0);
    ASSERT_PIXEL(&f, 11, 11, 255, 0, 0);
    ASSERT_PIXEL(&f, 3, 5, 0, 0, 0);
    ASSERT_PIXEL(&f, 12, 12, 0, 0, 0);
    fixture_close(&f);
}

RENDER_TEST(a_quad_covers_each_pixel_once)
{
    soft_fixture_t f;
    fixture_open(&f);
    f.state.blend = true;
    f.state.source_blend = f.state.dest_blend = 2; /* ONE, ONE */
    set_color(&f, 0.25f, 0.25f, 0.25f, 1.0f);
    draw_quad(&f, 0, 0, SIZE, SIZE, 0);
    fixture_read(&f);
    for (size_t i = 0; i < (size_t)SIZE * SIZE; i++) {
        ASSERT_TRUE(abs((int)f.pixels[i * 4] - 64) <= 1);
    }
    fixture_close(&f);
}

RENDER_TEST(an_edge_through_pixel_centers_draws_them_once)
{
    soft_fixture_t f;
    fixture_open(&f);
    f.state.blend = true;
    f.state.source_blend = f.state.dest_blend = 2;
    set_color(&f, 0.25f, 0.25f, 0.25f, 1.0f);
    draw_quad(&f, 0, 0, 8.5f, SIZE, 0);
    draw_quad(&f, 8.5f, 0, SIZE, SIZE, 0);
    ASSERT_PIXEL(&f, 7, 3, 64, 64, 64);
    ASSERT_PIXEL(&f, 8, 3, 64, 64, 64);
    ASSERT_PIXEL(&f, 9, 3, 64, 64, 64);
    fixture_close(&f);
}

RENDER_TEST(the_depth_test_keeps_the_nearer_fragment)
{
    soft_fixture_t f;
    fixture_open(&f);
    f.state.projection_kind = RENDER_PROJECTION_SCREEN;
    f.state.projection_rect[0] = f.state.projection_rect[1] = 0.0f;
    f.state.projection_rect[2] = f.state.projection_rect[3] = (float)SIZE;
    f.state.depth_test = true;
    set_color(&f, 1, 0, 0, 1);
    draw_quad(&f, 0, 0, SIZE, SIZE, 0.3f);
    set_color(&f, 0, 1, 0, 1);
    draw_quad(&f, 0, 0, SIZE, SIZE, 0.6f);
    ASSERT_PIXEL(&f, 8, 8, 255, 0, 0);
    set_color(&f, 0, 0, 1, 1);
    draw_quad(&f, 0, 0, 8, 8, 0.1f);
    ASSERT_PIXEL(&f, 4, 4, 0, 0, 255);
    ASSERT_PIXEL(&f, 12, 12, 255, 0, 0);
    /* without depth writes the next fragment tests against what was there */
    f.state.depth_write = false;
    set_color(&f, 1, 1, 1, 1);
    draw_quad(&f, 0, 0, SIZE, SIZE, 0.2f);
    f.state.depth_test = false;
    set_color(&f, 0, 1, 1, 1);
    draw_quad(&f, 0, 0, 2, 2, 0.9f);
    ASSERT_PIXEL(&f, 4, 4, 0, 0, 255);
    ASSERT_PIXEL(&f, 12, 12, 255, 255, 255);
    ASSERT_PIXEL(&f, 1, 1, 0, 255, 255);
    fixture_close(&f);
}

RENDER_TEST(blending_and_the_alpha_test_follow_the_state)
{
    soft_fixture_t f;
    fixture_open(&f);
    set_color(&f, 1, 0, 0, 1);
    draw_quad(&f, 0, 0, SIZE, SIZE, 0);
    f.state.blend = true;
    f.state.source_blend = 5; /* SRCALPHA */
    f.state.dest_blend = 6;   /* INVSRCALPHA */
    set_color(&f, 0, 1, 0, 0.5f);
    draw_quad(&f, 0, 0, 8, SIZE, 0);
    ASSERT_PIXEL(&f, 4, 4, 128, 128, 0);
    f.state.blend = false;
    f.state.alpha_test = true;
    f.state.alpha_func = 5; /* GREATER */
    f.state.alpha_ref = 0.5f;
    set_color(&f, 0, 0, 1, 0.25f);
    draw_quad(&f, 0, 0, SIZE, SIZE, 0);
    ASSERT_PIXEL(&f, 12, 4, 255, 0, 0);
    set_color(&f, 0, 0, 1, 0.75f);
    draw_quad(&f, 0, 0, SIZE, SIZE, 0);
    ASSERT_PIXEL(&f, 12, 4, 0, 0, 255);
    fixture_close(&f);
}

RENDER_TEST(a_texture_modulates_the_color_nearest_by_default)
{
    soft_fixture_t f;
    fixture_open(&f);
    const uint8_t texels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    uintptr_t texture = make_texture(&f, 2, 2, texels);
    use_texture(&f, texture);
    draw_quad(&f, 0, 0, SIZE, SIZE, 0);
    ASSERT_PIXEL(&f, 3, 3, 255, 0, 0);
    ASSERT_PIXEL(&f, 12, 3, 0, 255, 0);
    ASSERT_PIXEL(&f, 3, 12, 0, 0, 255);
    ASSERT_PIXEL(&f, 12, 12, 255, 255, 255);
    set_color(&f, 0.5f, 0.5f, 0.5f, 1.0f);
    draw_quad(&f, 0, 0, SIZE, SIZE, 0);
    ASSERT_PIXEL(&f, 12, 12, 128, 128, 128);
    f.state.texenv = RENDER_TEXENV_REPLACE;
    draw_quad(&f, 0, 0, SIZE, SIZE, 0);
    ASSERT_PIXEL(&f, 12, 12, 255, 255, 255);
    f.backend->ops->texture_destroy(f.backend, texture);
    fixture_close(&f);
}

RENDER_TEST(a_minified_texture_samples_its_smaller_level)
{
    soft_fixture_t f;
    fixture_open(&f);
    uint8_t level0[32 * 32 * 4], level1[16 * 16 * 4];
    for (int i = 0; i < 32 * 32; i++) {
        level0[i * 4 + 0] = 255, level0[i * 4 + 1] = 0, level0[i * 4 + 2] = 0, level0[i * 4 + 3] = 255;
    }
    for (int i = 0; i < 16 * 16; i++) {
        level1[i * 4 + 0] = 0, level1[i * 4 + 1] = 255, level1[i * 4 + 2] = 0, level1[i * 4 + 3] = 255;
    }
    uintptr_t texture = f.backend->ops->texture_create(f.backend);
    f.backend->ops->texture_image(f.backend, texture, RENDER_TEXTURE_2D, 0, 0, 32, 32, level0);
    f.backend->ops->texture_image(f.backend, texture, RENDER_TEXTURE_2D, 0, 1, 16, 16, level1);
    f.backend->ops->texture_levels(f.backend, texture, RENDER_TEXTURE_2D, 1);
    use_texture(&f, texture);
    f.state.sampler.mip = RENDER_MIP_NEAREST;
    draw_quad(&f, 0, 0, SIZE, SIZE, 0); /* 32 texels over 16 pixels: level 1 */
    ASSERT_PIXEL(&f, 8, 8, 0, 255, 0);
    f.state.sampler.mip = RENDER_MIP_NONE;
    draw_quad(&f, 0, 0, SIZE, SIZE, 0);
    ASSERT_PIXEL(&f, 8, 8, 255, 0, 0);
    f.backend->ops->texture_destroy(f.backend, texture);
    fixture_close(&f);
}

RENDER_TEST(the_stencil_masks_what_comes_after)
{
    soft_fixture_t f;
    fixture_open(&f);
    f.state.stencil = true;
    f.state.stencil_func = 8; /* ALWAYS */
    f.state.stencil_ref = 1;
    f.state.stencil_pass = 3; /* REPLACE */
    f.state.color_write = false;
    draw_quad(&f, 0, 0, 8, SIZE, 0);
    ASSERT_PIXEL(&f, 4, 4, 0, 0, 0);
    f.state.color_write = true;
    f.state.stencil_func = 3; /* EQUAL */
    f.state.stencil_pass = 1; /* KEEP */
    set_color(&f, 0, 1, 0, 1);
    draw_quad(&f, 0, 0, SIZE, SIZE, 0);
    ASSERT_PIXEL(&f, 4, 4, 0, 255, 0);
    ASSERT_PIXEL(&f, 12, 4, 0, 0, 0);
    /* a stencil clear writes its value under the write mask */
    f.state.clear_stencil = 1;
    const float black[4] = {0, 0, 0, 1};
    f.backend->ops->clear(f.backend, &f.state, false, false, true, black);
    set_color(&f, 0, 0, 1, 1);
    draw_quad(&f, 0, 0, SIZE, SIZE, 0);
    ASSERT_PIXEL(&f, 12, 4, 0, 0, 255);
    fixture_close(&f);
}

RENDER_TEST(back_faces_are_culled_by_their_winding)
{
    soft_fixture_t f;
    fixture_open(&f);
    f.state.cull = true;
    set_color(&f, 1, 0, 0, 1);
    draw_quad(&f, 0, 0, 8, SIZE, 0); /* clockwise on screen: front */
    ASSERT_PIXEL(&f, 4, 8, 255, 0, 0);
    draw_quad(&f, SIZE, 0, 8, SIZE, 0); /* mirrored: counter-clockwise, culled */
    ASSERT_PIXEL(&f, 12, 8, 0, 0, 0);
    f.state.front_ccw = true;
    draw_quad(&f, SIZE, 0, 8, SIZE, 0);
    ASSERT_PIXEL(&f, 12, 8, 255, 0, 0);
    fixture_close(&f);
}

RENDER_TEST(a_texture_drawn_into_shows_what_was_drawn_upright)
{
    soft_fixture_t f;
    fixture_open(&f);
    uintptr_t target = f.backend->ops->texture_create(f.backend);
    f.backend->ops->texture_target(f.backend, target, RENDER_TEXTURE_2D, 4, 4, false);
    ASSERT_TRUE(f.backend->ops->begin_target(f.backend, target, RENDER_TEXTURE_2D, 0, 4, 4));
    render_state_t screen = f.state;
    f.state.viewport[2] = f.state.viewport[3] = 4;
    f.state.projection_rect[0] = f.state.projection_rect[1] = 4.0f;
    const float blue[4] = {0, 0, 1, 1};
    f.backend->ops->clear(f.backend, &f.state, true, true, false, blue);
    set_color(&f, 1, 0, 0, 1);
    draw_quad(&f, 0, 0, 4, 2, 0); /* its top half */
    f.backend->ops->end_target(f.backend);
    f.state = screen;
    use_texture(&f, target);
    draw_quad(&f, 0, 0, SIZE, SIZE, 0);
    ASSERT_PIXEL(&f, 8, 2, 255, 0, 0);
    ASSERT_PIXEL(&f, 8, 13, 0, 0, 255);
    f.backend->ops->texture_destroy(f.backend, target);
    fixture_close(&f);
}

RENDER_TEST(linear_fog_blends_toward_the_fog_color)
{
    soft_fixture_t f;
    fixture_open(&f);
    f.state.fog = true;
    f.state.fog_mode = 3;
    f.state.fog_color = 0xFF000000u;
    f.state.fog_start = 0.0f;
    f.state.fog_end = 1.0f;
    draw_quad(&f, 0, 0, SIZE, SIZE, -0.5f); /* half way into the fog */
    ASSERT_PIXEL(&f, 8, 8, 128, 128, 128);
    fixture_close(&f);
}

RENDER_TEST(lines_are_a_pixel_wide)
{
    soft_fixture_t f;
    fixture_open(&f);
    const float positions[6] = {2.0f, 4.5f, 0.0f, 10.0f, 4.5f, 0.0f};
    render_geometry_t g;
    memset(&g, 0, sizeof(g));
    g.primitive = RENDER_PRIMITIVE_LINES;
    g.vertex_count = 2;
    g.positions = positions;
    g.position_stride = 3 * sizeof(float);
    f.backend->ops->draw(f.backend, &f.state, &g);
    ASSERT_PIXEL(&f, 3, 4, 255, 255, 255);
    ASSERT_PIXEL(&f, 8, 4, 255, 255, 255);
    ASSERT_PIXEL(&f, 5, 3, 0, 0, 0);
    ASSERT_PIXEL(&f, 5, 5, 0, 0, 0);
    ASSERT_PIXEL(&f, 11, 4, 0, 0, 0);
    fixture_close(&f);
}

RENDER_TEST(the_screen_drawn_last_is_read_back_and_kept)
{
    soft_fixture_t f;
    fixture_open(&f);
    set_color(&f, 0, 1, 0, 1);
    draw_quad(&f, 0, 0, 8, 8, 0);
    f.backend->ops->end_screen(f.backend);
    ASSERT_TRUE(f.backend->ops->last_frame_ready(f.backend, SIZE, SIZE));
    ASSERT_FALSE(f.backend->ops->last_frame_ready(f.backend, SIZE * 2, SIZE));
    const int32_t rect[4] = {6, 6, 4, 4};
    uint8_t read[4 * 4 * 4];
    ASSERT_TRUE(f.backend->ops->read_last_frame(f.backend, rect, read));
    ASSERT_EQ((int)read[1], 255);                  /* 6, 6: drawn */
    ASSERT_EQ((int)read[(3 * 4 + 3) * 4 + 1], 0);  /* 9, 9: not */
    /* a preserved screen keeps the frame before under the next one */
    ASSERT_TRUE(f.backend->ops->begin_screen(f.backend, SIZE, SIZE, true));
    ASSERT_PIXEL(&f, 4, 4, 0, 255, 0);
    fixture_close(&f);
}

#ifdef RENDER_TEST_GPU
static int gpu_available(void)
{
    render_backend_t *backend = backend_create();
    if (backend == NULL) {
        return 0;
    }
    backend->ops->destroy(backend);
    return 1;
}
#endif

TEST_MAIN_BEGIN()
#ifdef RENDER_TEST_GPU
if (!gpu_available()) {
    printf("SKIP: no GPU device\n");
    return TEST_EXIT_CODE_SKIPPED;
}
#endif
REGISTER_RENDER_TEST(a_clear_writes_only_inside_the_scissor);
REGISTER_RENDER_TEST(a_quad_covers_each_pixel_once);
REGISTER_RENDER_TEST(an_edge_through_pixel_centers_draws_them_once);
REGISTER_RENDER_TEST(the_depth_test_keeps_the_nearer_fragment);
REGISTER_RENDER_TEST(blending_and_the_alpha_test_follow_the_state);
REGISTER_RENDER_TEST(a_texture_modulates_the_color_nearest_by_default);
REGISTER_RENDER_TEST(a_minified_texture_samples_its_smaller_level);
REGISTER_RENDER_TEST(the_stencil_masks_what_comes_after);
REGISTER_RENDER_TEST(back_faces_are_culled_by_their_winding);
REGISTER_RENDER_TEST(a_texture_drawn_into_shows_what_was_drawn_upright);
REGISTER_RENDER_TEST(linear_fog_blends_toward_the_fog_color);
REGISTER_RENDER_TEST(lines_are_a_pixel_wide);
REGISTER_RENDER_TEST(the_screen_drawn_last_is_read_back_and_kept);
TEST_MAIN_END()
