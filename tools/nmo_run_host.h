/**
 * @file nmo_run_host.h
 * @brief What a host running a document in the engine gives it, shared by
 *        `nmo behavior run` and the web player: the files the document
 *        loads and saves, registry values, a game player's paths and keys
 *        held down by frame.
 */

#ifndef NMO_RUN_HOST_H
#define NMO_RUN_HOST_H

#include "nmo_engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Where the files a document loads are found: the write folder, the extra
 * path, then the run file's folder; and where the files it saves go: the
 * write folder, or nowhere. A name the folders lack as spelled is found
 * with each of its parts matched without case, as Windows finds it.
 */
typedef struct nmo_run_files {
    const char *write;
    const char *path;
    char *folder;
    char *found; /* the path last given to the engine */
    /* A window run without a write folder saves into a folder of its own, removed at the end */
    char *temporary;
    char **written; /* the files saved there */
    size_t written_count;
    char **directories; /* the folders the file blocks made there */
    size_t directory_count;
    nmo_engine_t *engine;
    struct nmo_run_listing_cache *listings; /* folders listed to match names without case */
} nmo_run_files_t;

/*
 * Start `files` for a run of the document at `file`: loads look in `write`,
 * `path` (either may be NULL) and the document's folder. With `temporary`
 * and no `write`, saves go to a new temporary folder removed at the end.
 * False when out of memory.
 */
bool nmo_run_files_init(nmo_run_files_t *files, const char *file, const char *path,
                        const char *write, bool temporary);
/* Where `name` (a name the document loads) is found, as the engine's resolver finds it; NULL for nowhere */
const char *nmo_run_files_resolve(nmo_run_files_t *files, const char *name);
/* Save `size` bytes of `data` as the document's file `name`, as the engine's writer saves it */
nmo_status_t nmo_run_files_write(nmo_run_files_t *files, const char *name, const void *data,
                                 size_t size);
/* Give `engine` the files' resolver, writer and file system (the file blocks') */
void nmo_run_files_attach(nmo_run_files_t *files, nmo_engine_t *engine);
/* What `files` holds, with the temporary folder and the files saved in it removed */
void nmo_run_files_release(nmo_run_files_t *files);

/*
 * The registry values of "KEY\NAME=VALUE;...", set in `engine` when not
 * NULL. False when an entry is not one.
 */
bool nmo_run_set_registry(nmo_engine_t *engine, const char *text);

/*
 * The path manager's paths a game player adds (GamePlayer.cpp): the
 * document's folder for data, sounds and bitmaps, and its Sounds and
 * Textures folders.
 */
bool nmo_run_add_paths(nmo_engine_t *engine);

/* A key: a name nmo_behavior_keyboard_key_name() spells, or a scan code; 0 for none */
uint32_t nmo_run_key_code(const char *name, size_t length);
/* A frame number at `*text`, which moves past it; false when there is none */
bool nmo_run_frame_number(const char **text, uint32_t *out);

/* A key held down from frame `first` to frame `last`. */
typedef struct nmo_run_key {
    uint32_t key;
    uint32_t first;
    uint32_t last;
} nmo_run_key_t;

/*
 * Parse "KEY@FRAME[-FRAME],..." into `out` (when not NULL); the count, or -1
 * when the text is malformed. A key is a name nmo_behavior_keyboard_key_name()
 * spells, or a scan code.
 */
long nmo_run_parse_keys(const char *spec, nmo_run_key_t *out);
/* Set each of the `count` keys down or up as its ranges cover frame `frame` */
void nmo_run_apply_keys(nmo_engine_t *engine, const nmo_run_key_t *keys, long count,
                        uint64_t frame);

#endif /* NMO_RUN_HOST_H */
