#ifndef NMO_DSL_LEX_H
#define NMO_DSL_LEX_H

#include "core/nmo_arena.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NMO_DSL_TOK_EOF = 0,
    /* Literals */
    NMO_DSL_TOK_IDENT,
    NMO_DSL_TOK_INT,
    NMO_DSL_TOK_HEX_INT,
    NMO_DSL_TOK_REAL,
    NMO_DSL_TOK_STRING,
    /* Expression punctuation */
    NMO_DSL_TOK_LPAREN,
    NMO_DSL_TOK_RPAREN,
    NMO_DSL_TOK_DOT,
    NMO_DSL_TOK_COMMA,
    NMO_DSL_TOK_LBRACK,
    NMO_DSL_TOK_RBRACK,
    NMO_DSL_TOK_COLON,
    NMO_DSL_TOK_QUESTION,
    NMO_DSL_TOK_STAR,
    /* Arithmetic */
    NMO_DSL_TOK_PLUS,
    NMO_DSL_TOK_MINUS,
    NMO_DSL_TOK_SLASH,
    NMO_DSL_TOK_PERCENT,
    /* Comparison/logic */
    NMO_DSL_TOK_BANG,
    NMO_DSL_TOK_LT,
    NMO_DSL_TOK_LE,
    NMO_DSL_TOK_GT,
    NMO_DSL_TOK_GE,
    NMO_DSL_TOK_EQEQ,
    NMO_DSL_TOK_NEQ,
    NMO_DSL_TOK_ANDAND,
    NMO_DSL_TOK_OROR,
    /* Script/schema (Phase B/C) */
    NMO_DSL_TOK_SEMICOLON,
    NMO_DSL_TOK_LBRACE,
    NMO_DSL_TOK_RBRACE,
    NMO_DSL_TOK_EQ,
    /* Keywords */
    NMO_DSL_TOK_KW_TRUE,
    NMO_DSL_TOK_KW_FALSE,
    NMO_DSL_TOK_KW_NULL,
    NMO_DSL_TOK_KW_SCHEMA,
    NMO_DSL_TOK_KW_ENUM,
    NMO_DSL_TOK_KW_FLAGS,
    NMO_DSL_TOK_KW_STRUCT,
    NMO_DSL_TOK_KW_ALIAS,
    NMO_DSL_TOK_KW_PACKED,
    NMO_DSL_TOK_KW_ALIGNED,
    /* Special */
    NMO_DSL_TOK_AT,
    NMO_DSL_TOK_ERROR,
} nmo_dsl_tok_kind_t;

typedef struct {
    nmo_dsl_tok_kind_t kind;
    const char *start;      /* pointer into source */
    size_t len;
    union {
        int64_t i64;        /* INT, HEX_INT */
        double r64;         /* REAL */
        const char *str;    /* STRING -- arena-allocated */
    } val;
    uint32_t line;          /* 1-based */
    uint32_t col;           /* 1-based */
    uint32_t offset;        /* byte offset from source start */
} nmo_dsl_token_t;

typedef struct {
    const char *source;     /* original source (not owned) */
    const char *cur;        /* current position */
    nmo_dsl_token_t tok;    /* current token */
    nmo_arena_t *arena;     /* for string allocations */
    uint32_t line;
    uint32_t col;
    char err[128];          /* error message buffer */
} nmo_dsl_lexer_t;

void nmo_dsl_lexer_init(nmo_dsl_lexer_t *lx, const char *source, nmo_arena_t *arena);
void nmo_dsl_lexer_next(nmo_dsl_lexer_t *lx);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DSL_LEX_H */
