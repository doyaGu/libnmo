/**
 * @file platform.h
 * @brief What libnmo takes from the operating system, in one place.
 *
 * The platform layer is the lowest: it uses only the C library and the
 * operating system, and every other layer reaches the operating system
 * through it. platform_posix.c implements it for POSIX systems (macOS,
 * Linux, the BSDs), platform_windows.c for Windows (MSVC and MinGW). Paths
 * are UTF-8 on every system. A port to another system implements this
 * header and nothing else.
 *
 * Functions that can fail return 0 on success and -1 on failure, unless
 * they say otherwise; nmo_platform_error_message() then tells why.
 */

#ifndef NMO_PLATFORM_H
#define NMO_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---- Memory ---- */

/* `size` bytes aligned to `alignment`, a power of two; NULL when out of memory */
void *nmo_platform_aligned_alloc(size_t size, size_t alignment);
/* Free what nmo_platform_aligned_alloc() gave; NULL does nothing */
void nmo_platform_aligned_free(void *ptr);

/* ---- Time and processes ---- */

/* A monotonic clock, in ticks of its own */
uint64_t nmo_platform_ticks(void);
/* A count of ticks in milliseconds */
double nmo_platform_ticks_to_ms(uint64_t ticks);
/* This process's id */
unsigned long nmo_platform_process_id(void);
/* The path of the running executable, malloc'ed; NULL when it is not known */
char *nmo_platform_executable_path(void);
/* The user's home directory, malloc'ed; NULL when it is not known */
char *nmo_platform_home_directory(void);

/* ---- Terminals ---- */

/* Whether `stream` is an interactive terminal */
bool nmo_platform_is_terminal(FILE *stream);
/* Clear the terminal's screen with the system's command for it */
int nmo_platform_clear_terminal(void);

/* ---- Errors ---- */

/* The system's message for the last failure, malloc'ed to its size; NULL when there is none */
char *nmo_platform_error_message(void);

/* ---- Paths ---- */

/* The separator between a path's parts, and whether a character is one */
char nmo_platform_separator(void);
bool nmo_platform_is_separator(char c);

/* ---- Files read whole ---- */

/* fopen with a UTF-8 path */
FILE *nmo_platform_fopen(const char *path, const char *mode);
/* fseek and ftell with 64-bit offsets */
int nmo_platform_seek(FILE *file, int64_t offset, int whence);
int64_t nmo_platform_tell(FILE *file);

/* A file mapped into memory, read only */
typedef struct nmo_platform_mapping nmo_platform_mapping_t;

/* Map the file at `path`: its bytes (NULL for an empty file) and size; NULL when it cannot be */
nmo_platform_mapping_t *nmo_platform_map(const char *path, const void **out_data, size_t *out_size);
void nmo_platform_unmap(nmo_platform_mapping_t *mapping);

/* Whether `a` and `b` are the same existing file, whatever names they have */
bool nmo_platform_same_file(const char *a, const char *b);

/* ---- Directories ---- */

/* Make the directory at `path`: 0, 1 when something is there already, -1 when it cannot be made */
int nmo_platform_make_directory(const char *path);
/* Remove the empty directory at `path` */
int nmo_platform_remove_directory(const char *path);
/* Whether there is a directory at `path` */
bool nmo_platform_is_directory(const char *path);
/*
 * Call `visit` with the name of each entry of the directory at `path`, in
 * the system's order ("." and ".." too where the system lists them), until
 * it returns false. -1 when the directory cannot be read.
 */
int nmo_platform_list_directory(const char *path, bool (*visit)(void *context, const char *name),
                                void *context);
/* A new empty directory in the system's temporary one, named `prefix` and more; malloc'ed, NULL when none could be made */
char *nmo_platform_make_temporary_directory(const char *prefix);

/* ---- Files written to replace others at once ---- */

typedef struct nmo_platform_file nmo_platform_file_t;

/*
 * Create the file at `path` for writing, only if there is none: 0 and the
 * file, 1 when one is there already, -1 when it cannot be made. From the
 * file at `like`, when there is one, it takes what replacing that file would
 * otherwise lose: its permissions (POSIX), or its hidden, system and
 * not-indexed attributes (Windows).
 */
int nmo_platform_file_create_new(const char *path, const char *like, nmo_platform_file_t **out_file);
/* Write all of `size` bytes */
int nmo_platform_file_write(nmo_platform_file_t *file, const void *data, size_t size);
/* Make what was written durable: data and metadata, or the data alone when `data_only` */
int nmo_platform_file_sync(nmo_platform_file_t *file, bool data_only);
/* Close the file; it is freed even when closing fails */
int nmo_platform_file_close(nmo_platform_file_t *file);
/*
 * Put the file at `from` in place of the one at `to`, at once; with
 * `write_through` the move itself reaches the disk before this returns
 * (where the system says so; POSIX needs nmo_platform_sync_directory()).
 */
int nmo_platform_replace(const char *from, const char *to, bool write_through);
/* Make the entries of the directory at `path` durable (a rename into it, say); 0 where there is no need */
int nmo_platform_sync_directory(const char *path);
int nmo_platform_remove(const char *path);

/* ---- Shared libraries ---- */

/* The library at `path`, loaded with its symbols bound; NULL when it cannot be (the message in *out_error, malloc'ed) */
void *nmo_platform_library_open(const char *path, char **out_error);
/* A symbol of a library (its value may be NULL); -1 when it has none (the message in *out_error, malloc'ed) */
int nmo_platform_library_symbol(void *library, const char *name, void **out_symbol, char **out_error);
void nmo_platform_library_close(void *library);

#endif /* NMO_PLATFORM_H */
