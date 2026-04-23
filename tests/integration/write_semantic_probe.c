#include "write_semantic_probe.h"

#include "document/nmo_document_load.h"
#include "test_framework.h"
#include "type/nmo_type_query.h"

#include <string.h>

nmo_status_t write_probe_open(write_semantic_probe_t *probe, const char *path) {
    if (!probe || !path) return NMO_ERR_INVALID_ARGUMENT;
    memset(probe, 0, sizeof(*probe));

    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;
    probe->ctx = nmo_context_create(&desc);
    if (!probe->ctx) return NMO_ERR_NOMEM;

    probe->session = nmo_session_create(probe->ctx);
    if (!probe->session) {
        nmo_context_release(probe->ctx);
        probe->ctx = NULL;
        return NMO_ERR_NOMEM;
    }

    nmo_status_t status = nmo_load_file(probe->session, path, NULL);
    if (status != NMO_OK) {
        write_probe_close(probe);
        return status;
    }
    (void)nmo_session_ensure_behavior_acceleration(probe->session);

    return NMO_OK;
}

void write_probe_close(write_semantic_probe_t *probe) {
    if (!probe) return;
    if (probe->session) {
        nmo_session_destroy(probe->session);
    }
    if (probe->ctx) {
        nmo_context_release(probe->ctx);
    }
    memset(probe, 0, sizeof(*probe));
}

nmo_object_repository_t *write_probe_repo(const write_semantic_probe_t *probe) {
    if (!probe || !probe->session) return NULL;
    return nmo_session_get_repository(probe->session);
}

size_t write_probe_object_count(const write_semantic_probe_t *probe) {
    nmo_object_repository_t *repo = write_probe_repo(probe);
    return repo ? nmo_object_repository_get_count(repo) : 0u;
}

nmo_object_t *write_probe_object_by_id(const write_semantic_probe_t *probe, nmo_object_id_t id) {
    nmo_object_repository_t *repo = write_probe_repo(probe);
    return repo ? nmo_object_repository_find_by_id(repo, id) : NULL;
}

nmo_object_t *write_probe_object_by_name(const write_semantic_probe_t *probe, const char *name) {
    nmo_object_repository_t *repo = write_probe_repo(probe);
    return repo ? nmo_object_repository_find_by_name(repo, name) : NULL;
}

size_t write_probe_count_class_name(const write_semantic_probe_t *probe,
                                    nmo_class_id_t class_id,
                                    const char *name) {
    nmo_object_repository_t *repo = write_probe_repo(probe);
    if (!repo) return 0u;

    size_t count = 0u;
    size_t object_count = 0u;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &object_count);
    for (size_t i = 0; objects && i < object_count; ++i) {
        const nmo_object_t *obj = objects[i];
        if (!obj || obj->class_id != class_id) continue;
        if (name && (!obj->name || strcmp(obj->name, name) != 0)) continue;
        ++count;
    }
    return count;
}

void *write_probe_state(const write_semantic_probe_t *probe,
                        nmo_object_id_t id,
                        nmo_guid_t guid) {
    if (!probe || !probe->ctx) return NULL;
    nmo_object_t *obj = write_probe_object_by_id(probe, id);
    if (!obj) return NULL;
    return nmo_type_query_object_get_ancestor_state_by_guid(
        nmo_context_get_type_registry(probe->ctx), obj, guid);
}

uint32_t write_probe_included_count(const write_semantic_probe_t *probe) {
    if (!probe || !probe->session) return 0u;
    uint32_t count = 0u;
    (void)nmo_session_get_included_files(probe->session, &count);
    return count;
}

const nmo_included_file_t *write_probe_included_by_index(const write_semantic_probe_t *probe,
                                                         uint32_t index) {
    if (!probe || !probe->session) return NULL;
    uint32_t count = 0u;
    nmo_included_file_t *files = nmo_session_get_included_files(probe->session, &count);
    if (!files || index >= count) return NULL;
    return &files[index];
}

const nmo_included_file_t *write_probe_included_by_name(const write_semantic_probe_t *probe,
                                                        const char *name) {
    if (!probe || !probe->session || !name) return NULL;
    uint32_t count = 0u;
    nmo_included_file_t *files = nmo_session_get_included_files(probe->session, &count);
    for (uint32_t i = 0; files && i < count; ++i) {
        if (files[i].name && strcmp(files[i].name, name) == 0) return &files[i];
    }
    return NULL;
}

const nmo_parameter_state_t *write_probe_parameter_state(const write_semantic_probe_t *probe,
                                                        nmo_object_id_t id) {
    nmo_object_t *obj = write_probe_object_by_id(probe, id);
    return obj ? nmo_parameter_get_state(obj) : NULL;
}

nmo_status_t write_probe_parameter_value(const write_semantic_probe_t *probe,
                                         nmo_object_id_t id,
                                         char *out_buf,
                                         size_t buf_size) {
    if (!probe || !probe->ctx || !out_buf || buf_size == 0u) return NMO_ERR_INVALID_ARGUMENT;
    nmo_object_t *obj = write_probe_object_by_id(probe, id);
    if (!obj) return NMO_ERR_NOT_FOUND;
    return nmo_parameter_get_value(
        obj, nmo_context_get_type_registry(probe->ctx), out_buf, buf_size);
}
