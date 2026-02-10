/**
 * @file nmo_dsl.h
 * @brief DSL compiler and evaluator (DSL layer)
 *
 * Provides a modular DSL compiler architecture with:
 * - Compile-once, evaluate-many semantics
 * - Expression mode: read-only queries
 * - Script mode: mutation support
 * - Schema mode: type declarations
 * - Module mode: schema + script in one unit
 */

#ifndef NMO_DSL_H
#define NMO_DSL_H

#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include "core/nmo_error.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nmo_type_registry nmo_type_registry_t;
typedef struct nmo_operation_registry nmo_operation_registry_t;

/* ============================================================================
 * Value Types
 * ============================================================================ */

typedef enum {
    NMO_DSL_VALUE_NULL = 0,
    NMO_DSL_VALUE_BOOL,
    NMO_DSL_VALUE_INT,
    NMO_DSL_VALUE_UINT,
    NMO_DSL_VALUE_REAL,
    NMO_DSL_VALUE_STRING,
    NMO_DSL_VALUE_BYREF,
    NMO_DSL_VALUE_OBJECT,
    NMO_DSL_VALUE_SEQ,
    NMO_DSL_VALUE_TYPE,    /* type handle: GUID + descriptor */
} nmo_dsl_value_kind_t;

struct nmo_dsl_seq;

typedef struct nmo_dsl_value {
    nmo_dsl_value_kind_t kind;
    union {
        bool b;
        int64_t i;
        uint64_t u;
        double r;
        const char *s;                          /* owned (malloc'd) */
        struct {                                 /* non-owning view */
            nmo_guid_t guid;
            const nmo_type_descriptor_t *type;
            const void *ptr;
            size_t size;
        } byref;
        struct {                                 /* non-owning view */
            const nmo_type_descriptor_t *type;
            const void *instance;
        } object;
        struct nmo_dsl_seq *seq;                 /* owned */
        struct {                                 /* reserved Phase D */
            nmo_guid_t guid;
            const nmo_type_descriptor_t *type;
        } type_handle;
    } as;
} nmo_dsl_value_t;

/* ============================================================================
 * Sequence Abstraction
 * ============================================================================ */

typedef struct nmo_dsl_seq_vtable {
    uint64_t (*count)(const struct nmo_dsl_seq *seq);
    bool     (*get)(const struct nmo_dsl_seq *seq, uint64_t index, nmo_dsl_value_t *out);
    void     (*destroy)(struct nmo_dsl_seq *seq);
} nmo_dsl_seq_vtable_t;

typedef struct nmo_dsl_seq {
    const nmo_dsl_seq_vtable_t *vt;
} nmo_dsl_seq_t;

static inline uint64_t nmo_dsl_seq_count(const nmo_dsl_seq_t *seq) {
    return (seq && seq->vt && seq->vt->count) ? seq->vt->count(seq) : 0;
}

static inline bool nmo_dsl_seq_get(const nmo_dsl_seq_t *seq, uint64_t index, nmo_dsl_value_t *out_value) {
    return (seq && seq->vt && seq->vt->get) ? seq->vt->get(seq, index, out_value) : false;
}

static inline void nmo_dsl_seq_destroy(nmo_dsl_seq_t *seq) {
    if (seq && seq->vt && seq->vt->destroy) {
        seq->vt->destroy(seq);
    }
}

/* ============================================================================
 * Mode and Options
 * ============================================================================ */

typedef enum {
    NMO_DSL_MODE_EXPRESSION = 1,
    NMO_DSL_MODE_SCHEMA    = 2,  /* Phase C */
    NMO_DSL_MODE_SCRIPT    = 3,  /* Phase B */
    NMO_DSL_MODE_MODULE    = 4,  /* schema + script */
} nmo_dsl_mode_t;

typedef struct nmo_dsl_compile_options {
    nmo_dsl_mode_t mode;
    uint32_t flags; /* reserved for future use */
} nmo_dsl_compile_options_t;

/* Schema application options (Phase C) */
typedef struct nmo_dsl_schema_options {
    /* Optional GUID provider for schema declarations */
    nmo_guid_t (*generate_guid)(const char *type_name, void *user);
    void *generate_guid_user;
    /* Allow re-declaring existing types by name (skip if exists) */
    bool allow_redeclare;
    /* Allow alias name to already exist if it matches target type */
    bool allow_alias_existing;
} nmo_dsl_schema_options_t;

/* ============================================================================
 * Evaluation Context
 * ============================================================================ */

typedef struct nmo_dsl_eval_context {
    const nmo_type_registry_t *registry;
    nmo_operation_registry_t *ops;          /* nullable, Phase E */

    const nmo_type_descriptor_t *root_type;
    void *root_instance;              /* mutable for script mode */

    const nmo_type_descriptor_t *current_type;
    const void *current_instance;

    uint64_t (*guess_array_count)(
        const nmo_type_descriptor_t *owner_type,
        const void *owner_instance,
        const nmo_type_field_t *field,
        void *user);
    void *guess_array_count_user;

    const char *(*resolve_object_name)(uint32_t id, void *user);
    void *resolve_object_name_user;

    /* Phase B mutation hooks (unused in Phase A) */
    void *(*alloc)(size_t size, size_t alignment, void *user);
    void (*dealloc)(void *ptr, void *user);
    void *alloc_user;
} nmo_dsl_eval_context_t;

/* ============================================================================
 * Program (opaque)
 * ============================================================================ */

typedef struct nmo_dsl_program nmo_dsl_program_t;

/* ============================================================================
 * Public Functions
 * ============================================================================ */

NMO_API nmo_status_t nmo_dsl_compile(
    const nmo_type_registry_t *registry,
    const nmo_operation_registry_t *ops,   /* nullable */
    const char *source,
    const nmo_dsl_compile_options_t *options,
    nmo_dsl_program_t **out_program);

NMO_API void nmo_dsl_program_destroy(nmo_dsl_program_t *program);

NMO_API nmo_status_t nmo_dsl_eval_expr(
    const nmo_dsl_program_t *program,
    const nmo_dsl_eval_context_t *ctx,
    nmo_dsl_value_t *out_value);

NMO_API nmo_status_t nmo_dsl_exec(
    const nmo_dsl_program_t *program,
    const nmo_dsl_eval_context_t *ctx,
    nmo_dsl_value_t *out_last_value);

/* Apply schema (schema or module mode) */
NMO_API nmo_status_t nmo_dsl_apply_schema_ex(
    nmo_type_registry_t *registry,
    const nmo_dsl_program_t *program,
    const nmo_dsl_schema_options_t *options);

NMO_API nmo_status_t nmo_dsl_apply_schema(
    nmo_type_registry_t *registry,
    const nmo_dsl_program_t *program);

/* Run module: apply schema (if any) then execute script (if any) */
NMO_API nmo_status_t nmo_dsl_run_module(
    nmo_type_registry_t *registry,
    const nmo_dsl_program_t *program,
    const nmo_dsl_eval_context_t *ctx,
    const nmo_dsl_schema_options_t *schema_options,
    nmo_dsl_value_t *out_last_value);

NMO_API void nmo_dsl_value_destroy(nmo_dsl_value_t *value);

/* ============================================================================
 * Convenience Wrapper
 * ============================================================================ */

static inline nmo_status_t nmo_dsl_eval_one(
    const nmo_type_registry_t *registry,
    const nmo_dsl_eval_context_t *ctx,
    const char *source,
    nmo_dsl_value_t *out_value)
{
    nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_EXPRESSION };
    nmo_dsl_program_t *prog = NULL;
    nmo_status_t st = nmo_dsl_compile(registry, NULL, source, &opts, &prog);
    if (st != NMO_OK) return st;
    st = nmo_dsl_eval_expr(prog, ctx, out_value);
    nmo_dsl_program_destroy(prog);
    return st;
}

#ifdef __cplusplus
}
#endif

#endif /* NMO_DSL_H */
