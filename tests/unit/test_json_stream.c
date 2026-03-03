/**
 * @file test_json_stream.c
 * @brief Tests for streaming JSON writer.
 */

#include "test_framework.h"
#include "app/nmo_json_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_stream_text(FILE *fp) {
    if (!fp) {
        return NULL;
    }
    if (fflush(fp) != 0) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        return NULL;
    }

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)size, fp);
    buf[got] = '\0';
    return buf;
}

static void strip_cr(char *s) {
    if (!s) {
        return;
    }
    char *w = s;
    for (char *r = s; *r; ++r) {
        if (*r != '\r') {
            *w++ = *r;
        }
    }
    *w = '\0';
}

TEST(json_stream, object_array_pretty_output) {
    FILE *fp = tmpfile();
    ASSERT_NOT_NULL(fp);

    nmo_json_stream_t w;
    nmo_json_stream_init(&w, fp, true);

    ASSERT_TRUE(nmo_json_stream_begin_object(&w));
    ASSERT_TRUE(nmo_json_stream_key(&w, "a"));
    ASSERT_TRUE(nmo_json_stream_value_uint(&w, 1));
    ASSERT_TRUE(nmo_json_stream_key(&w, "arr"));
    ASSERT_TRUE(nmo_json_stream_begin_array(&w));
    ASSERT_TRUE(nmo_json_stream_value_bool(&w, true));
    ASSERT_TRUE(nmo_json_stream_value_null(&w));
    ASSERT_TRUE(nmo_json_stream_end_array(&w));
    ASSERT_TRUE(nmo_json_stream_end_object(&w));
    ASSERT_TRUE(nmo_json_stream_ok(&w));

    char *actual = read_stream_text(fp);
    fclose(fp);
    ASSERT_NOT_NULL(actual);
    strip_cr(actual);

    const char *expected =
        "{\n"
        "  \"a\": 1,\n"
        "  \"arr\": [\n"
        "    true,\n"
        "    null\n"
        "  ]\n"
        "}";
    ASSERT_STR_EQ(expected, actual);
    free(actual);
}

TEST(json_stream, hex_value_output) {
    FILE *fp = tmpfile();
    ASSERT_NOT_NULL(fp);

    const unsigned char bytes[] = {0xDE, 0xAD, 0xBE, 0xEF};

    nmo_json_stream_t w;
    nmo_json_stream_init(&w, fp, true);
    ASSERT_TRUE(nmo_json_stream_begin_array(&w));
    ASSERT_TRUE(nmo_json_stream_value_hex_bytes(&w, bytes, sizeof(bytes), false));
    ASSERT_TRUE(nmo_json_stream_end_array(&w));
    ASSERT_TRUE(nmo_json_stream_ok(&w));

    char *actual = read_stream_text(fp);
    fclose(fp);
    ASSERT_NOT_NULL(actual);
    strip_cr(actual);

    const char *expected =
        "[\n"
        "  \"deadbeef\"\n"
        "]";
    ASSERT_STR_EQ(expected, actual);
    free(actual);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(json_stream, object_array_pretty_output);
    REGISTER_TEST(json_stream, hex_value_output);
TEST_MAIN_END()
