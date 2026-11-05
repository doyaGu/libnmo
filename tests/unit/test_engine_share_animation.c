#include "engine_fixture.h"
#include "../../src/engine/engine_internal.h"

#define GUID_NOP NMO_GUID(0x302561c4,0x0d282980)

typedef struct callback_observation { unsigned edits, loads, resets; int result; } callback_observation_t;

static int editor_callback(nmo_engine_block_t *block, uint32_t message)
{
    callback_observation_t *seen = nmo_engine_block_user_data(block);
    if (message == NMO_ENGINE_MSG_EDITED) { seen->edits++; return seen->result; }
    if (message == NMO_ENGINE_MSG_LOAD) seen->loads++;
    if (message == NMO_ENGINE_MSG_RESET) seen->resets++;
    return NMO_ENGINE_BR_OK;
}

TEST(engine_share_animation, callback_only_blocks_receive_explicit_edit_notifications_before_and_after_start)
{
    engine_fixture_t f; fixture_begin(&f); nmo_object_id_t block = fixture_block(&f,GUID_NOP,"Editor Only"); ASSERT_TRUE(block != 0); fixture_commit_bare(&f,NULL); callback_observation_t seen = {.result = NMO_ENGINE_BR_PARAMETERERROR};
    nmo_engine_block_desc_t desc = {.guid = GUID_NOP,.name = "Editor Only",.callback = editor_callback,.user_data = &seen}; ASSERT_EQ(NMO_OK,nmo_engine_register_block(f.engine,&desc)); ASSERT_TRUE(nmo_engine_has_block(f.engine,GUID_NOP)); int32_t result = -1;
    ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(f.engine,block,&result)); ASSERT_EQ(NMO_ENGINE_BR_PARAMETERERROR,result); ASSERT_EQ(1u,seen.edits); ASSERT_EQ(0u,seen.loads); ASSERT_EQ(0u,runs(&f,block));
    ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL)); ASSERT_EQ(1u,seen.loads); ASSERT_EQ(1u,seen.resets); ASSERT_EQ(NMO_OK,nmo_engine_step(f.engine,16)); ASSERT_EQ(1u,seen.edits); ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(f.engine,block,NULL)); ASSERT_EQ(2u,seen.edits); fixture_end(&f);
}

TEST(engine_share_animation, registered_blocks_without_functions_report_native_function_errors_instead_of_unknown_blocks)
{
    engine_fixture_t f; fixture_begin(&f); nmo_object_id_t block = fixture_block(&f,GUID_NOP,"Editor Only"); ASSERT_TRUE(block != 0); fixture_link(&f,f.script_in,BLOCK_IN(&f,block,0),0); nmo_engine_options_t options = nmo_engine_options_default(); options.fail_on = NMO_ENGINE_FAIL_ALL; fixture_commit_bare(&f,&options); callback_observation_t seen = {0};
    nmo_engine_block_desc_t desc = {.guid = GUID_NOP,.callback = editor_callback,.user_data = &seen}; ASSERT_EQ(NMO_OK,nmo_engine_register_block(f.engine,&desc)); ASSERT_EQ(NMO_OK,nmo_engine_start(f.engine,NMO_ENGINE_START_ALL)); ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,nmo_engine_step(f.engine,0)); nmo_engine_event_t failure; ASSERT_TRUE(nmo_engine_failure(f.engine,&failure)); ASSERT_EQ(NMO_ENGINE_EVENT_EXECUTE,failure.kind); ASSERT_EQ(NMO_ENGINE_BR_GENERICERROR,failure.result); ASSERT_EQ(0u,seen.edits); fixture_end(&f);
}

TEST(engine_share_animation, edit_notifications_validate_the_target_and_allow_registered_blocks_without_callbacks)
{
    engine_fixture_t f; fixture_begin(&f); nmo_object_id_t block = fixture_block(&f,GUID_NOP,"Plain Block"); ASSERT_TRUE(block != 0); fixture_commit(&f,NULL); int32_t result = -1;
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,nmo_engine_notify_behavior_edited(NULL,block,&result)); ASSERT_EQ(NMO_ERR_NOT_FOUND,nmo_engine_notify_behavior_edited(f.engine,f.script_id,&result)); ASSERT_EQ(NMO_ERR_NOT_FOUND,nmo_engine_notify_behavior_edited(f.engine,0,&result)); ASSERT_EQ(NMO_OK,nmo_engine_notify_behavior_edited(f.engine,block,&result)); ASSERT_EQ(NMO_ENGINE_BR_OK,result);
    nmo_engine_block_desc_t invalid = {.guid = GUID_NOP}; ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,nmo_engine_register_block(f.engine,&invalid)); nmo_engine_destroy_object(f.engine,block); ASSERT_EQ(NMO_ERR_NOT_FOUND,nmo_engine_notify_behavior_edited(f.engine,block,&result)); fixture_end(&f);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(engine_share_animation, callback_only_blocks_receive_explicit_edit_notifications_before_and_after_start);
REGISTER_TEST(engine_share_animation, registered_blocks_without_functions_report_native_function_errors_instead_of_unknown_blocks);
REGISTER_TEST(engine_share_animation, edit_notifications_validate_the_target_and_allow_registered_blocks_without_callbacks);
TEST_MAIN_END()
