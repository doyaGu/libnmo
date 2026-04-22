#include "test_framework.h"

#include "document/nmo_document_stats.h"
#include "export/nmo_export_json.h"

TEST(document_stats_api, semantic_and_json_export_apis_are_separate)
{
    nmo_file_stats_t stats = {0};
    nmo_status_t (*emit_json)(const nmo_file_stats_t *, const char *) =
        nmo_export_json_document_stats;

    ASSERT_EQ(0u, stats.objects.total_count);
    ASSERT_TRUE(emit_json != NULL);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(document_stats_api, semantic_and_json_export_apis_are_separate);
TEST_MAIN_END()
