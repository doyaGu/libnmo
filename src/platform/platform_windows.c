/**
 * @file platform_windows.c
 * @brief The platform layer on Windows (MSVC and MinGW). Paths are UTF-8,
 *        handed to the system's wide-character functions.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform.h"

#include <io.h>
#include <malloc.h>
#include <stdarg.h>
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

/* free() with the last error kept, so nmo_platform_error_message() still tells the failure before it */
static void platform_free(void *ptr)
{
    DWORD code = GetLastError();
    free(ptr);
    SetLastError(code);
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

/* A UTF-16 string as UTF-8, malloc'ed; NULL when it cannot be */
static char *platform_utf8(const wchar_t *wide)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (size <= 0) {
        return NULL;
    }
    char *utf8 = (char *)malloc((size_t)size);
    if (utf8 != NULL && WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, size, NULL, NULL) <= 0) {
        free(utf8);
        utf8 = NULL;
    }
    return utf8;
}

/* printf into a string malloc'ed to its size */
static char *platform_format(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (length < 0) {
        return NULL;
    }
    char *text = (char *)malloc((size_t)length + 1);
    if (text != NULL) {
        va_start(args, format);
        vsnprintf(text, (size_t)length + 1, format, args);
        va_end(args);
    }
    return text;
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

char *nmo_platform_executable_path(void)
{
    for (DWORD capacity = 260;; capacity *= 2) {
        wchar_t *wide = (wchar_t *)malloc((size_t)capacity * sizeof(wchar_t));
        if (wide == NULL) {
            return NULL;
        }
        DWORD length = GetModuleFileNameW(NULL, wide, capacity);
        if (length > 0 && length < capacity) {
            char *path = platform_utf8(wide);
            free(wide);
            return path;
        }
        free(wide);
        if (length == 0 || capacity >= ((DWORD)1 << 20)) {
            return NULL;
        }
    }
}

char *nmo_platform_home_directory(void)
{
    DWORD size = GetEnvironmentVariableW(L"USERPROFILE", NULL, 0);
    if (size == 0) {
        return NULL;
    }
    wchar_t *wide = (wchar_t *)malloc((size_t)size * sizeof(wchar_t));
    if (wide == NULL) {
        return NULL;
    }
    DWORD length = GetEnvironmentVariableW(L"USERPROFILE", wide, size);
    char *home = length > 0 && length < size ? platform_utf8(wide) : NULL;
    free(wide);
    return home;
}

/* ---- Terminals ---- */

bool nmo_platform_is_terminal(FILE *stream)
{
    return stream != NULL && _isatty(_fileno(stream)) != 0;
}

int nmo_platform_clear_terminal(void)
{
    return system("cls") == 0 ? 0 : -1;
}

/* The console's output code page before nmo_platform_use_utf8_console(); 0 while it has not changed it */
static UINT platform_console_code_page;

static void platform_restore_console(void)
{
    UINT code_page = platform_console_code_page;
    if (code_page != 0) {
        platform_console_code_page = 0;
        SetConsoleOutputCP(code_page);
    }
}

/* Ctrl+C, Ctrl+Break and closing the console end the process without running atexit() */
static BOOL WINAPI platform_console_event(DWORD event)
{
    (void)event;
    platform_restore_console();
    return FALSE; /* the next handler, at last the system's, ends the process */
}

void nmo_platform_use_utf8_console(void)
{
    UINT code_page = GetConsoleOutputCP();
    if (code_page == 0 || code_page == CP_UTF8 || platform_console_code_page != 0) {
        return; /* no console, one that takes UTF-8 already, or changed before */
    }
    if (SetConsoleOutputCP(CP_UTF8)) {
        platform_console_code_page = code_page;
        atexit(platform_restore_console);
        SetConsoleCtrlHandler(platform_console_event, TRUE);
    }
}

/* The console that `stream` reads from; NULL when it reads from something else */
static HANDLE platform_console_input(FILE *stream)
{
    int fd = _fileno(stream);
    if (fd < 0 || !_isatty(fd)) {
        return NULL;
    }
    HANDLE handle = (HANDLE)_get_osfhandle(fd);
    DWORD mode = 0;
    return handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode) ? handle : NULL;
}

/*
 * A line typed at `console`, as UTF-8: the console gives it as UTF-16,
 * where reading bytes would give it in the console's code page. Read one
 * character at a time, so that nothing after the line is taken.
 */
static char *platform_read_console_line(HANDLE console)
{
    size_t capacity = 128u;
    size_t length = 0u;
    wchar_t *line = (wchar_t *)malloc(capacity * sizeof(wchar_t));
    if (line == NULL) {
        return NULL;
    }
    for (;;) {
        wchar_t ch = 0;
        DWORD count = 0;
        if (!ReadConsoleW(console, &ch, 1, &count, NULL) || count == 0) {
            if (length == 0u) {
                platform_free(line);
                return NULL;
            }
            break;
        }
        if (ch == L'\n') {
            break;
        }
        if (length + 2u > capacity) {
            wchar_t *grown = (wchar_t *)realloc(line, capacity * 2u * sizeof(wchar_t));
            if (grown == NULL) {
                free(line);
                return NULL;
            }
            line = grown;
            capacity *= 2u;
        }
        line[length++] = ch;
    }
    if (length > 0u && line[length - 1u] == L'\r') {
        length--;
    }
    /* Ctrl+Z at the start of a line ends the input, as the C library has it */
    if (length > 0u && line[0] == 0x1A) {
        free(line);
        return NULL;
    }
    line[length] = L'\0';
    char *utf8 = platform_utf8(line);
    free(line);
    return utf8;
}

char *nmo_platform_read_line(FILE *stream)
{
    if (stream == NULL) {
        return NULL;
    }
    HANDLE console = platform_console_input(stream);
    if (console != NULL) {
        return platform_read_console_line(console);
    }
    size_t capacity = 128u;
    size_t length = 0u;
    char *line = (char *)malloc(capacity);
    if (line == NULL) {
        return NULL;
    }
    for (;;) {
        int ch = fgetc(stream);
        if (ch == EOF) {
            if (length == 0u) {
                free(line);
                return NULL;
            }
            break;
        }
        if (ch == '\n') {
            break;
        }
        if (length + 2u > capacity) {
            char *grown = (char *)realloc(line, capacity * 2u);
            if (grown == NULL) {
                free(line);
                return NULL;
            }
            line = grown;
            capacity *= 2u;
        }
        line[length++] = (char)ch;
    }
    if (length > 0u && line[length - 1u] == '\r') {
        length--;
    }
    line[length] = '\0';
    return line;
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
    platform_free(wide_path);
    platform_free(wide_mode);
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
    platform_free(wide);
    LARGE_INTEGER size;
    if (mapping->file == INVALID_HANDLE_VALUE || !GetFileSizeEx(mapping->file, &size)) {
        if (mapping->file != INVALID_HANDLE_VALUE) {
            CloseHandle(mapping->file);
        }
        platform_free(mapping);
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
            platform_free(mapping);
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

/* What tells a file from every other on the system: its volume and its index there */
static bool platform_file_identity(const char *path, BY_HANDLE_FILE_INFORMATION *info)
{
    wchar_t *wide = platform_wide(path);
    HANDLE handle = wide != NULL ? CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                               NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL)
                                 : INVALID_HANDLE_VALUE;
    platform_free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    BOOL known = GetFileInformationByHandle(handle, info);
    CloseHandle(handle);
    return known != 0;
}

bool nmo_platform_same_file(const char *a, const char *b)
{
    BY_HANDLE_FILE_INFORMATION x;
    BY_HANDLE_FILE_INFORMATION y;
    return platform_file_identity(a, &x) && platform_file_identity(b, &y) &&
           x.dwVolumeSerialNumber == y.dwVolumeSerialNumber && x.nFileIndexHigh == y.nFileIndexHigh &&
           x.nFileIndexLow == y.nFileIndexLow;
}

/* ---- Directories ---- */

int nmo_platform_make_directory(const char *path)
{
    wchar_t *wide = platform_wide(path);
    if (wide == NULL) {
        return -1;
    }
    BOOL made = CreateDirectoryW(wide, NULL);
    DWORD error = made ? 0 : GetLastError();
    platform_free(wide);
    if (made) {
        return 0;
    }
    return error == ERROR_ALREADY_EXISTS ? 1 : -1;
}

int nmo_platform_remove_directory(const char *path)
{
    wchar_t *wide = platform_wide(path);
    int result = wide != NULL && RemoveDirectoryW(wide) ? 0 : -1;
    platform_free(wide);
    return result;
}

bool nmo_platform_is_directory(const char *path)
{
    wchar_t *wide = platform_wide(path);
    DWORD attributes = wide != NULL ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
    platform_free(wide);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

int nmo_platform_list_directory(const char *path, bool (*visit)(void *context, const char *name),
                                void *context)
{
    size_t length = strlen(path);
    const char *joint = length == 0 || nmo_platform_is_separator(path[length - 1]) ? "" : "\\";
    char *pattern = platform_format("%s%s*", path, joint);
    wchar_t *wide = pattern != NULL ? platform_wide(pattern) : NULL;
    free(pattern);
    WIN32_FIND_DATAW entry;
    HANDLE find = wide != NULL ? FindFirstFileW(wide, &entry) : INVALID_HANDLE_VALUE;
    platform_free(wide);
    if (find == INVALID_HANDLE_VALUE) {
        return -1;
    }
    bool more = true;
    do {
        char *name = platform_utf8(entry.cFileName);
        if (name != NULL) { /* a name UTF-8 cannot hold is passed over */
            more = visit(context, name);
            free(name);
        }
    } while (more && FindNextFileW(find, &entry));
    FindClose(find);
    return 0;
}

char *nmo_platform_make_temporary_directory(const char *prefix)
{
    DWORD size = GetTempPathW(0, NULL);
    wchar_t *wide = size > 0 ? (wchar_t *)malloc((size_t)size * sizeof(wchar_t)) : NULL;
    DWORD length = wide != NULL ? GetTempPathW(size, wide) : 0;
    char *base = length > 0 && length < size ? platform_utf8(wide) : NULL;
    free(wide);
    if (base == NULL) {
        return NULL;
    }
    /* GetTempPathW ends the folder with a backslash */
    unsigned long pid = (unsigned long)GetCurrentProcessId();
    unsigned long seed = (unsigned long)(nmo_platform_ticks() & 0xFFFFFFFFu);
    for (unsigned long attempt = 0; attempt < 64u; attempt++) {
        char *path = platform_format("%s%s%lx-%lx", base, prefix, pid, (seed + attempt * 7919u) & 0xFFFFFFFFu);
        int made = path != NULL ? nmo_platform_make_directory(path) : -1;
        if (made == 0) {
            free(base);
            return path;
        }
        free(path);
        if (made < 0) {
            break;
        }
    }
    free(base);
    return NULL;
}

/* ---- Files written to replace others at once ---- */

struct nmo_platform_file {
    HANDLE handle;
};

/* The attributes MoveFileExW would lose: the replacing file's own are what the target ends with */
#define PLATFORM_KEPT_ATTRIBUTES (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED)

/* The attributes a file replacing the one at `like` takes from it; normal when there is none */
static DWORD platform_kept_attributes(const char *like)
{
    wchar_t *wide = like != NULL ? platform_wide(like) : NULL;
    DWORD attributes = wide != NULL ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
    free(wide);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & PLATFORM_KEPT_ATTRIBUTES) == 0) {
        return FILE_ATTRIBUTE_NORMAL;
    }
    return attributes & PLATFORM_KEPT_ATTRIBUTES;
}

int nmo_platform_file_create_new(const char *path, const char *like, nmo_platform_file_t **out_file)
{
    *out_file = NULL;
    wchar_t *wide = platform_wide(path);
    if (wide == NULL) {
        return -1;
    }
    /* Not FILE_ATTRIBUTE_TEMPORARY: the file keeps its attributes when it is moved in place */
    HANDLE handle = CreateFileW(wide, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                platform_kept_attributes(like) | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        platform_free(wide);
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
    platform_free(wide_from);
    platform_free(wide_to);
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
    platform_free(wide);
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
    platform_free(wide);
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
