#include "engine_fixture.h"
#include "../../src/engine/engine_transform.h"
#include "engine/nmo_engine_render.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "../fixtures/engine_morph_samples.h"

#include <math.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static nmo_object_id_t morph_mesh(engine_fixture_t *f, uint32_t count)
{
    nmo_object_id_t mesh = fixture_create(f, NMO_CID_MESH, "Mesh");
    nmo_mesh_state_t *state = fixture_state(f, mesh);
    nmo_3dentity_state_t *entity = fixture_state(f, f->owner_id);
    if (state == NULL || entity == NULL) return 0;
    state->vertex_count = count;
    state->vertices = nmo_arena_alloc(nmo_session_get_arena(f->session),
                                      count * sizeof(*state->vertices), _Alignof(nmo_vertex_t));
    if (state->vertices == NULL) return 0;
    memset(state->vertices, 0, count * sizeof(*state->vertices));
    for (uint32_t v = 0; v < count; v++) {
        state->vertices[v].position = (nmo_vector_t){123, 234, 345};
        state->vertices[v].normal = (nmo_vector_t){0, 1, 0};
        state->vertices[v].uv = (nmo_vector2_t){0.125f * (float)v, 0.75f};
    }
    entity->current_mesh = nmo_ref_from_id(mesh);
    return mesh;
}

static nmo_vector_t expected_normal(uint32_t word)
{
    double pitch = (word & 16383) * (6.28318530717958647692 / 16384);
    double yaw = ((word >> 16) & 16383) * (6.28318530717958647692 / 16384);
    return (nmo_vector_t){(float)(sin(yaw) * cos(pitch)), (float)-sin(pitch), (float)(cos(yaw) * cos(pitch))};
}

/* Split the packed fixture into NEWDATA/LEGACY arrays. Mode 2 supplies both
 * normal sections, with conflicting COMP data to check conversion precedence. */
static bool separate_morph(engine_fixture_t *f, nmo_objectanimation_state_t *state, int mode)
{
    nmo_arena_t *arena = nmo_session_get_arena(f->session);
    nmo_objanim_controller_t controller = {NMO_OBJANIM_CONTROLLER_MORPH, 0,
                                           sizeof(PackedNormalsWords), (void *)PackedNormalsWords};
    nmo_objanim_morph_info_t info;
    if (!nmo_objanim_morph_controller_info(&controller, &info)) return false;
    state->has_morph_counts = 1;
    state->morph_key_count = (int32_t)info.key_count;
    state->morph_vertex_count = (int32_t)info.vertex_count;
    state->morph_key_parsed_count = info.key_count;
    state->morph_keys = nmo_arena_alloc(arena, info.key_count * sizeof(*state->morph_keys), _Alignof(nmo_objanim_morph_key_t));
    if (mode) {
        state->morph_comp_count = info.key_count;
        state->morph_comp_sizes = nmo_arena_alloc(arena, info.key_count * sizeof(uint32_t), _Alignof(uint32_t));
        state->morph_comp_data = nmo_arena_alloc(arena, info.key_count * sizeof(void *), _Alignof(void *));
    }
    if (mode >= 2) {
        state->morph_normals_count = info.key_count;
        state->morph_normals_sizes = nmo_arena_alloc(arena, info.key_count * sizeof(uint32_t), _Alignof(uint32_t));
        state->morph_normals_data = nmo_arena_alloc(arena, info.key_count * sizeof(void *), _Alignof(void *));
    }
    if (!state->morph_keys || (mode && (!state->morph_comp_sizes || !state->morph_comp_data)) ||
        (mode >= 2 && (!state->morph_normals_sizes || !state->morph_normals_data))) return false;
    for (uint32_t i = 0; i < info.key_count; i++) {
        const float *positions;
        const uint8_t *normals;
        nmo_objanim_morph_controller_key(&controller, &info, i, &state->morph_keys[i].time_step, &positions, &normals);
        state->morph_keys[i].data_size = info.vertex_count * 12;
        state->morph_keys[i].data = (void *)positions;
        if (mode) {
            state->morph_comp_sizes[i] = info.vertex_count * 4;
            state->morph_comp_data[i] = mode == 2
                ? nmo_arena_alloc(arena, info.vertex_count * 4, 4) : (void *)normals;
            if (state->morph_comp_data[i] == NULL) return false;
            if (mode == 2) memset(state->morph_comp_data[i], 0, info.vertex_count * 4);
        }
        if (mode >= 2) {
            state->morph_normals_sizes[i] = mode == 3 ? 0 : info.vertex_count * 8;
            uint32_t *old = nmo_arena_alloc(arena, info.vertex_count * 8, 4);
            if (old == NULL) return false;
            state->morph_normals_data[i] = mode == 3 ? NULL : old;
            for (uint32_t v = 0; v < info.vertex_count; v++) {
                uint32_t word;
                memcpy(&word, normals + v * 4, 4);
                old[v*2] = 0x12340000u | (word & 65535);
                old[v*2+1] = 0x56780000u | (word >> 16);
            }
        }
    }
    return true;
}

TEST(engine_morph, controller_positions_and_packed_normals_match_reference_samples)
{
    size_t checked = 0;
    for (size_t c = 0; c < COUNT(MorphReferences); c++) {
        const morph_reference_t *input = &MorphReferences[c];
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t mesh = morph_mesh(&f, input->vertices);
        nmo_object_id_t animation = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Morph");
        nmo_objectanimation_state_t *state = fixture_state(&f, animation);
        ASSERT_TRUE(mesh != 0 && state != NULL);
        /* Exercise unaligned wire bytes as well as strided destination vertices. */
        uint8_t *bytes = nmo_arena_alloc(nmo_session_get_arena(f.session), input->bytes + 1, 1);
        ASSERT_NOT_NULL(bytes);
        memcpy(bytes + 1, input->words, input->bytes);
        nmo_objanim_controller_t controller = {NMO_OBJANIM_CONTROLLER_MORPH, 0, input->bytes, bytes + 1};
        state->entity = nmo_ref_from_id(f.owner_id);
        state->has_length = 1;
        state->length = 1;
        state->controller_count = 1;
        state->controllers = &controller;
        const nmo_mesh_state_t *original = fixture_state(&f, mesh);
        nmo_vertex_t saved[4];
        memcpy(saved, original->vertices, input->vertices * sizeof(*saved));
        fixture_begin_edit(&f);
        fixture_commit(&f, NULL);
        ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
        for (uint32_t s = 0; s < input->sample_count; s++) {
            const morph_sample_t *sample = &input->samples[s];
            ASSERT_TRUE(eng_object_animation_set_step(f.engine, animation, sample->time));
            nmo_engine_mesh_t runtime;
            ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &runtime));
            bool empty = input->words[0] == 0;
            for (uint32_t v = 0; v < input->vertices; v++) {
                const nmo_vertex_t *vertex = &runtime.vertices[v];
                const float *actual = &vertex->position.x;
                const float *expected = empty ? &saved[v].position.x : sample->positions[v];
                for (int k = 0; k < 3; k++) {
                    if (fabsf(actual[k] - expected[k]) > 0.00001f) fprintf(stderr, "%s time %g vertex %u\n", input->name, sample->time, v);
                    ASSERT_FLOAT_EQ(expected[k], actual[k], 0.00001f);
                }
                if (!empty && input->words[2]) {
                    nmo_vector_t normal = expected_normal(sample->normals[v]);
                    ASSERT_FLOAT_EQ(normal.x, vertex->normal.x, 0.000001f);
                    ASSERT_FLOAT_EQ(normal.y, vertex->normal.y, 0.000001f);
                    ASSERT_FLOAT_EQ(normal.z, vertex->normal.z, 0.000001f);
                }
                ASSERT_EQ(0, memcmp(&saved[v].uv, &vertex->uv, sizeof(vertex->uv)));
            }
            checked++;
        }
        ASSERT_EQ(0, memcmp(saved, original->vertices, input->vertices * sizeof(*saved)));
        ASSERT_EQ(0, memcmp(bytes + 1, input->words, input->bytes));
        nmo_engine_t *other = NULL;
        ASSERT_EQ(NMO_OK, nmo_engine_create(f.workspace, NULL, &other));
        nmo_engine_mesh_t untouched;
        ASSERT_TRUE(nmo_engine_mesh(other, mesh, &untouched));
        ASSERT_FLOAT_EQ(123, untouched.vertices[0].position.x, 0);
        nmo_engine_destroy(other);
        fixture_end(&f);
    }
    ASSERT_EQ(75u, checked);
}

TEST(engine_morph, invalid_or_ignored_morphs_leave_the_mesh_unchanged)
{
    engine_fixture_t f;
    fixture_begin_document(&f);
    nmo_object_id_t mesh = morph_mesh(&f, 4);
    nmo_object_id_t animation = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Morph");
    nmo_objectanimation_state_t *state = fixture_state(&f, animation);
    uint32_t words[COUNT(PackedNormalsWords)];
    nmo_objanim_controller_t controller = {NMO_OBJANIM_CONTROLLER_MORPH, 0, sizeof(words), words};
    state->entity = nmo_ref_from_id(f.owner_id);
    state->has_length = 1;
    state->length = 1;
    state->controller_count = 1;
    state->controllers = &controller;
    fixture_begin_edit(&f);
    fixture_commit(&f, NULL);
    ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
    nmo_engine_mesh_t runtime;
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &runtime));
    uint32_t version = runtime.version;
    nmo_vertex_t saved[4];
    memcpy(saved, runtime.vertices, sizeof(saved));
    for (int invalid = 0; invalid < 7; invalid++) {
        memcpy(words, PackedNormalsWords, sizeof(words));
        controller.data_size = sizeof(words);
        state->flags = 0;
        float time = 0;
        if (invalid == 0) controller.data_size--;
        if (invalid == 1) words[1] = UINT32_MAX;
        if (invalid == 2) { float nan = NAN; memcpy(&words[37], &nan, 4); }
        if (invalid == 3) { float earlier = -1; memcpy(&words[37], &earlier, 4); }
        if (invalid == 4) state->flags = CK_OBJECTANIMATION_IGNOREMORPH;
        if (invalid == 5) time = NAN;
        if (invalid == 6) time = INFINITY;
        ASSERT_TRUE(eng_object_animation_set_step(f.engine, animation, time));
        ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &runtime));
        ASSERT_EQ(version, runtime.version);
        ASSERT_EQ(0, memcmp(saved, runtime.vertices, sizeof(saved)));
    }
    memcpy(words, PackedNormalsWords, sizeof(words));
    state->flags = 0;
    ASSERT_TRUE(eng_object_animation_set_step(f.engine, animation, 6));
    ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &runtime));
    ASSERT_TRUE(runtime.version > version);
    ASSERT_FLOAT_EQ(-2, runtime.vertices[0].position.x, 0.000001f);
    fixture_end(&f);
}

TEST(engine_morph, separate_keys_convert_legacy_normals_and_replace_optional_sections)
{
    for (int mode = 0; mode < 4; mode++) {
        engine_fixture_t f;
        fixture_begin_document(&f);
        nmo_object_id_t mesh = morph_mesh(&f, 4);
        nmo_object_id_t animation = fixture_create(&f, NMO_CID_OBJECTANIMATION, "Morph");
        nmo_objectanimation_state_t *state = fixture_state(&f, animation);
        state->entity = nmo_ref_from_id(f.owner_id);
        state->has_length = 1;
        state->length = 1;
        ASSERT_TRUE(separate_morph(&f, state, mode));
        fixture_begin_edit(&f);
        fixture_commit(&f, NULL);
        ASSERT_EQ(NMO_OK, nmo_engine_start(f.engine, NMO_ENGINE_START_ALL));
        ASSERT_TRUE(eng_object_animation_set_step(f.engine, animation, 6));
        nmo_engine_mesh_t runtime;
        ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &runtime));
        for (int v = 0; v < 4; v++) {
            ASSERT_FLOAT_EQ(PackedNormalsSamples[6].positions[v][0], runtime.vertices[v].position.x, 0.000001f);
            nmo_vector_t normal = mode == 1 || mode == 2 ? expected_normal(PackedNormalsSamples[6].normals[v]) : (nmo_vector_t){0};
            ASSERT_FLOAT_EQ(normal.x, runtime.vertices[v].normal.x, 0.000001f);
            ASSERT_FLOAT_EQ(normal.y, runtime.vertices[v].normal.y, 0.000001f);
            ASSERT_FLOAT_EQ(normal.z, runtime.vertices[v].normal.z, 0.000001f);
        }
        /* An absent position array must not be dereferenced or erase vertices. */
        state->morph_keys[0].data_size = 0;
        state->morph_keys[0].data = NULL;
        ASSERT_TRUE(eng_object_animation_set_step(f.engine, animation, 2));
        ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &runtime));
        ASSERT_FLOAT_EQ(-2, runtime.vertices[0].position.x, 0.000001f);
        /* A truncated later key rejects the whole source before any mutation. */
        uint32_t version = runtime.version;
        state->morph_keys[2].data_size = 4;
        ASSERT_TRUE(eng_object_animation_set_step(f.engine, animation, 0));
        ASSERT_TRUE(nmo_engine_mesh(f.engine, mesh, &runtime));
        ASSERT_EQ(version, runtime.version);
        fixture_end(&f);
    }
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_morph, controller_positions_and_packed_normals_match_reference_samples);
REGISTER_TEST(engine_morph, invalid_or_ignored_morphs_leave_the_mesh_unchanged);
REGISTER_TEST(engine_morph, separate_keys_convert_legacy_normals_and_replace_optional_sections);
TEST_MAIN_END()
