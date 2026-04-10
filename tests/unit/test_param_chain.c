/**
 * @file test_param_chain.c
 * @brief Tests for typed parameter chain tracing
 */

#include "../test_framework.h"
#include "app/nmo_script_walker.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "app/nmo_session_util.h"
#include "core/nmo_array.h"
#include "format/nmo_object.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"

static bool open_test_file(const char *path,
                           nmo_context_t **out_ctx,
                           nmo_session_t **out_session)
{
    char errbuf[256] = {0};
    return nmo_session_open_file_with_context(path, out_ctx, out_session,
                                              errbuf, sizeof(errbuf));
}

/* Walk behaviors looking for a pIn with source, optionally shared */
typedef struct {
    nmo_session_t *session;
    nmo_object_id_t found_shared;
    nmo_object_id_t found_direct;
} find_pin_ctx_t;

static bool find_pin_visitor(nmo_object_id_t behavior_id,
                             const nmo_behavior_state_t *state,
                             uint32_t depth, bool is_bb, void *user_data)
{
    (void)behavior_id; (void)depth; (void)is_bb;
    find_pin_ctx_t *fctx = (find_pin_ctx_t *)user_data;
    if (!state) return true;

    nmo_object_repository_t *repo = nmo_session_get_repository(fctx->session);
    const nmo_object_id_t *pin_ids =
        (const nmo_object_id_t *)state->in_parameters.data;

    for (size_t i = 0; i < state->in_parameters.count; ++i) {
        if (pin_ids[i] == 0) continue;
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, pin_ids[i]);
        if (!obj) continue;

        const nmo_parameterin_state_t *pin =
            (const nmo_parameterin_state_t *)nmo_object_get_state(obj);
        if (!pin || pin->source_id == 0) continue;

        if (pin->is_shared && fctx->found_shared == 0)
            fctx->found_shared = pin_ids[i];
        if (!pin->is_shared && fctx->found_direct == 0)
            fctx->found_direct = pin_ids[i];

        if (fctx->found_shared != 0 && fctx->found_direct != 0)
            return false;
    }
    return true;
}

static void find_pins(nmo_context_t *ctx, nmo_session_t *session,
                      find_pin_ctx_t *fctx)
{
    fctx->session = session;
    fctx->found_shared = 0;
    fctx->found_direct = 0;

    nmo_array_t scripts;
    nmo_array_init(&scripts, sizeof(nmo_script_entry_t), 32, NULL);
    nmo_script_walker_find_scripts(ctx, session, &scripts);

    const nmo_script_entry_t *entries =
        (const nmo_script_entry_t *)scripts.data;
    for (size_t i = 0; i < scripts.count; ++i) {
        nmo_script_walker_walk(ctx, session, entries[i].script_id,
                               find_pin_visitor, fctx);
        if (fctx->found_shared != 0 && fctx->found_direct != 0) break;
    }
    nmo_array_dispose(&scripts);
}

/* ---- Tests ---- */

TEST(param_chain, basic_chain_has_steps)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                        &ctx, &session))
        return;

    find_pin_ctx_t fctx = {0};
    find_pins(ctx, session, &fctx);
    if (fctx.found_direct == 0) {
        nmo_session_close_with_context(ctx, session);
        return;
    }

    nmo_array_t chain;
    nmo_array_init(&chain, sizeof(nmo_param_chain_step_t), 16, NULL);
    nmo_status_t st = nmo_script_walker_trace_param_chain(
        ctx, session, fctx.found_direct, &chain, 32);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_TRUE(chain.count >= 2);

    const nmo_param_chain_step_t *steps =
        (const nmo_param_chain_step_t *)chain.data;
    ASSERT_EQ((int)NMO_CHAIN_STEP_START, (int)steps[0].type);
    ASSERT_EQ((long long)fctx.found_direct, (long long)steps[0].id);

    /* Last step should be a non-ParameterIn (DirectSource terminal) */
    ASSERT_TRUE(steps[chain.count - 1].class_id != NMO_CID_PARAMETERIN ||
                steps[chain.count - 1].type == NMO_CHAIN_STEP_START);

    nmo_array_dispose(&chain);
    nmo_session_close_with_context(ctx, session);
}

TEST(param_chain, shared_source_detected)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                        &ctx, &session))
        return;

    find_pin_ctx_t fctx = {0};
    find_pins(ctx, session, &fctx);
    if (fctx.found_shared == 0) {
        nmo_session_close_with_context(ctx, session);
        return;
    }

    nmo_array_t chain;
    nmo_array_init(&chain, sizeof(nmo_param_chain_step_t), 16, NULL);
    nmo_script_walker_trace_param_chain(ctx, session, fctx.found_shared,
                                        &chain, 32);
    ASSERT_TRUE(chain.count >= 2);

    /* The starting pIn has is_shared=true. When the chain continues through
     * another pIn that also has is_shared, that step is SHARED_SOURCE.
     * At minimum verify the chain contains a non-START step. */
    const nmo_param_chain_step_t *steps =
        (const nmo_param_chain_step_t *)chain.data;
    bool has_shared_or_direct = false;
    for (size_t i = 1; i < chain.count; ++i) {
        if (steps[i].type == NMO_CHAIN_STEP_SHARED_SOURCE ||
            steps[i].type == NMO_CHAIN_STEP_DIRECT_SOURCE) {
            has_shared_or_direct = true;
            break;
        }
    }
    ASSERT_TRUE(has_shared_or_direct);

    nmo_array_dispose(&chain);
    nmo_session_close_with_context(ctx, session);
}

TEST(param_chain, owner_id_populated)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                        &ctx, &session))
        return;

    find_pin_ctx_t fctx = {0};
    find_pins(ctx, session, &fctx);
    if (fctx.found_direct == 0) {
        nmo_session_close_with_context(ctx, session);
        return;
    }

    nmo_array_t chain;
    nmo_array_init(&chain, sizeof(nmo_param_chain_step_t), 16, NULL);
    nmo_script_walker_trace_param_chain(ctx, session, fctx.found_direct,
                                        &chain, 32);

    const nmo_param_chain_step_t *steps =
        (const nmo_param_chain_step_t *)chain.data;
    bool any_owner = false;
    for (size_t i = 0; i < chain.count; ++i) {
        if (steps[i].owner_id != 0) { any_owner = true; break; }
    }
    ASSERT_TRUE(any_owner);

    nmo_array_dispose(&chain);
    nmo_session_close_with_context(ctx, session);
}

TEST(param_chain, max_depth_respected)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    if (!open_test_file(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
                        &ctx, &session))
        return;

    find_pin_ctx_t fctx = {0};
    find_pins(ctx, session, &fctx);
    if (fctx.found_direct == 0) {
        nmo_session_close_with_context(ctx, session);
        return;
    }

    nmo_array_t chain;
    nmo_array_init(&chain, sizeof(nmo_param_chain_step_t), 16, NULL);
    nmo_script_walker_trace_param_chain(ctx, session, fctx.found_direct,
                                        &chain, 1);
    ASSERT_TRUE(chain.count <= 1);

    nmo_array_dispose(&chain);
    nmo_session_close_with_context(ctx, session);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(param_chain, basic_chain_has_steps);
    REGISTER_TEST(param_chain, shared_source_detected);
    REGISTER_TEST(param_chain, owner_id_populated);
    REGISTER_TEST(param_chain, max_depth_respected);
TEST_MAIN_END()
