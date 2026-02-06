#ifndef NMO_DSL_PARSE_H
#define NMO_DSL_PARSE_H

#include "dsl/nmo_dsl_lex.h"
#include "dsl/nmo_dsl_ast.h"
#include "core/nmo_arena.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    nmo_dsl_lexer_t lx;
    nmo_arena_t *arena;
    bool had_error;
} nmo_dsl_parser_t;

void nmo_dsl_parser_init(nmo_dsl_parser_t *ps, const char *source, nmo_arena_t *arena);

/* Parse a single expression. Returns NULL on error (ps->lx.err has message). */
nmo_dsl_expr_t *nmo_dsl_parse_expression(nmo_dsl_parser_t *ps);

/* Parse a script (semicolon-separated statements). Phase B. */
nmo_dsl_stmt_t *nmo_dsl_parse_script(nmo_dsl_parser_t *ps);

/* Parse schema declarations (enum, flags, struct, alias). Phase C. */
nmo_dsl_stmt_t *nmo_dsl_parse_schema(nmo_dsl_parser_t *ps);

/* Module parse result: schema declarations + script statements */
typedef struct nmo_dsl_module_ast {
    nmo_dsl_stmt_t *schema_decls;
    nmo_dsl_stmt_t *script_stmts;
} nmo_dsl_module_ast_t;

/* Parse a module (schema decls followed by script statements). */
bool nmo_dsl_parse_module(nmo_dsl_parser_t *ps, nmo_dsl_module_ast_t *out_module);

/* Check if parser is at EOF */
static inline bool nmo_dsl_parser_at_eof(const nmo_dsl_parser_t *ps) {
    return ps->lx.tok.kind == NMO_DSL_TOK_EOF;
}

#ifdef __cplusplus
}
#endif

#endif /* NMO_DSL_PARSE_H */
