/**
 * @file nmo_cmd_file.h
 * @brief CLI file command group
 */

#ifndef NMO_CMD_FILE_H
#define NMO_CMD_FILE_H

#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief nmo file info - Show file summary
 */
int nmo_cmd_file_info(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_file_info_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

/**
 * @brief nmo file header - Show file header fields
 */
int nmo_cmd_file_header(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_file_header_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

/**
 * @brief nmo file stats - Show file statistics
 */
int nmo_cmd_file_stats(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_file_stats_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

/**
 * @brief nmo file classes - Show class ID distribution
 */
int nmo_cmd_file_classes(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_file_classes_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

/**
 * @brief nmo file plugins - Show plugin dependencies
 */
int nmo_cmd_file_plugins(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_file_plugins_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

/**
 * @brief nmo file space - Space analysis with per-class and per-object breakdown
 */
int nmo_cmd_file_space(int argc, char **argv, const nmo_cli_global_opts_t *global);
int nmo_cmd_file_space_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CMD_FILE_H */
