#ifndef NMO_DSL_AST_H
#define NMO_DSL_AST_H

#include "dsl/nmo_dsl_lex.h"
#include "core/nmo_arena.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Source span for error reporting */
typedef struct {
    uint32_t offset;
    uint32_t length;
    uint32_t line;
    uint32_t col;
} nmo_dsl_span_t;

/* Expression kinds */
typedef enum {
    NMO_DSL_EXPR_LITERAL = 0,
    NMO_DSL_EXPR_IDENT,
    NMO_DSL_EXPR_UNARY,
    NMO_DSL_EXPR_BINARY,
    NMO_DSL_EXPR_CALL,
    NMO_DSL_EXPR_MEMBER,
    NMO_DSL_EXPR_INDEX,
    NMO_DSL_EXPR_WILDCARD,
    NMO_DSL_EXPR_SLICE,
    NMO_DSL_EXPR_FILTER,
} nmo_dsl_expr_kind_t;

/* Forward-declared value kind enum -- mirrors the public API */
typedef enum {
    NMO_DSL_LIT_NULL = 0,
    NMO_DSL_LIT_BOOL,
    NMO_DSL_LIT_INT,
    NMO_DSL_LIT_UINT,
    NMO_DSL_LIT_REAL,
    NMO_DSL_LIT_STRING,
} nmo_dsl_lit_kind_t;

/* Literal representation in AST (arena-owned strings, no malloc) */
typedef struct {
    nmo_dsl_lit_kind_t kind;
    union {
        bool b;
        int64_t i;
        uint64_t u;
        double r;
        const char *s;    /* arena-owned */
    } as;
} nmo_dsl_lit_t;

typedef struct nmo_dsl_expr nmo_dsl_expr_t;

typedef struct {
    nmo_dsl_tok_kind_t op;
    nmo_dsl_expr_t *rhs;
} nmo_dsl_unary_t;

typedef struct {
    nmo_dsl_tok_kind_t op;
    nmo_dsl_expr_t *lhs;
    nmo_dsl_expr_t *rhs;
} nmo_dsl_binary_t;

typedef struct {
    const char *name;         /* arena-owned */
    nmo_dsl_expr_t **args;    /* arena-allocated array */
    size_t arg_count;
} nmo_dsl_call_t;

typedef struct {
    nmo_dsl_expr_t *base;
    const char *field;        /* arena-owned */
} nmo_dsl_member_t;

typedef struct {
    nmo_dsl_expr_t *base;
    nmo_dsl_expr_t *index;
} nmo_dsl_index_t;

typedef struct {
    nmo_dsl_expr_t *base;
    nmo_dsl_expr_t *start;    /* nullable: omitted start */
    nmo_dsl_expr_t *end;      /* nullable: omitted end */
} nmo_dsl_slice_t;

typedef struct {
    nmo_dsl_expr_t *base;
    nmo_dsl_expr_t *pred;
} nmo_dsl_filter_t;

struct nmo_dsl_expr {
    nmo_dsl_expr_kind_t kind;
    nmo_dsl_span_t span;
    union {
        nmo_dsl_lit_t lit;
        const char *ident;        /* arena-owned */
        nmo_dsl_unary_t unary;
        nmo_dsl_binary_t binary;
        nmo_dsl_call_t call;
        nmo_dsl_member_t member;
        nmo_dsl_index_t index;
        nmo_dsl_slice_t slice;
        nmo_dsl_filter_t filter;
        nmo_dsl_expr_t *wild_base;
    } as;
};

/* Statement kinds */
typedef enum {
    NMO_DSL_STMT_EXPR,
    NMO_DSL_STMT_ASSIGN,
    NMO_DSL_STMT_ENUM_DECL,
    NMO_DSL_STMT_FLAGS_DECL,
    NMO_DSL_STMT_STRUCT_DECL,
    NMO_DSL_STMT_ALIAS_DECL,
} nmo_dsl_stmt_kind_t;

/* Schema declaration AST types (Phase C) */
typedef struct {
    const char *name;
    bool has_value;
    int64_t value;
} nmo_dsl_enum_entry_t;

typedef struct {
    const char *name;
    nmo_dsl_enum_entry_t *entries;
    size_t entry_count;
    const char *underlying_type;
} nmo_dsl_enum_decl_t;

typedef struct {
    const char *name;
    bool has_value;
    uint64_t value;
} nmo_dsl_flags_entry_t;

typedef struct {
    const char *name;
    nmo_dsl_flags_entry_t *entries;
    size_t entry_count;
    const char *underlying_type;
} nmo_dsl_flags_decl_t;

typedef struct {
    const char *type_name;
    const char *field_name;
    bool is_repeated;
} nmo_dsl_struct_field_decl_t;

typedef struct {
    const char *name;
    const char *base_name;
    bool is_packed;
    uint32_t alignment;
    nmo_dsl_struct_field_decl_t *fields;
    size_t field_count;
} nmo_dsl_struct_decl_t;

typedef struct {
    const char *name;
    const char *target_name;
} nmo_dsl_alias_decl_t;

typedef struct nmo_dsl_stmt nmo_dsl_stmt_t;

struct nmo_dsl_stmt {
    nmo_dsl_stmt_kind_t kind;
    nmo_dsl_span_t span;
    struct nmo_dsl_stmt *next;
    union {
        nmo_dsl_expr_t *expr;            /* STMT_EXPR */
        struct {                          /* STMT_ASSIGN */
            nmo_dsl_expr_t *target;
            nmo_dsl_expr_t *value;
        } assign;
        nmo_dsl_enum_decl_t enum_decl;   /* STMT_ENUM_DECL */
        nmo_dsl_flags_decl_t flags_decl; /* STMT_FLAGS_DECL */
        nmo_dsl_struct_decl_t struct_decl;/* STMT_STRUCT_DECL */
        nmo_dsl_alias_decl_t alias_decl; /* STMT_ALIAS_DECL */
    } as;
};

/* Arena allocation helpers */
nmo_dsl_expr_t *nmo_dsl_expr_alloc(nmo_arena_t *arena, nmo_dsl_expr_kind_t kind);
nmo_dsl_stmt_t *nmo_dsl_stmt_alloc(nmo_arena_t *arena, nmo_dsl_stmt_kind_t kind);

/* Create a span from current token */
static inline nmo_dsl_span_t nmo_dsl_span_from_token(const nmo_dsl_token_t *tok) {
    nmo_dsl_span_t s;
    s.offset = tok->offset;
    s.length = (uint32_t)tok->len;
    s.line = tok->line;
    s.col = tok->col;
    return s;
}

#ifdef __cplusplus
}
#endif

#endif /* NMO_DSL_AST_H */
