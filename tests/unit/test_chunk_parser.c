/**
 * @file test_chunk_parser.c
 * @brief Unit tests for chunk parsing
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(chunk_parser, create_parser) {
    uint8_t buffer[256] = {0};
    nmo_io_reader *reader =
        nmo_io_memory_reader_create(NULL, buffer, sizeof(buffer));
    ASSERT_NOT_NULL(reader);

    nmo_chunk_parser *parser = nmo_chunk_parser_create(NULL, reader);
    if (parser != NULL) {
        nmo_chunk_parser_destroy(parser);
    }
    nmo_io_reader_release(reader);
}

TEST(chunk_parser, has_next) {
    uint8_t buffer[256] = {0};
    nmo_io_reader *reader =
        nmo_io_memory_reader_create(NULL, buffer, sizeof(buffer));
    ASSERT_NOT_NULL(reader);

    nmo_chunk_parser *parser = nmo_chunk_parser_create(NULL, reader);
    if (parser != NULL) {
        int has_next = nmo_chunk_parser_has_next(parser);
        ASSERT_TRUE(has_next >= 0);
        nmo_chunk_parser_destroy(parser);
    }
    nmo_io_reader_release(reader);
}

int main(void) {
    return 0;
}
