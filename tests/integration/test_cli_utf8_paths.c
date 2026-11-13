/**
 * @file test_cli_utf8_paths.c
 * @brief nmo with paths beyond ASCII: a file the library saves in a folder
 *        with a Chinese name is read, copied and extracted by nmo, and the
 *        copy loads again. On Windows nmo and this test run with the UTF-8
 *        code page (nmo_use_utf8_code_page), so the command lines are UTF-8.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L /* popen */
#endif

#include "test_framework.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_save.h"
#include "object/nmo_context.h"
#include "runtime/nmo_document.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_session.h"
#include "../../src/platform/platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(__MINGW32__)
#define NMO_POPEN _popen
#define NMO_PCLOSE _pclose
#else
#define NMO_POPEN popen
#define NMO_PCLOSE pclose
#endif

#ifndef NMO_CLI_PATH
#define NMO_CLI_PATH "nmo"
#endif

/* Names in UTF-8: 路径 (path), 文档 (document), 副本 (copy), 导出 (export) */
#define UTF8_FOLDER "cli_utf8_" "\xe8\xb7\xaf\xe5\xbe\x84" "_" "\xc3\xbc"
#define UTF8_DOCUMENT "\xe6\x96\x87\xe6\xa1\xa3" ".nmo"
#define UTF8_COPY "\xe5\x89\xaf\xe6\x9c\xac" ".nmo"
#define UTF8_EXPORT "\xe5\xaf\xbc\xe5\x87\xba"

/* printf into a string malloc'ed to its size */
static char *format(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int length = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (length < 0) {
        return NULL;
    }
    char *text = (char *)malloc((size_t)length + 1);
    if (text != NULL) {
        va_start(args, fmt);
        vsnprintf(text, (size_t)length + 1, fmt, args);
        va_end(args);
    }
    return text;
}

/* What `nmo <args>` prints, its errors too; malloc'ed */
static char *run_cli(const char *args) {
    char *command = format("%s %s 2>&1", NMO_CLI_PATH, args);
    FILE *pipe = command != NULL ? NMO_POPEN(command, "r") : NULL;
    free(command);
    if (pipe == NULL) {
        return NULL;
    }
    size_t capacity = 4096;
    size_t size = 0;
    char *output = (char *)malloc(capacity);
    while (output != NULL) {
        size += fread(output + size, 1, capacity - 1 - size, pipe);
        if (size < capacity - 1) {
            break;
        }
        char *grown = (char *)realloc(output, capacity * 2);
        if (grown == NULL) {
            free(output);
        }
        output = grown;
        capacity *= 2;
    }
    NMO_PCLOSE(pipe);
    if (output != NULL) {
        output[size] = '\0';
    }
    return output;
}

/* A document with one object, saved at `path` */
static nmo_status_t save_document(const char *path) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    if (ctx == NULL) {
        return NMO_ERR_NOMEM;
    }
    nmo_document_t *document = nmo_document_create(ctx);
    nmo_workspace_t *workspace = NULL;
    nmo_status_t status = document != NULL ? nmo_workspace_create(ctx, document, &workspace) : NMO_ERR_NOMEM;
    nmo_object_id_t id = 0;
    if (status == NMO_OK) {
        status = nmo_session_create_object(nmo_workspace_internal_session(workspace), 1, "utf8-object",
                                           (nmo_guid_t){0, 0}, &id, NULL);
    }
    if (status == NMO_OK) {
        status = nmo_document_save_file(document, path, NULL);
    }
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
    return status;
}

static nmo_status_t load_document(const char *path) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    if (ctx == NULL) {
        return NMO_ERR_NOMEM;
    }
    nmo_document_t *document = NULL;
    nmo_status_t status = nmo_document_load_file(ctx, path, NULL, &document);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
    return status;
}

/* Fails the test with nmo's output when `needle` is not in it */
static int output_has(const char *output, const char *needle) {
    if (output != NULL && strstr(output, needle) != NULL) {
        return 1;
    }
    fprintf(stderr, "expected \"%s\" in nmo's output:\n%s\n", needle, output != NULL ? output : "(none)");
    return 0;
}

TEST(cli_utf8_paths, read_copy_and_extract) {
    (void)nmo_platform_make_directory(NMO_TEST_SCRATCH_DIR);
    char *root = format("%s/%s", NMO_TEST_SCRATCH_DIR, UTF8_FOLDER);
    ASSERT_NOT_NULL(root);
    char *document_path = format("%s/%s", root, UTF8_DOCUMENT);
    char *copy_path = format("%s/%s", root, UTF8_COPY);
    char *export_dir = format("%s/%s", root, UTF8_EXPORT);
    ASSERT_TRUE(document_path != NULL && copy_path != NULL && export_dir != NULL);

    /* What an earlier run left */
    (void)nmo_platform_remove(copy_path);
    (void)nmo_platform_remove_directory(export_dir);

    ASSERT_TRUE(nmo_platform_make_directory(root) >= 0);
    ASSERT_EQ(NMO_OK, save_document(document_path));

    char *args = format("file info \"%s\"", document_path);
    char *output = run_cli(args);
    ASSERT_TRUE(output_has(output, "Objects"));
    free(args);
    free(output);

    args = format("convert copy -o \"%s\" \"%s\"", copy_path, document_path);
    output = run_cli(args);
    free(args);
    ASSERT_EQ(NMO_OK, load_document(copy_path));
    free(output);

    args = format("texture extract -d \"%s\" \"%s\"", export_dir, document_path);
    output = run_cli(args);
    free(args);
    if (!nmo_platform_is_directory(export_dir)) {
        fprintf(stderr, "nmo made no folder %s:\n%s\n", export_dir, output != NULL ? output : "(none)");
    }
    ASSERT_TRUE(nmo_platform_is_directory(export_dir));
    free(output);

    ASSERT_EQ(0, nmo_platform_remove_directory(export_dir));
    ASSERT_EQ(0, nmo_platform_remove(copy_path));
    ASSERT_EQ(0, nmo_platform_remove(document_path));
    ASSERT_EQ(0, nmo_platform_remove_directory(root));
    free(root);
    free(document_path);
    free(copy_path);
    free(export_dir);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(cli_utf8_paths, read_copy_and_extract);
TEST_MAIN_END()
