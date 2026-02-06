#ifndef NMO_DSL_EVAL_H
#define NMO_DSL_EVAL_H

#include "dsl/nmo_dsl.h"
#include "dsl/nmo_dsl_ast.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const nmo_dsl_eval_context_t *ctx;
    char err[256];
} nmo_dsl_eval_state_t;

bool nmo_dsl_eval_expr_impl(
    nmo_dsl_eval_state_t *ev,
    const nmo_dsl_expr_t *e,
    nmo_dsl_value_t *out);

bool nmo_dsl_eval_stmt_list(
    nmo_dsl_eval_state_t *ev,
    const nmo_dsl_stmt_t *head,
    nmo_dsl_value_t *out_last_value);

#ifdef __cplusplus
}
#endif

#endif /* NMO_DSL_EVAL_H */
