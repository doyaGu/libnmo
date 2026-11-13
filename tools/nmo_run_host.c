/**
 * @file nmo_run_host.c
 * @brief What a host running a document in the engine gives it: files,
 *        registry values, a game player's paths and keys by frame.
 */

#include "nmo_run_host.h"

#include "behavior/nmo_behavior_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/platform/platform.h"

/* ---- Keys held down by frame ---- */

/* A key: a name nmo_behavior_keyboard_key_name() spells, or a scan code. */
uint32_t nmo_run_key_code(const char *name, size_t length)
{
    char *copy = (char *)malloc(length + 1u);
    if (copy == NULL) {
        return 0;
    }
    memcpy(copy, name, length);
    copy[length] = '\0';
    uint32_t key = nmo_behavior_keyboard_key_from_name(copy);
    if (key == 0 && length > 0) {
        char *end = NULL;
        unsigned long code = strtoul(copy, &end, 0);
        key = (end != NULL && *end == '\0' && code > 0 && code < 256) ? (uint32_t)code : 0u;
    }
    free(copy);
    return key;
}

bool nmo_run_frame_number(const char **text, uint32_t *out)
{
    const char *start = *text;
    char *end = NULL;
    unsigned long value = strtoul(start, &end, 10);
    if (end == start || value > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)value;
    *text = end;
    return true;
}

/*
 * Parse "KEY@FRAME[-FRAME],..." into `out` (when not NULL); the count, or -1
 * when the text is malformed. A key name ends at its '@', so names with
 * commas or spaces ("Right Shift") need no quoting inside the list.
 */
long nmo_run_parse_keys(const char *spec, nmo_run_key_t *out)
{
    long count = 0;
    const char *p = spec;
    while (p != NULL && *p != '\0') {
        const char *at = strchr(p, '@');
        if (at == NULL || at == p) {
            return -1;
        }
        nmo_run_key_t entry = {.key = nmo_run_key_code(p, (size_t)(at - p))};
        p = at + 1;
        if (entry.key == 0 || !nmo_run_frame_number(&p, &entry.first)) {
            return -1;
        }
        entry.last = entry.first;
        if (*p == '-') {
            p++;
            if (!nmo_run_frame_number(&p, &entry.last) || entry.last < entry.first) {
                return -1;
            }
        }
        if (*p == ',') {
            p++;
        } else if (*p != '\0') {
            return -1;
        }
        if (out != NULL) {
            out[count] = entry;
        }
        count++;
    }
    return count;
}

/*
 * The registry values of --registry: KEY\NAME=VALUE entries separated by
 * ';', set in `engine` when given. False when an entry is not one.
 */
bool nmo_run_set_registry(nmo_engine_t *engine, const char *text)
{
    while (text != NULL && *text != '\0') {
        const char *end = strchr(text, ';');
        size_t length = end != NULL ? (size_t)(end - text) : strlen(text);
        const char *equals = memchr(text, '=', length);
        const char *slash = NULL;
        for (const char *p = text; equals != NULL && p < equals; p++) {
            if (*p == '\\') {
                slash = p;
            }
        }
        if (slash == NULL || slash == text || slash + 1 == equals) {
            return false;
        }
        if (engine != NULL) {
            char *entry = (char *)malloc(length + 1u);
            if (entry == NULL) {
                return false;
            }
            memcpy(entry, text, length);
            entry[length] = '\0';
            entry[slash - text] = '\0';
            entry[equals - text] = '\0';
            nmo_status_t status = nmo_engine_set_registry_value(
                engine, entry, entry + (slash - text) + 1, entry + (equals - text) + 1);
            free(entry);
            if (status != NMO_OK) {
                return false;
            }
        }
        text = end != NULL ? end + 1 : NULL;
    }
    return true;
}

static char *run_join(const char *folder, const char *name, size_t name_length)
{
    size_t folder_length = folder != NULL ? strlen(folder) : 0u;
    char *joined = (char *)malloc(folder_length + name_length + 2u);
    if (joined == NULL) {
        return NULL;
    }
    size_t n = 0;
    if (folder_length > 0) {
        memcpy(joined, folder, folder_length);
        n = folder_length;
        if (joined[n - 1] != '/') {
            joined[n++] = '/';
        }
    }
    for (size_t i = 0; i < name_length; i++) {
        joined[n++] = name[i] == '\\' ? '/' : name[i];
    }
    joined[n] = '\0';
    return joined;
}

static bool run_file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

static bool run_is_directory(const char *path)
{
    return path != NULL && nmo_platform_is_directory(path);
}

/* ---- Names matched without case ----
 *
 * Windows finds "TEXTURES\\Wood.bmp" in a folder holding "Textures/wood.BMP";
 * a file system that tells case apart (Linux, a browser's memory) needs each
 * part matched against the names its folder holds. The folders' listings are
 * kept until a save or a file block changes the folders.
 */

typedef struct run_listing {
    char *folder;
    char **names;
    size_t count;
} run_listing_t;

struct nmo_run_listing_cache {
    run_listing_t *items;
    size_t count;
};

static void run_listings_clear(nmo_run_files_t *files)
{
    struct nmo_run_listing_cache *cache = files->listings;
    if (cache == NULL) {
        return;
    }
    for (size_t i = 0; i < cache->count; i++) {
        for (size_t n = 0; n < cache->items[i].count; n++) {
            free(cache->items[i].names[n]);
        }
        free(cache->items[i].names);
        free(cache->items[i].folder);
    }
    free(cache->items);
    cache->items = NULL;
    cache->count = 0;
}

/* Keeps a copy of each name but "." and ".." */
static bool run_listing_visit(void *context, const char *name)
{
    run_listing_t *listing = (run_listing_t *)context;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return true;
    }
    size_t length = strlen(name);
    char *copy = (char *)malloc(length + 1u);
    char **grown = copy != NULL ? (char **)realloc(listing->names,
                                                   (listing->count + 1u) * sizeof(*grown))
                                : NULL;
    if (grown == NULL) {
        free(copy);
        return false;
    }
    memcpy(copy, name, length + 1u);
    listing->names = grown;
    listing->names[listing->count++] = copy;
    return true;
}

/* The names in `folder` ("" the current one; none when it cannot be read), listed once */
static const run_listing_t *run_listing_of(nmo_run_files_t *files, const char *folder)
{
    if (files->listings == NULL) {
        files->listings = (struct nmo_run_listing_cache *)calloc(1, sizeof(*files->listings));
        if (files->listings == NULL) {
            return NULL;
        }
    }
    struct nmo_run_listing_cache *cache = files->listings;
    for (size_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->items[i].folder, folder) == 0) {
            return &cache->items[i];
        }
    }
    size_t length = strlen(folder);
    run_listing_t listing = {(char *)malloc(length + 1u), NULL, 0};
    run_listing_t *grown = listing.folder != NULL
                               ? (run_listing_t *)realloc(cache->items,
                                                          (cache->count + 1u) * sizeof(*grown))
                               : NULL;
    if (grown == NULL) {
        free(listing.folder);
        return NULL;
    }
    cache->items = grown;
    memcpy(listing.folder, folder, length + 1u);
    (void)nmo_platform_list_directory(folder, run_listing_visit, &listing);
    cache->items[cache->count] = listing;
    return &cache->items[cache->count++];
}

static int run_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/*
 * `part` (`length` characters, either separator) inside `folder` with each
 * of its names matched without case, preferring one spelled alike; the path
 * when all match and it is a directory (`directory`) or a file, else NULL.
 */
static char *run_match_case(nmo_run_files_t *files, const char *folder, const char *part,
                            size_t length, bool directory)
{
    char *path = run_join(folder, "", 0);
    size_t path_length = path != NULL ? strlen(path) : 0u;
    if (path_length > 1 && path[path_length - 1] == '/') {
        path[path_length - 1] = '\0'; /* listed as the folder itself */
    }
    for (size_t i = 0; path != NULL && i < length;) {
        size_t end = i;
        while (end < length && part[end] != '/' && part[end] != '\\') {
            end++;
        }
        size_t name_length = end - i;
        if (name_length > 0 && !(name_length == 1 && part[i] == '.')) {
            const run_listing_t *listing = run_listing_of(files, path);
            const char *match = NULL;
            for (size_t n = 0; listing != NULL && n < listing->count; n++) {
                const char *name = listing->names[n];
                if (strlen(name) != name_length) {
                    continue;
                }
                bool same = true, exact = true;
                for (size_t c = 0; c < name_length && same; c++) {
                    exact = exact && name[c] == part[i + c];
                    same = run_lower((unsigned char)name[c]) == run_lower((unsigned char)part[i + c]);
                }
                if (same && (match == NULL || exact)) {
                    match = name;
                }
            }
            char *next = match != NULL ? run_join(path, match, strlen(match)) : NULL;
            free(path);
            path = next;
        }
        i = end + 1;
    }
    if (path != NULL && (directory ? !run_is_directory(path) : !run_file_exists(path))) {
        free(path);
        path = NULL;
    }
    return path;
}

/*
 * The engine's file resolver: a name relative to the run file's folder (or
 * --path), with Windows separators and any case; its last part alone as
 * well, for files copied into one folder. An absolute name (the machine the file was made
 * on: "E:\\Game\\Database.tdb") or one leaving the folder is found by its
 * last part only, so nothing outside the folders is read; one that leaves it
 * only by leading ".." parts (Ballance's "..\\Sounds\\", from its Bin folder)
 * is also found by the rest of it inside the folders.
 */
static const char *run_resolve_file(void *user_data, const char *name)
{
    nmo_run_files_t *files = (nmo_run_files_t *)user_data;
    free(files->found);
    files->found = NULL;
    size_t length = strlen(name);
    if (length == 0) {
        return NULL;
    }
    bool outside = name[0] == '/' || name[0] == '\\' || (length > 1 && name[1] == ':');
    for (size_t i = 0; i + 1 < length && !outside; i++) {
        bool starts = i == 0 || name[i - 1] == '/' || name[i - 1] == '\\';
        outside = starts && name[i] == '.' && name[i + 1] == '.';
    }
    const char *base = name;
    for (const char *p = name; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    if (base[0] == '\0' || strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
        return NULL;
    }
    /* The name past its leading ".." parts, when no other part leaves the folder */
    const char *inner = name;
    while (inner[0] == '.' && inner[1] == '.' && (inner[2] == '\\' || inner[2] == '/')) {
        inner += 3;
    }
    bool inner_inside = inner != name && inner[0] != '/' && inner[0] != '\\' &&
                        (inner[0] == '\0' || inner[1] != ':');
    for (size_t i = 0; inner_inside && inner[i] != '\0' && inner[i + 1] != '\0'; i++) {
        bool starts = i == 0 || inner[i - 1] == '/' || inner[i - 1] == '\\';
        inner_inside = !(starts && inner[i] == '.' && inner[i + 1] == '.');
    }
    const char *folders[3] = {files->write, files->path, files->folder};
    for (int f = 0; f < 3; f++) {
        if (f < 2 && folders[f] == NULL) {
            continue;
        }
        for (int whole = outside ? (inner_inside ? 2 : 0) : 1; whole >= 0; whole--) {
            const char *part = whole == 2 ? inner : whole ? name : base;
            if (whole == 1 && outside) {
                continue;
            }
            char *candidate = run_join(folders[f], part, strlen(part));
            if (candidate == NULL || !run_file_exists(candidate)) {
                free(candidate);
                candidate = run_match_case(files, folders[f], part, strlen(part), false);
            }
            if (candidate != NULL) {
                files->found = candidate;
                return candidate;
            }
        }
    }
    return NULL;
}

/*
 * The engine's file writer: a saved file goes into the --write folder under
 * its name's last part (where the resolver finds it first), never next to the
 * run file; without --write nothing is written (a window run has a temporary
 * folder instead, whose files it remembers to remove).
 */
static nmo_status_t run_write_file(void *user_data, const char *name, const void *data,
                                   size_t size)
{
    nmo_run_files_t *files = (nmo_run_files_t *)user_data;
    if (files->write == NULL) {
        return NMO_ERR_NOT_SUPPORTED;
    }
    const char *base = name;
    for (const char *p = name; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    if (base[0] == '\0' || strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    run_listings_clear(files);
    char *path = run_join(files->write, base, strlen(base));
    FILE *file = path != NULL ? fopen(path, "wb") : NULL;
    if (file == NULL) {
        free(path);
        return NMO_ERR_CANT_OPEN_FILE;
    }
    bool written = size == 0 || fwrite(data, 1, size, file) == size;
    bool closed = fclose(file) == 0;
    bool listed = false;
    for (size_t i = 0; files->temporary != NULL && i < files->written_count && !listed; i++) {
        listed = strcmp(files->written[i], path) == 0;
    }
    char **grown = files->temporary != NULL && !listed
                       ? (char **)realloc(files->written,
                                          (files->written_count + 1u) * sizeof(*grown))
                       : NULL;
    if (grown != NULL) {
        files->written = grown;
        files->written[files->written_count++] = path;
        path = NULL;
    }
    free(path);
    return written && closed ? NMO_OK : NMO_ERR_CANT_WRITE_FILE;
}

/* ---- The file blocks' file system (TT_CopyFile, TT_ListDir, ...) ----
 *
 * The blocks read where the resolver reads (--write, --path, the run file's
 * folder) and change files only inside --write: a name that stays inside it
 * keeps its path there, any other goes there by its last part, as the writer
 * puts saved files. Without --write they change nothing. A window run's
 * temporary folder loses what they made there at the end.
 */

/* An absolute name, or one with a ".." part */
static bool run_name_leaves(const char *name)
{
    size_t length = strlen(name);
    if (length > 0 && (name[0] == '/' || name[0] == '\\' || (length > 1 && name[1] == ':'))) {
        return true;
    }
    for (size_t i = 0; i + 1 < length; i++) {
        bool starts = i == 0 || name[i - 1] == '/' || name[i - 1] == '\\';
        if (starts && name[i] == '.' && name[i + 1] == '.' &&
            (name[i + 2] == '\0' || name[i + 2] == '/' || name[i + 2] == '\\')) {
            return true;
        }
    }
    return false;
}

static const char *run_last_part(const char *name)
{
    const char *base = name;
    for (const char *p = name; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return base;
}

/* What of `name` a folder holds: its path when it stays inside, else its last part; NULL for none */
static const char *run_place_part(const char *name, size_t *out_length)
{
    const char *part = run_name_leaves(name) ? run_last_part(name) : name;
    if (part[0] == '\0' || strcmp(part, ".") == 0 || strcmp(part, "..") == 0) {
        return NULL;
    }
    size_t length = strlen(part);
    while (length > 0 && (part[length - 1] == '/' || part[length - 1] == '\\')) {
        length--; /* "Save\\" names the folder Save */
    }
    *out_length = length;
    return length > 0 ? part : NULL;
}

/* `name` in `folder`: at its path when it stays inside, else by its last part; NULL for none */
static char *run_place(const char *folder, const char *name)
{
    size_t length = 0;
    const char *part = run_place_part(name, &length);
    return part != NULL ? run_join(folder, part, length) : NULL;
}

/* The folder a name means (any case), in the first of the folders the resolver reads that has it */
static char *run_find_directory(nmo_run_files_t *files, const char *name)
{
    const char *folders[3] = {files->write, files->path, files->folder};
    for (int f = 0; f < 3; f++) {
        if (folders[f] == NULL) {
            continue;
        }
        char *path = name[0] != '\0' ? run_place(folders[f], name) : run_join(folders[f], "", 0);
        if (run_is_directory(path)) {
            return path;
        }
        free(path);
        size_t length = 0;
        const char *part = name[0] != '\0' ? run_place_part(name, &length) : NULL;
        path = part != NULL ? run_match_case(files, folders[f], part, length, true) : NULL;
        if (path != NULL) {
            return path;
        }
    }
    return NULL;
}

/* A path inside --write to remember: a window run's temporary folder removes it at the end */
static void run_remember(char ***list, size_t *count, const nmo_run_files_t *files, const char *path)
{
    if (files->temporary == NULL) {
        return;
    }
    for (size_t i = 0; i < *count; i++) {
        if (strcmp((*list)[i], path) == 0) {
            return;
        }
    }
    size_t length = strlen(path);
    char *copy = (char *)malloc(length + 1u);
    char **grown = copy != NULL ? (char **)realloc(*list, (*count + 1u) * sizeof(*grown)) : NULL;
    if (grown == NULL) {
        free(copy);
        return;
    }
    memcpy(copy, path, length + 1u);
    *list = grown;
    (*list)[(*count)++] = copy;
}

static bool run_fs_exists(void *user_data, const char *name)
{
    nmo_run_files_t *files = (nmo_run_files_t *)user_data;
    if (run_resolve_file(files, name) != NULL) {
        return true;
    }
    char *directory = run_find_directory(files, name);
    free(directory);
    return directory != NULL;
}

static bool run_fs_copy(void *user_data, const char *source, const char *destination, bool overwrite)
{
    nmo_run_files_t *files = (nmo_run_files_t *)user_data;
    run_listings_clear(files);
    const char *found = run_resolve_file(files, source);
    char *from_path = found != NULL ? run_join(NULL, found, strlen(found)) : NULL;
    char *to_path = files->write != NULL ? run_place(files->write, destination) : NULL;
    bool done = false;
    if (from_path != NULL && to_path != NULL && strcmp(from_path, to_path) != 0 &&
        (overwrite || !run_file_exists(to_path)) && !run_is_directory(to_path)) {
        FILE *in = fopen(from_path, "rb");
        FILE *out = in != NULL ? fopen(to_path, "wb") : NULL;
        done = out != NULL;
        char buffer[4096];
        size_t n;
        while (done && in != NULL && (n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
            done = fwrite(buffer, 1, n, out) == n;
        }
        done = done && !ferror(in);
        if (out != NULL) {
            done = fclose(out) == 0 && done;
            run_remember(&files->written, &files->written_count, files, to_path);
        }
        if (in != NULL) {
            fclose(in);
        }
    }
    free(from_path);
    free(to_path);
    return done;
}

static bool run_fs_remove_file(void *user_data, const char *name)
{
    nmo_run_files_t *files = (nmo_run_files_t *)user_data;
    run_listings_clear(files);
    char *path = files->write != NULL ? run_place(files->write, name) : NULL;
    bool done = path != NULL && nmo_platform_remove(path) == 0;
    free(path);
    return done;
}

static bool run_fs_create_directory(void *user_data, const char *name)
{
    nmo_run_files_t *files = (nmo_run_files_t *)user_data;
    run_listings_clear(files);
    char *path = files->write != NULL ? run_place(files->write, name) : NULL;
    bool done = path != NULL && nmo_platform_make_directory(path) == 0;
    if (done) {
        run_remember(&files->directories, &files->directory_count, files, path);
    }
    free(path);
    return done;
}

static bool run_fs_remove_directory(void *user_data, const char *name)
{
    nmo_run_files_t *files = (nmo_run_files_t *)user_data;
    run_listings_clear(files);
    char *path = files->write != NULL ? run_place(files->write, name) : NULL;
    bool done = path != NULL && nmo_platform_remove_directory(path) == 0;
    free(path);
    return done;
}

/* A folder the resolver can read becomes the current directory, reported by the name given */
static bool run_fs_set_current_directory(void *user_data, const char *name)
{
    nmo_run_files_t *files = (nmo_run_files_t *)user_data;
    char *directory = run_find_directory(files, name);
    free(directory);
    return directory != NULL && nmo_engine_set_current_directory(files->engine, name) == NMO_OK;
}

/* A Windows file mask: * and ?, without case; "*.*" is everything */
static bool run_mask_matches(const char *mask, const char *name)
{
    if (strcmp(mask, "*.*") == 0) {
        return true;
    }
    if (*mask == '\0') {
        return *name == '\0';
    }
    if (*mask == '*') {
        for (const char *rest = name;; rest++) {
            if (run_mask_matches(mask + 1, rest)) {
                return true;
            }
            if (*rest == '\0') {
                return false;
            }
        }
    }
    if (*name == '\0') {
        return false;
    }
    if (*mask == '?' || run_lower((unsigned char)*mask) == run_lower((unsigned char)*name)) {
        return run_mask_matches(mask + 1, name + 1);
    }
    return false;
}

static int run_compare_names(const void *a, const void *b)
{
    const char *x = *(const char *const *)a, *y = *(const char *const *)b;
    for (; *x != '\0' && run_lower((unsigned char)*x) == run_lower((unsigned char)*y); x++, y++) {
    }
    return run_lower((unsigned char)*x) - run_lower((unsigned char)*y);
}

typedef struct run_mask_listing {
    const char *mask;
    char **names;
    size_t count;
    bool ok;
} run_mask_listing_t;

/* Keeps a copy of each name the mask matches */
static bool run_list_visit(void *context, const char *name)
{
    run_mask_listing_t *listing = (run_mask_listing_t *)context;
    if (!run_mask_matches(listing->mask, name)) {
        return true;
    }
    size_t length = strlen(name);
    char *copy = (char *)malloc(length + 1u);
    char **grown = copy != NULL ? (char **)realloc(listing->names, (listing->count + 1u) * sizeof(*grown)) : NULL;
    if (grown == NULL) {
        free(copy);
        listing->ok = false;
        return false;
    }
    memcpy(copy, name, length + 1u);
    listing->names = grown;
    listing->names[listing->count++] = copy;
    return true;
}

/* FindFirstFile/FindNextFile over the folder of the pattern, in name order as NTFS gives them */
static bool run_fs_list(void *user_data, const char *pattern, void (*found)(void *, const char *),
                        void *context)
{
    nmo_run_files_t *files = (nmo_run_files_t *)user_data;
    const char *mask = run_last_part(pattern);
    size_t folder_length = (size_t)(mask - pattern);
    char *folder_name = (char *)malloc(folder_length + 1u);
    if (folder_name == NULL) {
        return false;
    }
    memcpy(folder_name, pattern, folder_length);
    folder_name[folder_length] = '\0';
    char *folder = run_find_directory(files, folder_name);
    free(folder_name);
    if (folder == NULL || mask[0] == '\0') {
        free(folder);
        return false;
    }
    run_mask_listing_t listing = {mask, NULL, 0, true};
    (void)nmo_platform_list_directory(folder, run_list_visit, &listing); /* a folder it cannot read lists nothing */
    free(folder);
    if (listing.count > 1) {
        qsort(listing.names, listing.count, sizeof(*listing.names), run_compare_names);
    }
    for (size_t i = 0; i < listing.count; i++) {
        if (listing.ok) {
            found(context, listing.names[i]);
        }
        free(listing.names[i]);
    }
    free(listing.names);
    return listing.ok;
}

/* A new empty folder for a window run's saves; NULL when none could be made */
static char *run_make_temporary_folder(void)
{
    return nmo_platform_make_temporary_directory("nmo-run-");
}

/* What nmo_run_files_t holds, with the temporary folder and the files saved in it removed */
void nmo_run_files_release(nmo_run_files_t *files)
{
    for (size_t i = 0; i < files->written_count; i++) {
        (void)nmo_platform_remove(files->written[i]);
        free(files->written[i]);
    }
    free(files->written);
    for (size_t i = files->directory_count; i > 0; i--) {
        (void)nmo_platform_remove_directory(files->directories[i - 1]);
        free(files->directories[i - 1]);
    }
    free(files->directories);
    if (files->temporary != NULL) {
        (void)nmo_platform_remove_directory(files->temporary);
        free(files->temporary);
    }
    run_listings_clear(files);
    free(files->listings);
    free(files->folder);
    free(files->found);
    memset(files, 0, sizeof(*files));
}

/* The folder of `file`, "" for the current one. */
static char *run_folder_of(const char *file)
{
    const char *slash = file != NULL ? strrchr(file, '/') : NULL;
    size_t length = slash != NULL ? (size_t)(slash - file) : 0u;
    char *folder = (char *)malloc(length + 1u);
    if (folder != NULL) {
        memcpy(folder, file, length);
        folder[length] = '\0';
    }
    return folder;
}

/*
 * The path manager's paths a game player adds (GamePlayer.cpp): the
 * document's folder for data, sounds and bitmaps, and its Sounds and
 * Textures folders.
 */
bool nmo_run_add_paths(nmo_engine_t *engine)
{
    static const struct {
        int32_t category; /* 0 bitmaps, 1 data, 2 sounds */
        const char *path;
    } paths[] = {{1, "."}, {2, "."}, {2, "Sounds"}, {0, "."}, {0, "Textures"}};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        if (nmo_engine_add_path(engine, paths[i].category, paths[i].path) != NMO_OK) {
            return false;
        }
    }
    return true;
}

bool nmo_run_files_init(nmo_run_files_t *files, const char *file, const char *path,
                        const char *write, bool temporary)
{
    memset(files, 0, sizeof(*files));
    files->write = write;
    files->path = path;
    files->folder = run_folder_of(file);
    if (files->folder == NULL) {
        return false;
    }
    if (write == NULL && temporary) {
        files->temporary = run_make_temporary_folder();
        files->write = files->temporary;
    }
    return true;
}

const char *nmo_run_files_resolve(nmo_run_files_t *files, const char *name)
{
    return files != NULL && name != NULL ? run_resolve_file(files, name) : NULL;
}

nmo_status_t nmo_run_files_write(nmo_run_files_t *files, const char *name, const void *data,
                                 size_t size)
{
    return files != NULL && name != NULL ? run_write_file(files, name, data, size)
                                         : NMO_ERR_INVALID_ARGUMENT;
}

void nmo_run_files_attach(nmo_run_files_t *files, nmo_engine_t *engine)
{
    files->engine = engine;
    nmo_engine_set_file_resolver(engine, run_resolve_file, files);
    nmo_engine_set_file_writer(engine, run_write_file, files);
    const nmo_engine_file_system_t file_system = {
        files, run_fs_exists, run_fs_copy, run_fs_remove_file, run_fs_create_directory,
        run_fs_remove_directory, run_fs_set_current_directory, run_fs_list};
    nmo_engine_set_file_system(engine, &file_system);
}

void nmo_run_apply_keys(nmo_engine_t *engine, const nmo_run_key_t *keys, long count,
                        uint64_t frame)
{
    /* A key is down in a frame one of its ranges covers */
    for (long k = 0; k < count; k++) {
        bool down = false;
        for (long j = 0; j < count; j++) {
            down = down || (keys[j].key == keys[k].key && frame >= keys[j].first &&
                            frame <= keys[j].last);
        }
        nmo_engine_set_key(engine, keys[k].key, down);
    }
}
