#ifndef NMO_CLI_WRITE_H
#define NMO_CLI_WRITE_H

#include "nmo_cli_common.h"

#include "document/nmo_document_save.h"

#include <stdbool.h>

typedef struct nmo_cmd_ctx nmo_cmd_ctx_t;
typedef struct nmo_session nmo_session_t;

typedef int (*nmo_cli_write_mutate_fn)(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data);

typedef int (*nmo_cli_write_report_fn)(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data);

typedef bool (*nmo_cli_write_should_save_fn)(
    bool dry_run,
    const char *output_path,
    void *user_data);

typedef struct nmo_cli_write_spec {
    const char *command_name;
    bool output_required_unless_dry_run;
    nmo_cli_write_should_save_fn should_save;
} nmo_cli_write_spec_t;

int nmo_cli_run_write_command(
    const char *input_path,
    const char *output_path,
    bool dry_run,
    const nmo_cli_global_opts_t *global,
    const nmo_cli_write_spec_t *spec,
    nmo_cli_write_mutate_fn mutate,
    nmo_cli_write_report_fn report,
    void *user_data);

int nmo_cli_write_init_ctx(
    nmo_cmd_ctx_t *ctx,
    const char *input_path,
    const nmo_cli_global_opts_t *global);

int nmo_cli_save_session(
    nmo_session_t *session,
    const char *output_path,
    const nmo_save_options_t *save_opts);

#endif /* NMO_CLI_WRITE_H */
