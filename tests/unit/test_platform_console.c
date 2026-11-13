/**
 * @file test_platform_console.c
 * @brief Lines read through the platform layer, and the console's UTF-8 code page
 */

#include "test_framework.h"
#include "../../src/platform/platform.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#endif

#define CHILD_ARGUMENT "--use-utf8-console"

/* A file holding some bytes, in a directory of its own, open for reading them */
typedef struct lines_file {
    char *dir;
    char *path;
    FILE *file;
} lines_file_t;

static bool lines_open(lines_file_t *lines, const char *data, size_t size)
{
    memset(lines, 0, sizeof(*lines));
    lines->dir = nmo_platform_make_temporary_directory("nmo_lines_");
    if (lines->dir == NULL) {
        return false;
    }
    size_t length = strlen(lines->dir) + sizeof("/lines.txt");
    lines->path = (char *)malloc(length);
    if (lines->path == NULL) {
        return false;
    }
    snprintf(lines->path, length, "%s/lines.txt", lines->dir);
    lines->file = nmo_platform_fopen(lines->path, "w+b");
    return lines->file != NULL && fwrite(data, 1, size, lines->file) == size &&
           fseek(lines->file, 0, SEEK_SET) == 0;
}

static void lines_close(lines_file_t *lines)
{
    if (lines->file != NULL) {
        fclose(lines->file);
    }
    if (lines->path != NULL) {
        nmo_platform_remove(lines->path);
        free(lines->path);
    }
    if (lines->dir != NULL) {
        nmo_platform_remove_directory(lines->dir);
        free(lines->dir);
    }
}

/* Read a line and check it is `expected`; NULL expects the end of the file */
static bool lines_next_is(lines_file_t *lines, const char *expected)
{
    char *line = nmo_platform_read_line(lines->file);
    bool same = (line == NULL || expected == NULL) ? line == expected : strcmp(line, expected) == 0;
    free(line);
    return same;
}

TEST(platform_console, reads_lines)
{
    /* "中文" in UTF-8, ended by \r\n; a line longer than the first buffer; a last one with no end */
    static const char head[] = "first\n\xE4\xB8\xAD\xE6\x96\x87\r\n\n";
    enum { LONG_LINE = 1000 };
    size_t size = sizeof(head) - 1 + LONG_LINE + sizeof("\nlast") - 1;
    char *data = (char *)malloc(size);
    char *long_line = (char *)malloc(LONG_LINE + 1);
    ASSERT_NOT_NULL(data);
    ASSERT_NOT_NULL(long_line);
    memset(long_line, 'x', LONG_LINE);
    long_line[LONG_LINE] = '\0';
    memcpy(data, head, sizeof(head) - 1);
    memcpy(data + sizeof(head) - 1, long_line, LONG_LINE);
    memcpy(data + sizeof(head) - 1 + LONG_LINE, "\nlast", sizeof("\nlast") - 1);

    lines_file_t lines;
    bool opened = lines_open(&lines, data, size);
    bool ok = opened && lines_next_is(&lines, "first") && lines_next_is(&lines, "\xE4\xB8\xAD\xE6\x96\x87") &&
              lines_next_is(&lines, "") && lines_next_is(&lines, long_line) && lines_next_is(&lines, "last") &&
              lines_next_is(&lines, NULL) && lines_next_is(&lines, NULL);
    lines_close(&lines);
    free(long_line);
    free(data);
    ASSERT_TRUE(opened);
    ASSERT_TRUE(ok);
}

TEST(platform_console, ends_at_nothing)
{
    lines_file_t lines;
    bool opened = lines_open(&lines, "", 0);
    bool ok = opened && lines_next_is(&lines, NULL);
    lines_close(&lines);
    ASSERT_TRUE(opened);
    ASSERT_TRUE(ok);
    ASSERT_NULL(nmo_platform_read_line(NULL));
}

#ifdef _WIN32

/* A child sharing this console switches it to UTF-8, and it is back as it was once the child ends */
TEST(platform_console, switches_to_utf8_and_back)
{
    UINT before = GetConsoleOutputCP();
    if (before == 0) {
        TEST_SKIP("no console");
    }
    if (before == CP_UTF8) {
        TEST_SKIP("the console is in UTF-8 already");
    }
    char *self = nmo_platform_executable_path();
    ASSERT_NOT_NULL(self);
    intptr_t status = _spawnl(_P_WAIT, self, "test_platform_console", CHILD_ARGUMENT, NULL);
    free(self);
    ASSERT_EQ(status, 0);
    ASSERT_EQ(GetConsoleOutputCP(), before);
}

#else

TEST(platform_console, switches_to_utf8_and_back)
{
    nmo_platform_use_utf8_console();
    TEST_SKIP("only a Windows console has a code page of its own");
}

#endif

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], CHILD_ARGUMENT) == 0) {
        nmo_platform_use_utf8_console();
#ifdef _WIN32
        return GetConsoleOutputCP() == CP_UTF8 ? 0 : 2;
#else
        return 0;
#endif
    }
    TEST_ENTER_WORKING_DIR();
    test_framework_init();
    REGISTER_TEST(platform_console, reads_lines);
    REGISTER_TEST(platform_console, ends_at_nothing);
    REGISTER_TEST(platform_console, switches_to_utf8_and_back);
    return test_framework_run();
}
