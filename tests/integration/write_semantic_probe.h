#ifndef WRITE_SEMANTIC_PROBE_H
#define WRITE_SEMANTIC_PROBE_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "type/nmo_type_system.h"

#include <stddef.h>
#include <stdint.h>

typedef struct write_semantic_probe {
    nmo_context_t *ctx;
    nmo_session_t *session;
} write_semantic_probe_t;

nmo_status_t write_probe_open(write_semantic_probe_t *probe, const char *path);
void write_probe_close(write_semantic_probe_t *probe);

nmo_object_repository_t *write_probe_repo(const write_semantic_probe_t *probe);
size_t write_probe_object_count(const write_semantic_probe_t *probe);
nmo_object_t *write_probe_object_by_id(const write_semantic_probe_t *probe, nmo_object_id_t id);
nmo_object_t *write_probe_object_by_name(const write_semantic_probe_t *probe, const char *name);
size_t write_probe_count_class_name(const write_semantic_probe_t *probe,
                                    nmo_class_id_t class_id,
                                    const char *name);

void *write_probe_state(const write_semantic_probe_t *probe,
                        nmo_object_id_t id,
                        nmo_guid_t guid);

uint32_t write_probe_included_count(const write_semantic_probe_t *probe);
const nmo_included_file_t *write_probe_included_by_index(const write_semantic_probe_t *probe,
                                                         uint32_t index);
const nmo_included_file_t *write_probe_included_by_name(const write_semantic_probe_t *probe,
                                                        const char *name);

const nmo_parameter_state_t *write_probe_parameter_state(const write_semantic_probe_t *probe,
                                                        nmo_object_id_t id);
nmo_status_t write_probe_parameter_value(const write_semantic_probe_t *probe,
                                         nmo_object_id_t id,
                                         char *out_buf,
                                         size_t buf_size);

#endif /* WRITE_SEMANTIC_PROBE_H */
