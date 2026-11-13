/**
 * @file platform_posix.c
 * @brief The platform layer on POSIX systems: macOS, Linux, the BSDs.
 */

/* mkdtemp, fdatasync, fseeko, fileno, readlink, clock_gettime */
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE /* where Darwin declares mkdtemp */
#endif

#include "platform.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

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

/* ---- Memory ---- */

void *nmo_platform_aligned_alloc(size_t size, size_t alignment)
{
    /* malloc's own alignment covers the small ones */
    if (alignment <= sizeof(void *) * 2) {
        return malloc(size);
    }
    void *ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return NULL;
    }
    return ptr;
}

void nmo_platform_aligned_free(void *ptr)
{
    free(ptr);
}

/* ---- Time and processes ---- */

uint64_t nmo_platform_ticks(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

double nmo_platform_ticks_to_ms(uint64_t ticks)
{
    return (double)ticks / 1000000.0;
}

unsigned long nmo_platform_process_id(void)
{
    return (unsigned long)getpid();
}

char *nmo_platform_executable_path(void)
{
#if defined(__APPLE__)
    uint32_t size = 0;
    (void)_NSGetExecutablePath(NULL, &size); /* tells the size */
    char *path = (char *)malloc(size > 0 ? size : 1);
    if (path != NULL && _NSGetExecutablePath(path, &size) != 0) {
        free(path);
        path = NULL;
    }
    return path;
#elif defined(__linux__)
    for (size_t capacity = 256;; capacity *= 2) {
        char *path = (char *)malloc(capacity);
        if (path == NULL) {
            return NULL;
        }
        ssize_t length = readlink("/proc/self/exe", path, capacity - 1);
        if (length > 0 && (size_t)length < capacity - 1) {
            path[length] = '\0';
            return path;
        }
        free(path);
        if (length <= 0 || capacity >= ((size_t)1 << 20)) {
            return NULL;
        }
    }
#else
    return NULL;
#endif
}

char *nmo_platform_home_directory(void)
{
    const char *home = getenv("HOME");
    return home != NULL && home[0] != '\0' ? platform_copy(home) : NULL;
}

/* ---- Terminals ---- */

bool nmo_platform_is_terminal(FILE *stream)
{
    return stream != NULL && isatty(fileno(stream)) == 1;
}

int nmo_platform_clear_terminal(void)
{
    return system("clear") == 0 ? 0 : -1;
}

void nmo_platform_use_utf8_console(void)
{
}

char *nmo_platform_read_line(FILE *stream)
{
    if (stream == NULL) {
        return NULL;
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
    return errno != 0 ? platform_copy(strerror(errno)) : NULL;
}

/* ---- Paths ---- */

char nmo_platform_separator(void)
{
    return '/';
}

bool nmo_platform_is_separator(char c)
{
    return c == '/';
}

/* ---- Files read whole ---- */

FILE *nmo_platform_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int nmo_platform_seek(FILE *file, int64_t offset, int whence)
{
    return fseeko(file, (off_t)offset, whence) == 0 ? 0 : -1;
}

int64_t nmo_platform_tell(FILE *file)
{
    return (int64_t)ftello(file);
}

struct nmo_platform_mapping {
    int fd;
    void *data;
    size_t size;
};

nmo_platform_mapping_t *nmo_platform_map(const char *path, const void **out_data, size_t *out_size)
{
    nmo_platform_mapping_t *mapping = (nmo_platform_mapping_t *)calloc(1, sizeof(*mapping));
    if (mapping == NULL) {
        return NULL;
    }
    mapping->fd = open(path, O_RDONLY);
    struct stat st;
    if (mapping->fd < 0 || fstat(mapping->fd, &st) != 0) {
        if (mapping->fd >= 0) {
            close(mapping->fd);
        }
        free(mapping);
        return NULL;
    }
    mapping->size = (size_t)st.st_size;
    if (mapping->size > 0) {
        mapping->data = mmap(NULL, mapping->size, PROT_READ, MAP_PRIVATE, mapping->fd, 0);
        if (mapping->data == MAP_FAILED) {
            close(mapping->fd);
            free(mapping);
            return NULL;
        }
    }
    *out_data = mapping->data;
    *out_size = mapping->size;
    return mapping;
}

void nmo_platform_unmap(nmo_platform_mapping_t *mapping)
{
    if (mapping == NULL) {
        return;
    }
    if (mapping->data != NULL) {
        munmap(mapping->data, mapping->size);
    }
    close(mapping->fd);
    free(mapping);
}

bool nmo_platform_same_file(const char *a, const char *b)
{
    struct stat sa;
    struct stat sb;
    return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

/* ---- Directories ---- */

int nmo_platform_make_directory(const char *path)
{
    if (mkdir(path, 0777) == 0) {
        return 0;
    }
    return errno == EEXIST ? 1 : -1;
}

int nmo_platform_remove_directory(const char *path)
{
    return rmdir(path) == 0 ? 0 : -1;
}

bool nmo_platform_is_directory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int nmo_platform_list_directory(const char *path, bool (*visit)(void *context, const char *name),
                                void *context)
{
    DIR *dir = opendir(path[0] != '\0' ? path : ".");
    if (dir == NULL) {
        return -1;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && visit(context, entry->d_name)) {
    }
    closedir(dir);
    return 0;
}

char *nmo_platform_make_temporary_directory(const char *prefix)
{
    const char *base = getenv("TMPDIR");
    if (base == NULL || base[0] == '\0') {
        base = "/tmp";
    }
    static const char unique[] = "XXXXXX";
    size_t base_length = strlen(base);
    size_t prefix_length = strlen(prefix);
    size_t joint = base[base_length - 1] == '/' ? 0 : 1;
    char *path = (char *)malloc(base_length + joint + prefix_length + sizeof(unique));
    if (path == NULL) {
        return NULL;
    }
    memcpy(path, base, base_length);
    if (joint != 0) {
        path[base_length] = '/';
    }
    memcpy(path + base_length + joint, prefix, prefix_length);
    memcpy(path + base_length + joint + prefix_length, unique, sizeof(unique));
    if (mkdtemp(path) == NULL) {
        free(path);
        return NULL;
    }
    return path;
}

/* ---- Files written to replace others at once ---- */

struct nmo_platform_file {
    int fd;
};

int nmo_platform_file_create_new(const char *path, const char *like, nmo_platform_file_t **out_file)
{
    *out_file = NULL;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        return errno == EEXIST ? 1 : -1;
    }
    /* The permissions of the file it will replace, else rw-r--r-- */
    struct stat st;
    mode_t mode = 0644;
    if (like != NULL && stat(like, &st) == 0) {
        mode = st.st_mode & 0777;
    }
    (void)fchmod(fd, mode);
    nmo_platform_file_t *file = (nmo_platform_file_t *)malloc(sizeof(*file));
    if (file == NULL) {
        close(fd);
        unlink(path);
        return -1;
    }
    file->fd = fd;
    *out_file = file;
    return 0;
}

int nmo_platform_file_write(nmo_platform_file_t *file, const void *data, size_t size)
{
    size_t written = 0;
    while (written < size) {
        ssize_t n = write(file->fd, (const char *)data + written, size - written);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return -1;
        }
        written += (size_t)n;
    }
    return 0;
}

int nmo_platform_file_sync(nmo_platform_file_t *file, bool data_only)
{
#if defined(__linux__)
    if (data_only) {
        return fdatasync(file->fd) == 0 ? 0 : -1;
    }
#else
    (void)data_only; /* no fdatasync: the whole of it */
#endif
    return fsync(file->fd) == 0 ? 0 : -1;
}

int nmo_platform_file_close(nmo_platform_file_t *file)
{
    int result = close(file->fd) == 0 ? 0 : -1;
    free(file);
    return result;
}

int nmo_platform_replace(const char *from, const char *to, bool write_through)
{
    (void)write_through; /* rename is atomic; its durability is the directory's sync */
    return rename(from, to) == 0 ? 0 : -1;
}

int nmo_platform_sync_directory(const char *path)
{
    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    int fd = open(path, flags);
    if (fd < 0) {
        return -1;
    }
    int result;
    do {
        result = fsync(fd);
    } while (result != 0 && errno == EINTR);
    int saved = errno;
    if (close(fd) != 0 && result == 0) {
        return -1;
    }
    errno = saved;
    return result == 0 ? 0 : -1;
}

int nmo_platform_remove(const char *path)
{
    return unlink(path) == 0 ? 0 : -1;
}

/* ---- Shared libraries ---- */

static void platform_dl_error(char **out_error, const char *fallback)
{
    if (out_error != NULL) {
        const char *message = dlerror();
        *out_error = platform_copy(message != NULL ? message : fallback);
    }
}

void *nmo_platform_library_open(const char *path, char **out_error)
{
    dlerror(); /* forget any error before this one */
    void *library = dlopen(path, RTLD_NOW);
    if (library == NULL) {
        platform_dl_error(out_error, "dynamic loader error (unknown)");
    }
    return library;
}

int nmo_platform_library_symbol(void *library, const char *name, void **out_symbol, char **out_error)
{
    dlerror();
    *out_symbol = dlsym(library, name);
    const char *message = dlerror();
    if (message != NULL) {
        *out_symbol = NULL;
        if (out_error != NULL) {
            *out_error = platform_copy(message);
        }
        return -1;
    }
    return 0;
}

void nmo_platform_library_close(void *library)
{
    if (library != NULL) {
        (void)dlclose(library);
    }
}
