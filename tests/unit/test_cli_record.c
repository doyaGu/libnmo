/**
 * @file test_cli_record.c
 * @brief Tests for nested objects, JSON and text splices, arrays, tables,
 *        titles, integer lists, and key widths in CLI records
 */

#include "test_framework.h"
#include "nmo_cli_record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Record as compact JSON; the caller frees the result. */
static char *record_json(const nmo_cli_record_t *rec)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, obj);
    char *json = NULL;
    if (nmo_cli_record_to_json(rec, doc, obj)) {
        json = yyjson_mut_write(doc, 0, NULL);
    }
    yyjson_mut_doc_free(doc);
    return json;
}

/* Record as plain key/value text; the caller frees the result. */
static char *record_text(const nmo_cli_record_t *rec, int key_width)
{
    FILE *out = tmpfile();
    if (!out) {
        return NULL;
    }
    nmo_cli_record_print_kv(rec, out, key_width, false);
    long size = ftell(out);
    char *text = size >= 0 ? (char *)malloc((size_t)size + 1u) : NULL;
    if (text) {
        rewind(out);
        size_t n = fread(text, 1u, (size_t)size, out);
        text[n] = '\0';
    }
    fclose(out);
    return text;
}

static bool splice_answer(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                          const void *data)
{
    const int *value = (const int *)data;
    return yyjson_mut_obj_add_int(doc, obj, "answer", *value);
}

static bool splice_fail(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                        const void *data)
{
    (void)doc;
    (void)obj;
    (void)data;
    return false;
}

static void splice_text(FILE *out, bool colorize, const void *data)
{
    fprintf(out, "[%s%s]\n", (const char *)data, colorize ? " color" : "");
}

TEST(cli_record, object_nests_json_and_inlines_text)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    ASSERT_NOT_NULL(rec);
    ASSERT_TRUE(nmo_cli_record_uint(rec, "id", "ID", 7));
    nmo_cli_record_t *child = nmo_cli_record_object(rec, "child");
    ASSERT_NOT_NULL(child);
    ASSERT_TRUE(nmo_cli_record_str(child, "name", "Name", "box"));
    ASSERT_TRUE(nmo_cli_record_bool(child, "hidden", NULL, true));
    ASSERT_TRUE(nmo_cli_record_uint(rec, "after", "After", 1));

    char *json = record_json(rec);
    ASSERT_STR_EQ(json, "{\"id\":7,\"child\":{\"name\":\"box\",\"hidden\":true},\"after\":1}");
    char *text = record_text(rec, 0);
    ASSERT_STR_EQ(text, "ID: 7\nName: box\nAfter: 1\n");
    free(json);
    free(text);
    nmo_cli_record_free(rec);
}

TEST(cli_record, object_without_key_is_text_only)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    nmo_cli_record_t *child = nmo_cli_record_object(rec, NULL);
    ASSERT_NOT_NULL(child);
    ASSERT_TRUE(nmo_cli_record_uint(child, "n", "N", 2));

    char *json = record_json(rec);
    ASSERT_STR_EQ(json, "{}");
    char *text = record_text(rec, 0);
    ASSERT_STR_EQ(text, "N: 2\n");
    free(json);
    free(text);
    nmo_cli_record_free(rec);
}

TEST(cli_record, json_splice_runs_in_order_and_skips_text)
{
    int answer = 42;
    nmo_cli_record_t *rec = nmo_cli_record_new();
    ASSERT_TRUE(nmo_cli_record_uint(rec, "first", "First", 1));
    ASSERT_TRUE(nmo_cli_record_json(rec, splice_answer, &answer));
    ASSERT_TRUE(nmo_cli_record_uint(rec, "last", "Last", 2));
    ASSERT_FALSE(nmo_cli_record_json(rec, NULL, NULL));

    char *json = record_json(rec);
    ASSERT_STR_EQ(json, "{\"first\":1,\"answer\":42,\"last\":2}");
    char *text = record_text(rec, 0);
    ASSERT_STR_EQ(text, "First: 1\nLast: 2\n");
    free(json);
    free(text);
    nmo_cli_record_free(rec);
}

TEST(cli_record, json_splice_failure_fails_render)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    ASSERT_TRUE(nmo_cli_record_json(rec, splice_fail, NULL));
    char *json = record_json(rec);
    ASSERT_NULL(json);
    nmo_cli_record_free(rec);
}

TEST(cli_record, text_splice_runs_in_order_and_skips_json)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    ASSERT_TRUE(nmo_cli_record_uint(rec, "first", "First", 1));
    ASSERT_TRUE(nmo_cli_record_text_splice(rec, splice_text, "mid"));
    ASSERT_TRUE(nmo_cli_record_uint(rec, "last", "Last", 2));
    ASSERT_FALSE(nmo_cli_record_text_splice(rec, NULL, NULL));

    char *json = record_json(rec);
    ASSERT_STR_EQ(json, "{\"first\":1,\"last\":2}");
    char *text = record_text(rec, 0);
    ASSERT_STR_EQ(text, "First: 1\n[mid]\nLast: 2\n");
    free(json);
    free(text);
    nmo_cli_record_free(rec);
}

TEST(cli_record, array_omit_heading_prints_summaries_only)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    ASSERT_TRUE(nmo_cli_record_raw(rec, "Header\n"));
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, "items", NULL);
    ASSERT_NOT_NULL(arr);
    nmo_cli_record_array_omit_heading(arr);
    for (unsigned i = 1; i <= 2; ++i) {
        nmo_cli_record_t *item = nmo_cli_record_new();
        ASSERT_TRUE(nmo_cli_record_uint(item, "n", NULL, i));
        ASSERT_TRUE(nmo_cli_record_set_summary_fmt(item, "Item %u", i));
        ASSERT_TRUE(nmo_cli_record_array_add(arr, item));
    }

    char *json = record_json(rec);
    ASSERT_STR_EQ(json, "{\"items\":[{\"n\":1},{\"n\":2}]}");
    char *text = record_text(rec, 0);
    ASSERT_STR_EQ(text, "Header\nItem 1\nItem 2\n");
    free(json);
    free(text);
    nmo_cli_record_free(rec);
}

TEST(cli_record, array_inline_items_print_item_fields)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    ASSERT_NOT_NULL(rec);
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, "items", NULL);
    ASSERT_NOT_NULL(arr);
    nmo_cli_record_array_omit_heading(arr);
    nmo_cli_record_array_inline_items(arr);
    for (unsigned i = 0; i < 2u; ++i) {
        nmo_cli_record_t *item = nmo_cli_record_new();
        ASSERT_NOT_NULL(item);
        ASSERT_TRUE(nmo_cli_record_uint(item, "id", NULL, i));
        ASSERT_TRUE(nmo_cli_record_raw_fmt(item, "[%u]\n", i));
        ASSERT_TRUE(nmo_cli_record_set_summary(item, "ignored"));
        ASSERT_TRUE(nmo_cli_record_array_add(arr, item));
    }

    char *json = record_json(rec);
    ASSERT_STR_EQ(json, "{\"items\":[{\"id\":0},{\"id\":1}]}");
    char *text = record_text(rec, 0);
    ASSERT_STR_EQ(text, "[0]\n[1]\n");
    free(json);
    free(text);
    nmo_cli_record_free(rec);
}

TEST(cli_record, title_has_no_leading_blank_line)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    ASSERT_NOT_NULL(rec);
    ASSERT_TRUE(nmo_cli_record_title(rec, "First"));
    ASSERT_TRUE(nmo_cli_record_title_fmt(rec, "Second (%d)", 2));
    ASSERT_TRUE(nmo_cli_record_heading(rec, "Third"));

    char *json = record_json(rec);
    ASSERT_STR_EQ(json, "{}");
    char *text = record_text(rec, 0);
    ASSERT_STR_EQ(text, "First\nSecond (2)\n\nThird\n");
    free(json);
    free(text);
    nmo_cli_record_free(rec);
}

TEST(cli_record, int_list_keeps_sign)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    ASSERT_NOT_NULL(rec);
    const int64_t values[] = {-1, 0, 7};
    ASSERT_TRUE(nmo_cli_record_int_list(rec, "values", "Values", values, 3, "-1 0 7"));
    ASSERT_TRUE(nmo_cli_record_int_list(rec, "empty", NULL, NULL, 0, NULL));

    char *json = record_json(rec);
    ASSERT_STR_EQ(json, "{\"values\":[-1,0,7],\"empty\":[]}");
    char *text = record_text(rec, 0);
    ASSERT_STR_EQ(text, "Values: -1 0 7\n");
    free(json);
    free(text);
    nmo_cli_record_free(rec);
}

TEST(cli_record, array_table_prints_item_rows)
{
    static const nmo_cli_table_col_t cols[] = {
        {"ID", NMO_CLI_ALIGN_RIGHT, 0, 0},
        {"NAME", NMO_CLI_ALIGN_LEFT, 0, 0},
    };
    nmo_cli_record_t *rec = nmo_cli_record_new();
    ASSERT_NOT_NULL(rec);
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, "items", NULL);
    ASSERT_NOT_NULL(arr);
    ASSERT_TRUE(nmo_cli_record_array_set_table(arr, cols, 2));
    for (unsigned i = 9; i <= 10; ++i) {
        nmo_cli_record_t *item = nmo_cli_record_new();
        ASSERT_NOT_NULL(item);
        ASSERT_TRUE(nmo_cli_record_uint(item, "id", "ID", i));
        ASSERT_TRUE(nmo_cli_record_str(item, "kind", NULL, "json-only"));
        ASSERT_TRUE(nmo_cli_record_text(item, "NAME", "x"));
        ASSERT_TRUE(nmo_cli_record_array_add(arr, item));
    }
    nmo_cli_record_t *empty = nmo_cli_record_new();
    ASSERT_NOT_NULL(empty);
    nmo_cli_record_array_t *none = nmo_cli_record_array(empty, NULL, NULL);
    ASSERT_NOT_NULL(none);
    ASSERT_TRUE(nmo_cli_record_array_set_table(none, cols, 2));

    char *json = record_json(rec);
    ASSERT_STR_EQ(json, "{\"items\":[{\"id\":9,\"kind\":\"json-only\"},"
                        "{\"id\":10,\"kind\":\"json-only\"}]}");
    char *text = record_text(rec, 0);
    ASSERT_STR_EQ(text, "ID  NAME\n--  ----\n 9  x   \n10  x   \n");
    char *empty_text = record_text(empty, 0);
    ASSERT_STR_EQ(empty_text, "ID  NAME\n--  ----\n");
    free(json);
    free(text);
    free(empty_text);
    nmo_cli_record_free(rec);
    nmo_cli_record_free(empty);
}

TEST(cli_record, omit_json_item_is_text_only)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    ASSERT_NOT_NULL(rec);
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, "items", NULL);
    ASSERT_NOT_NULL(arr);
    nmo_cli_record_array_omit_heading(arr);
    for (unsigned i = 1; i <= 2; ++i) {
        nmo_cli_record_t *item = nmo_cli_record_new();
        ASSERT_NOT_NULL(item);
        ASSERT_TRUE(nmo_cli_record_uint(item, "n", NULL, i));
        ASSERT_TRUE(nmo_cli_record_set_summary_fmt(item, "Item %u", i));
        if (i == 1) {
            nmo_cli_record_omit_json(item);
        }
        ASSERT_TRUE(nmo_cli_record_array_add(arr, item));
    }

    char *json = record_json(rec);
    ASSERT_STR_EQ(json, "{\"items\":[{\"n\":2}]}");
    char *text = record_text(rec, 0);
    ASSERT_STR_EQ(text, "Item 1\nItem 2\n");
    free(json);
    free(text);
    nmo_cli_record_free(rec);
}

TEST(cli_record, object_key_width_overrides_parent)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    ASSERT_NOT_NULL(rec);
    ASSERT_TRUE(nmo_cli_record_uint(rec, "a", "A", 1));
    nmo_cli_record_t *child = nmo_cli_record_object(rec, "child");
    ASSERT_NOT_NULL(child);
    nmo_cli_record_set_key_width(child, 3);
    ASSERT_TRUE(nmo_cli_record_uint(child, "b", "B", 2));
    ASSERT_TRUE(nmo_cli_record_uint(rec, "c", "C", 3));

    char *text = record_text(rec, 5);
    ASSERT_STR_EQ(text, "A    : 1\nB  : 2\nC    : 3\n");
    free(text);
    nmo_cli_record_free(rec);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli_record, object_nests_json_and_inlines_text);
    REGISTER_TEST(cli_record, object_without_key_is_text_only);
    REGISTER_TEST(cli_record, json_splice_runs_in_order_and_skips_text);
    REGISTER_TEST(cli_record, json_splice_failure_fails_render);
    REGISTER_TEST(cli_record, text_splice_runs_in_order_and_skips_json);
    REGISTER_TEST(cli_record, array_omit_heading_prints_summaries_only);
    REGISTER_TEST(cli_record, array_inline_items_print_item_fields);
    REGISTER_TEST(cli_record, title_has_no_leading_blank_line);
    REGISTER_TEST(cli_record, int_list_keeps_sign);
    REGISTER_TEST(cli_record, array_table_prints_item_rows);
    REGISTER_TEST(cli_record, omit_json_item_is_text_only);
    REGISTER_TEST(cli_record, object_key_width_overrides_parent);
TEST_MAIN_END()
