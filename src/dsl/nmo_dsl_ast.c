#include "dsl/nmo_dsl_ast.h"
#include <string.h>

nmo_dsl_expr_t *nmo_dsl_expr_alloc(nmo_arena_t *arena, nmo_dsl_expr_kind_t kind) {
    nmo_dsl_expr_t *e = (nmo_dsl_expr_t *)nmo_arena_alloc(arena, sizeof(*e), sizeof(void *));
    if (!e) return NULL;
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    return e;
}

nmo_dsl_stmt_t *nmo_dsl_stmt_alloc(nmo_arena_t *arena, nmo_dsl_stmt_kind_t kind) {
    nmo_dsl_stmt_t *s = (nmo_dsl_stmt_t *)nmo_arena_alloc(arena, sizeof(*s), sizeof(void *));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));
    s->kind = kind;
    return s;
}
