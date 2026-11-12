/**
 * @file platform_windows.c
 * @brief The platform layer on Windows (MSVC and MinGW). Paths are UTF-8,
 *        handed to the system's wide-character functions.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform.h"

#include <malloc.h>
#include <stdlib.h>
#include <string.h>

/* A copy of `text`, malloc'ed to its size */
static char *platform_copy(const char *text)
{
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, text, length + 1);
    }
    return copy;
}

/* A UTF-8 string as UTF-16, malloc'ed; NULL when it is not one */
static wchar_t *platform_wide(const char *utf8)
{
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0);
    if (size <= 0) {
        return NULL;
    }
    wchar_t *wide = (wchar_t *)malloc((size_t)size * sizeof(wchar_t));
    if (wide != NULL && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide, size) <= 0) {
        free(wide);
        wide = NULL;
    }
    return wide;
}

/* The system's message for an error code, malloc'ed, its line ends trimmed */
static char *platform_message(DWORD code)
{
    char *buffer = NULL;
    DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                      FORMAT_MESSAGE_IGNORE_INSERTS,
                                  NULL, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buffer, 0,
                                  NULL);
    if (length == 0 || buffer == NULL) {
        return NULL;
    }
    while (length > 0 && (buffer[length - 1] == '\r' || buffer[length - 1] == '\n' || buffer[length - 1] == ' ')) {
        buffer[--length] = '\0';
    }
    char *copy = platform_copy(buffer);
    LocalFree(buffer);
    return copy;
}

/* ---- Memory ---- */

void *nmo_platform_aligned_alloc(size_t size, size_t alignment)
{
    return _aligned_malloc(size, alignment < sizeof(void *) ? sizeof(void *) : alignment);
}

void nmo_platform_aligned_free(void *ptr)
{
    _aligned_free(ptr);
}

/* ---- Time and processes ---- */

uint64_t nmo_platform_ticks(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)counter.QuadPart;
}

double nmo_platform_ticks_to_ms(uint64_t ticks)
{
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
        return 0.0;
    }
    return ((double)ticks * 1000.0) / (double)frequency.QuadPart;
}

unsigned long nmo_platform_process_id(void)
{
    return (unsigned long)GetCurrentProcessId();
}

/* ---- Errors ---- */

char *nmo_platform_error_message(void)
{
    DWORD code = GetLastError();
    return code != 0 ? platform_message(code) : NULL;
}

/* ---- Paths ---- */

char nmo_platform_separator(void)
{
    return '\\';
}

bool nmo_platform_is_separator(char c)
{
    return c == '\\' || c == '/';
}

/* ---- Files read whole ---- */

FILE *nmo_platform_fopen(const char *path, const char *mode)
{
    wchar_t *wide_path = platform_wide(path);
    wchar_t *wide_mode = platform_wide(mode);
    FILE *file = wide_path != NULL && wide_mode != NULL ? _wfopen(wide_path, wide_mode) : NULL;
    free(wide_path);
    free(wide_mode);
    return file;
}

int nmo_platform_seek(FILE *file, int64_t offset, int whence)
{
    return _fseeki64(file, offset, whence) == 0 ? 0 : -1;
}

int64_t nmo_platform_tell(FILE *file)
{
    return (int64_t)_ftelli64(file);
}

struct nmo_platform_mapping {
    HANDLE file;
    HANDLE mapping;
    void *data;
};

nmo_platform_mapping_t *nmo_platform_map(const char *path, const void **out_data, size_t *out_size)
{
    wchar_t *wide = platform_wide(path);
    if (wide == NULL) {
        return NULL;
    }
    nmo_platform_mapping_t *mapping = (nmo_platform_mapping_t *)calloc(1, sizeof(*mapping));
    if (mapping == NULL) {
        free(wide);
        return NULL;
    }
    mapping->file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide);
    LARGE_INTEGER size;
    if (mapping->file == INVALID_HANDLE_VALUE || !GetFileSizeEx(mapping->file, &size)) {
        if (mapping->file != INVALID_HANDLE_VALUE) {
            CloseHandle(mapping->file);
        }
        free(mapping);
        return NULL;
    }
    if (size.QuadPart > 0) {
        mapping->mapping = CreateFileMappingW(mapping->file, NULL, PAGE_READONLY, 0, 0, NULL);
        mapping->data = mapping->mapping != NULL ? MapViewOfFile(mapping->mapping, FILE_MAP_READ, 0, 0, 0) : NULL;
        if (mapping->data == NULL) {
            if (mapping->mapping != NULL) {
                CloseHandle(mapping->mapping);
            }
            CloseHandle(mapping->file);
            free(mapping);
            return NULL;
        }
    }
    *out_data = mapping->data;
    *out_size = (size_t)size.QuadPart;
    return mapping;
}

void nmo_platform_unmap(nmo_platform_mapping_t *mapping)
{
    if (mapping == NULL) {
        return;
    }
    if (mapping->data != NULL) {
        UnmapViewOfFile(mapping->data);
    }
    if (mapping->mapping != NULL) {
        CloseHandle(mapping->mapping);
    }
    CloseHandle(mapping->file);
    free(mapping);
}

/* ---- Files written to replace others at once ---- */

struct nmo_platform_file {
    HANDLE handle;
};

int nmo_platform_file_create_new(const char *path, const char *like, nmo_platform_file_t **out_file)
{
    (void)like; /* a replaced file's attributes are the move's to keep */
    *out_file = NULL;
    wchar_t *wide = platform_wide(path);
    if (wide == NULL) {
        return -1;
    }
    HANDLE handle = CreateFileW(wide, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        free(wide);
        return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ? 1 : -1;
    }
    nmo_platform_file_t *file = (nmo_platform_file_t *)malloc(sizeof(*file));
    if (file == NULL) {
        CloseHandle(handle);
        DeleteFileW(wide);
        free(wide);
        return -1;
    }
    free(wide);
    file->handle = handle;
    *out_file = file;
    return 0;
}

int nmo_platform_file_write(nmo_platform_file_t *file, const void *data, size_t size)
{
    size_t written = 0;
    while (written < size) {
        size_t left = size - written;
        DWORD chunk = left > 0x40000000u ? 0x40000000u : (DWORD)left;
        DWORD done = 0;
        if (!WriteFile(file->handle, (const char *)data + written, chunk, &done, NULL) || done == 0) {
            return -1;
        }
        written += done;
    }
    return 0;
}

int nmo_platform_file_sync(nmo_platform_file_t *file, bool data_only)
{
    (void)data_only; /* FlushFileBuffers is the whole of it */
    return FlushFileBuffers(file->handle) ? 0 : -1;
}

int nmo_platform_file_close(nmo_platform_file_t *file)
{
    int result = CloseHandle(file->handle) ? 0 : -1;
    free(file);
    return result;
}

int nmo_platform_replace(const char *from, const char *to, bool write_through)
{
    wchar_t *wide_from = platform_wide(from);
    wchar_t *wide_to = platform_wide(to);
    DWORD flags = MOVEFILE_REPLACE_EXISTING | (write_through ? MOVEFILE_WRITE_THROUGH : 0u);
    int result = wide_from != NULL && wide_to != NULL && MoveFileExW(wide_from, wide_to, flags) ? 0 : -1;
    free(wide_from);
    free(wide_to);
    return result;
}

int nmo_platform_sync_directory(const char *path)
{
    (void)path; /* MOVEFILE_WRITE_THROUGH made the move durable */
    return 0;
}

int nmo_platform_remove(const char *path)
{
    wchar_t *wide = platform_wide(path);
    int result = wide != NULL && DeleteFileW(wide) ? 0 : -1;
    free(wide);
    return result;
}

/* ---- Shared libraries ---- */

static void platform_last_error(char **out_error, const char *fallback)
{
    if (out_error != NULL) {
        char *message = nmo_platform_error_message();
        *out_error = message != NULL ? message : platform_copy(fallback);
    }
}

void *nmo_platform_library_open(const char *path, char **out_error)
{
    wchar_t *wide = platform_wide(path);
    HMODULE library = wide != NULL ? LoadLibraryW(wide) : NULL;
    free(wide);
    if (library == NULL) {
        platform_last_error(out_error, "LoadLibrary failed");
    }
    return (void *)library;
}

int nmo_platform_library_symbol(void *library, const char *name, void **out_symbol, char **out_error)
{
    FARPROC proc = GetProcAddress((HMODULE)library, name);
    if (proc == NULL) {
        *out_symbol = NULL;
        platform_last_error(out_error, "GetProcAddress failed");
        return -1;
    }
    union {
        FARPROC proc;
        void *ptr;
    } cast;
    cast.proc = proc;
    *out_symbol = cast.ptr;
    return 0;
}

void nmo_platform_library_close(void *library)
{
    if (library != NULL) {
        (void)FreeLibrary((HMODULE)library);
    }
}
