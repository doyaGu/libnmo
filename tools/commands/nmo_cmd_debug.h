/**
 * @file nmo_cmd_debug.h
 * @brief CLI debug command group (non-interactive diagnostics)
 */

#ifndef NMO_CMD_DEBUG_H
#define NMO_CMD_DEBUG_H

#include "../nmo_cli_common.h"
#include "../nmo_cmd_ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

int nmo_cmd_debug_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

/**
 * @brief nmo debug load-phases - Show load pipeline phases
 */
int nmo_cmd_debug_load_phases(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_debug_load_phases_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

/**
 * @brief nmo debug chunks - Show chunk parse details
 */
int nmo_cmd_debug_chunks(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_debug_chunks_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

/**
 * @brief nmo debug objects - Show object load details
 */
int nmo_cmd_debug_objects(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_debug_objects_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

/**
 * @brief nmo debug export - Export JSON snapshot for debugging
 */
int nmo_cmd_debug_export(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_debug_export_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_DEBUG_H */
