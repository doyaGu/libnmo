/**
 * @file nmo_script_index.h
 * @brief What a document's building blocks do with messages, data arrays, and scripts.
 */

#ifndef NMO_SCRIPT_INDEX_H
#define NMO_SCRIPT_INDEX_H

#include "behavior/nmo_script_model.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The script index lists every use a building block makes of a message, a
 * data array, or a script: who sends and waits for each message, which
 * blocks read and write which array (and column), and which blocks activate
 * and deactivate which script. A use's key (the message, array, or script)
 * is resolved from the block's inputs when their value is saved in the
 * file. An array or script the scripts look up by name at run time ("Get
 * Object By Name" or "Convert" of a saved string, straight into the input or
 * into the local it reads) gets that name, and its object when the document
 * holds one of that name and class. Other keys computed at run time stay
 * unknown.
 *
 * The index borrows the model and the document: it is valid while both are.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define NMO_SCRIPT_INDEX_PUBLIC_HEADER_KIND NMO_PUBLIC_HEADER_KIND_SINGLE_TIER
#define NMO_SCRIPT_INDEX_API_TIER NMO_API_TIER_ADVANCED_C

typedef struct nmo_script_index nmo_script_index_t;

typedef enum nmo_script_use_kind {
    NMO_SCRIPT_USE_MESSAGE_SEND = 0,  /* Send Message, Broadcast Message, ... */
    NMO_SCRIPT_USE_MESSAGE_WAIT,      /* Wait Message, Switch On Message */
    NMO_SCRIPT_USE_MESSAGE_OTHER,     /* any other block with a Message input */
    NMO_SCRIPT_USE_ARRAY_READ,        /* Get Cell, Get Row, Iterator, ... */
    NMO_SCRIPT_USE_ARRAY_WRITE,       /* Set Cell, Add Row, Clear Array, ... */
    NMO_SCRIPT_USE_SCRIPT_ACTIVATE,   /* Activate Script, Execute Script */
    NMO_SCRIPT_USE_SCRIPT_DEACTIVATE, /* Deactivate Script */
} nmo_script_use_kind_t;

typedef struct nmo_script_use {
    nmo_script_use_kind_t kind;
    nmo_object_id_t node_id;        /* the building block */
    nmo_object_id_t graph_id;       /* the graph holding it */
    nmo_object_id_t root_id;        /* the root (script) it is in */
    nmo_object_id_t key_param_id;   /* the input naming the key; 0 when none */
    nmo_script_value_kind_t key_value; /* how the key input gets its value */
    const char *message;            /* MESSAGE_*: the message name; NULL unless saved */
    nmo_object_id_t object_id;      /* ARRAY_*: the array; SCRIPT_*: the script; 0 unless known */
    const char *object_name;        /* the name a by-name lookup finds the array or script by */
    int32_t column;                 /* ARRAY_* on a cell or column: its index; -1 otherwise */
    const char *column_name;        /* NULL unless the column is saved and the array names it */
    nmo_object_id_t dest_object_id; /* MESSAGE_SEND: the Dest or Group object; 0 unless saved */
} nmo_script_use_t;

NMO_API nmo_status_t nmo_script_index_build(nmo_workspace_t *workspace,
                                            const nmo_script_model_t *model,
                                            nmo_script_index_t **out_index);
NMO_API void nmo_script_index_destroy(nmo_script_index_t *index);

/** Every use, in model node order. */
NMO_API const nmo_script_use_t *nmo_script_index_uses(const nmo_script_index_t *index,
                                                      size_t *out_count);

/** "send", "wait", "message", "read", "write", "activate", "deactivate". */
NMO_API const char *nmo_script_use_kind_name(nmo_script_use_kind_t kind);
NMO_API bool nmo_script_use_kind_is_message(nmo_script_use_kind_t kind);
NMO_API bool nmo_script_use_kind_is_array(nmo_script_use_kind_t kind);
NMO_API bool nmo_script_use_kind_is_script(nmo_script_use_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif /* NMO_SCRIPT_INDEX_H */
