#include "test_framework.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"

#include <stdio.h>

TEST(bulk_destroy, take_and_destroy_all) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_load(ctx, "data/Ballance/P_Modul_01.nmo");
    if (!session) {
        /* Skip if test data not available */
        nmo_context_release(ctx);
        return;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    size_t total = 0;
    nmo_object_t **all = nmo_object_repository_get_all(repo, &total);
    ASSERT_TRUE(total > 0);

    /* Collect all IDs first (snapshot) */
    nmo_object_id_t *ids = (nmo_object_id_t *)malloc(total * sizeof(nmo_object_id_t));
    ASSERT_NOT_NULL(ids);
    for (size_t i = 0; i < total; i++) {
        ids[i] = nmo_object_get_id(all[i]);
    }

    /* Take + destroy each, print progress */
    size_t destroyed = 0;
    for (size_t i = 0; i < total; i++) {
        nmo_object_t *obj = NULL;
        int rc = nmo_object_repository_take(repo, ids[i], &obj);
        if (rc == NMO_OK && obj != NULL) {
            nmo_object_destroy(obj);
            destroyed++;
        }
    }

    fprintf(stderr, "  destroyed %zu / %zu objects\n", destroyed, total);
    ASSERT_EQ(total, destroyed);

    free(ids);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(bulk_destroy, session_destroy_objects_bulk) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_load(ctx, "data/Ballance/P_Modul_01.nmo");
    if (!session) {
        nmo_context_release(ctx);
        return;
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    size_t total = 0;
    nmo_object_t **all = nmo_object_repository_get_all(repo, &total);
    ASSERT_TRUE(total > 0);

    /* Keep only 3 objects, destroy the rest via session API */
    nmo_object_id_t keep_ids[3];
    size_t keep_count = 0;
    for (size_t i = 0; i < total && keep_count < 3; i++) {
        if (nmo_object_get_class_id(all[i]) == 41) { /* CK3dObject */
            keep_ids[keep_count++] = nmo_object_get_id(all[i]);
        }
    }
    ASSERT_TRUE(keep_count > 0);

    /* Build remove list */
    nmo_object_id_t *remove_ids = (nmo_object_id_t *)malloc(total * sizeof(nmo_object_id_t));
    ASSERT_NOT_NULL(remove_ids);
    size_t remove_count = 0;
    for (size_t i = 0; i < total; i++) {
        nmo_object_id_t oid = nmo_object_get_id(all[i]);
        int is_kept = 0;
        for (size_t k = 0; k < keep_count; k++) {
            if (keep_ids[k] == oid) { is_kept = 1; break; }
        }
        if (!is_kept) remove_ids[remove_count++] = oid;
    }

    fprintf(stderr, "  removing %zu / %zu objects\n", remove_count, total);
    nmo_runtime_report_t report = {0};
    int rc = nmo_session_destroy_objects(session, remove_ids, remove_count, 0, &report);
    fprintf(stderr, "  destroy returned %d, deleted=%zu\n", rc, report.deleted_objects);
    ASSERT_EQ(NMO_OK, rc);
    ASSERT_EQ(remove_count, report.deleted_objects);

    free(remove_ids);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(bulk_destroy, take_and_destroy_all);
    REGISTER_TEST(bulk_destroy, session_destroy_objects_bulk);
TEST_MAIN_END()
