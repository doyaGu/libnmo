#include "test_framework.h"
#include "project/nmo_project_plan.h"
#include "object/nmo_class_ids.h"

TEST(project_plan, creates_empty_project_plan)
{
    nmo_project_plan_t *plan = NULL;
    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_NOT_NULL(plan);
    ASSERT_EQ(0u, nmo_project_plan_scene_count(plan));
    ASSERT_NULL(nmo_project_plan_document_name(plan));
    nmo_project_plan_destroy(plan);
}

TEST(project_plan, clones_document_metadata)
{
    nmo_project_plan_t *plan = NULL;
    nmo_project_plan_t *clone = NULL;

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "GeneratedLevel"));
    ASSERT_EQ(NMO_OK, nmo_project_plan_clone(plan, &clone));
    ASSERT_NOT_NULL(clone);
    ASSERT_STR_EQ("GeneratedLevel", nmo_project_plan_document_name(clone));
    ASSERT_EQ(0u, nmo_project_plan_scene_count(clone));

    nmo_project_plan_destroy(clone);
    nmo_project_plan_destroy(plan);
}

TEST(project_plan, clones_animation_morph_keys)
{
    nmo_project_plan_t *plan = NULL;
    nmo_project_plan_t *clone = NULL;
    uint32_t scene = 1u;
    uint32_t target = 0u;
    uint32_t animation = 0u;
    float morph_data[] = {1.0f, 2.0f, 3.0f};
    nmo_objanim_morph_key_t morph_key = {
        .time_step = 0.5f,
        .data_size = sizeof(morph_data),
        .data = morph_data,
    };

    ASSERT_EQ(NMO_OK, nmo_project_plan_create(&plan));
    ASSERT_EQ(NMO_OK, nmo_project_plan_set_document_name(plan, "GeneratedLevel"));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_3DENTITY,
                      .name = "Target",
                  },
                  &target));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_add_object(
                  plan,
                  &(nmo_project_object_spec_t){
                      .scene_handle = scene,
                      .class_id = NMO_CID_OBJECTANIMATION,
                      .name = "Animation",
                  },
                  &animation));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_object_animation(
                  plan,
                  animation,
                  target,
                  CKOBJANIM_FORMAT_NEWDATA,
                  false,
                  0.0f,
                  0.0f,
                  0.0f,
                  false,
                  0u,
                  false,
                  0.0f));
    ASSERT_EQ(NMO_OK,
              nmo_project_plan_set_object_animation_morph_keys(
                  plan,
                  animation,
                  &morph_key,
                  1u));
    morph_data[1] = 99.0f;

    ASSERT_EQ(NMO_OK, nmo_project_plan_clone(plan, &clone));
    ASSERT_NOT_NULL(clone);

    nmo_project_object_desc_t object = {0};
    ASSERT_EQ(NMO_OK, nmo_project_plan_get_object(clone, 1u, &object));
    ASSERT_EQ(1u, object.animation_morph_key_count);
    ASSERT_NOT_NULL(object.animation_morph_keys);
    const float *stored = (const float *)object.animation_morph_keys[0].data;
    ASSERT_FLOAT_EQ(2.0f, stored[1], 0.0001f);

    nmo_project_plan_destroy(clone);
    nmo_project_plan_destroy(plan);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(project_plan, creates_empty_project_plan);
REGISTER_TEST(project_plan, clones_document_metadata);
REGISTER_TEST(project_plan, clones_animation_morph_keys);
TEST_MAIN_END()
