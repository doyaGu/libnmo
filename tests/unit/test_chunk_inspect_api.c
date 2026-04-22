#include "test_framework.h"

#include "chunk/nmo_chunk_inspect.h"
#include "export/nmo_export_text.h"

#include <stdio.h>

TEST(chunk_inspect_api, semantic_and_text_export_apis_are_separate)
{
    nmo_chunk_validation_t validation = {0};
    nmo_status_t (*emit_text)(
        const nmo_chunk_t *,
        FILE *,
        const nmo_export_text_chunk_options_t *) = nmo_export_text_chunk;

    ASSERT_FALSE(validation.is_valid);
    ASSERT_TRUE(emit_text != NULL);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_inspect_api, semantic_and_text_export_apis_are_separate);
TEST_MAIN_END()
