#include "test_framework.h"

#include "nmo.h"

#include "document/nmo_document_compare.h"
#include "document/nmo_document.h"
#include "object/nmo_object_summary.h"
#include "object/nmo_object_diff.h"
#include "object/nmo_object_query.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_session_util.h"
#include "type/nmo_reflection.h"

#include <stdalign.h>
#include <string.h>

typedef struct report_view_ref_state {
    nmo_object_id_t target_id;
} report_view_ref_state_t;

static const nmo_guid_t report_view_ref_guid = NMO_GUID_INIT(0x51A4E101u, 0x00000001u);
static const nmo_type_field_t report_view_ref_fields[] = {
    NMO_FIELD_REF(report_view_ref_state_t, target_id),
};

TEST(report_view, diff_view_preserves_paths_for_renamed_objects)
{
    nmo_context_t *ctx1 = NULL;
    nmo_session_t *session1 = NULL;
    nmo_context_t *ctx2 = NULL;
    nmo_session_t *session2 = NULL;
    nmo_document_t *document2 = NULL;
    nmo_object_t *renamed = NULL;
    nmo_diff_view_t view;
    nmo_status_t status = NMO_OK;
    char errbuf[256] = {0};
    size_t i = 0u;
    bool found = false;

    ASSERT_TRUE(nmo_session_open_file_with_context(
        NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
        &ctx1, &session1, errbuf, sizeof(errbuf)));
    ASSERT_TRUE(nmo_session_open_file_with_context(
        NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
        &ctx2, &session2, errbuf, sizeof(errbuf)));
    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session2, &document2));
    ASSERT_NOT_NULL(document2);

    status = nmo_object_query_resolve_one(
        document2,
        &(nmo_object_selector_t){.name = "InGameCam"},
        &renamed,
        NULL);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_NOT_NULL(renamed);
    ASSERT_EQ(NMO_OK,
              nmo_object_repository_rename(nmo_session_get_repository(session2),
                                           nmo_object_get_id(renamed),
                                           "InGameCam_Renamed"));

    memset(&view, 0, sizeof(view));
    status = nmo_diff_build_view(session1, session2, &view);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(view.renamed_count > 0u);
    for (i = 0u; i < view.renamed_count; ++i) {
        if (view.renamed[i].before_name != NULL &&
            strcmp(view.renamed[i].before_name, "InGameCam") == 0) {
            ASSERT_NOT_NULL(view.renamed[i].before_path);
            ASSERT_NOT_NULL(view.renamed[i].after_path);
            ASSERT_TRUE(view.renamed[i].before_path[0] != '\0');
            ASSERT_TRUE(view.renamed[i].after_path[0] != '\0');
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    nmo_diff_view_destroy(&view);
    nmo_session_close_with_context(ctx1, session1);
    nmo_session_close_with_context(ctx2, session2);
}

TEST(report_view, object_summary_view_preserves_resolved_reference_names)
{
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *target = NULL;
    nmo_object_t *holder = NULL;
    nmo_object_t *holder_in_repo = NULL;
    report_view_ref_state_t *state = NULL;
    nmo_type_descriptor_t desc = {
        .guid = report_view_ref_guid,
        .id = NMO_TYPE_ID_INVALID,
        .class_id = 0,
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = 0,
        .name = "ReportViewRefState",
        .description = NULL,
        .base_type = NMO_NULL_GUID,
        .base_type_id = NMO_TYPE_ID_INVALID,
        .size = (uint32_t)sizeof(report_view_ref_state_t),
        .alignment = (uint32_t)alignof(report_view_ref_state_t),
        .fields = report_view_ref_fields,
        .field_count = sizeof(report_view_ref_fields) / sizeof(report_view_ref_fields[0]),
        .vtable = NULL,
        .creator_plugin_guid = NMO_NULL_GUID,
        .saver_manager = 0,
        .specialized_index = NMO_SPECIALIZED_INDEX_INVALID,
        .valid = true,
        .version = 0,
        .min_compatible_version = 0,
        .ext = NULL,
    };
    nmo_object_summary_view_t view;
    size_t i = 0u;
    bool found = false;

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);

    ASSERT_EQ(NMO_OK, nmo_type_registry_begin_update(nmo_context_get_type_registry(ctx)));
    ASSERT_EQ(NMO_OK, nmo_type_registry_register(nmo_context_get_type_registry(ctx), &desc));

    target = nmo_object_create(NULL, 200u, 0);
    ASSERT_NOT_NULL(target);
    ASSERT_EQ(NMO_OK, nmo_object_set_name(target, "Target Object"));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &target));

    holder = nmo_object_create(NULL, 100u, 0);
    ASSERT_NOT_NULL(holder);
    ASSERT_EQ(NMO_OK, nmo_object_set_name(holder, "Holder Object"));
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(holder, report_view_ref_guid));
    ASSERT_EQ(NMO_OK, nmo_object_alloc_state(holder, sizeof(*state)));
    state = (report_view_ref_state_t *)nmo_object_get_state(holder);
    ASSERT_NOT_NULL(state);
    state->target_id = 200u;
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &holder));

    holder_in_repo = nmo_object_repository_find_by_id(repo, 100u);
    ASSERT_NOT_NULL(holder_in_repo);

    memset(&view, 0, sizeof(view));
    ASSERT_EQ(NMO_OK, nmo_object_summary_build_view(ctx, session, holder_in_repo, &view));
    for (i = 0u; i < view.field_count; ++i) {
        if (view.fields[i].name != NULL &&
            strcmp(view.fields[i].name, "target_id") == 0 &&
            view.fields[i].ref_name != NULL &&
            strcmp(view.fields[i].ref_name, "Target Object") == 0) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    nmo_object_summary_view_destroy(&view);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(report_view, diff_view_preserves_paths_for_renamed_objects);
    REGISTER_TEST(report_view, object_summary_view_preserves_resolved_reference_names);
TEST_MAIN_END()

