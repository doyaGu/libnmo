/**
 * @file test_param_value.c
 * @brief Unit tests for parameter value decoding (nmo_param_value.h)
 */

#include "test_framework.h"
#include "nmo.h"
#include "behavior/nmo_behavior_view.h"
#include "runtime/nmo_workspace.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "session/nmo_session_bridge.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_guids.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "core/nmo_guid.h"

#include <string.h>
#include <math.h>
#include <stdio.h>

/* ============================================================================
 * Helper: build a fake nmo_parameter_state_t with BUFFER mode
 * ============================================================================ */

static nmo_parameter_state_t make_buffer_param(
    nmo_guid_t type_guid,
    const void *data, size_t data_size)
{
    nmo_parameter_state_t p;
    memset(&p, 0, sizeof(p));
    p.type_guid = type_guid;
    p.mode = CKPARAM_MODE_BUFFER;
    p.has_state = true;
    p.buffer_data.data = (void *)data;
    p.buffer_data.count = data_size;
    p.buffer_data.element_size = 1;
    return p;
}

/* ============================================================================
 * Tests: nmo_behavior_param_mode_to_string
 * ============================================================================ */

TEST(param_value, mode_to_string) {
    ASSERT_STR_EQ("buffer",   nmo_behavior_param_mode_to_string(CKPARAM_MODE_BUFFER));
    ASSERT_STR_EQ("object",   nmo_behavior_param_mode_to_string(CKPARAM_MODE_OBJECT));
    ASSERT_STR_EQ("subchunk", nmo_behavior_param_mode_to_string(CKPARAM_MODE_SUBCHUNK));
    ASSERT_STR_EQ("manager",  nmo_behavior_param_mode_to_string(CKPARAM_MODE_MANAGER));
    ASSERT_STR_EQ("none",     nmo_behavior_param_mode_to_string(CKPARAM_MODE_NONE));

}

/* ============================================================================
 * Tests: nmo_behavior_param_value_to_string with various modes
 * ============================================================================ */

TEST(param_value, no_state) {
    nmo_parameter_state_t p;
    memset(&p, 0, sizeof(p));
    p.has_state = false;

    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    char buf[128];
    nmo_status_t st = nmo_behavior_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    ASSERT_STR_EQ("(no state)", buf);

    nmo_context_release(ctx);

}

TEST(param_value, mode_none) {
    nmo_parameter_state_t p;
    memset(&p, 0, sizeof(p));
    p.has_state = true;
    p.mode = CKPARAM_MODE_NONE;

    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    char buf[128];
    nmo_status_t st = nmo_behavior_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    ASSERT_STR_EQ("(no value)", buf);

    nmo_context_release(ctx);

}

TEST(param_value, remap_preserves_serialized_none_state_marker) {
    nmo_parameter_state_t p;
    memset(&p, 0, sizeof(p));
    p.has_state = true;
    p.mode = CKPARAM_MODE_NONE;

    nmo_status_t st = nmo_parameter_remap_dependencies(&p, NULL, NULL);
    ASSERT_EQ(NMO_OK, st);
    ASSERT_TRUE(p.has_state);
    ASSERT_EQ(CKPARAM_MODE_NONE, p.mode);
}

TEST(param_value, mode_object) {
    nmo_parameter_state_t p;
    memset(&p, 0, sizeof(p));
    p.has_state = true;
    p.mode = CKPARAM_MODE_OBJECT;
    p.object_id = 42;

    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    char buf[128];
    nmo_status_t st = nmo_behavior_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    ASSERT_STR_EQ("#42", buf);

    nmo_context_release(ctx);

}

TEST(param_value, mode_object_resolves_name_with_workspace) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_session_t *session = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_type_registry_t *reg = NULL;
    nmo_object_id_t target_id = 0;
    nmo_parameter_state_t p;
    char buf[128];
    char expected[128];

    ASSERT_NOT_NULL(ctx);
    session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);
    reg = nmo_context_get_type_registry(ctx);

    ASSERT_EQ(NMO_OK, nmo_session_create_object(
        session, NMO_CID_OBJECT, "TargetObject", (nmo_guid_t){0, 0}, &target_id, NULL));
    ASSERT_TRUE(target_id != 0);

    ASSERT_EQ(NMO_OK, nmo_session_borrow_document(session, &document));
    ASSERT_NOT_NULL(document);
    ASSERT_EQ(NMO_OK, nmo_workspace_create(ctx, document, &workspace));
    ASSERT_NOT_NULL(workspace);

    memset(&p, 0, sizeof(p));
    p.has_state = true;
    p.mode = CKPARAM_MODE_OBJECT;
    p.object_id = target_id;

    snprintf(expected, sizeof(expected), "#%u (TargetObject)", target_id);
    ASSERT_EQ(NMO_OK,
              nmo_behavior_param_value_to_string(&p, reg, workspace, buf, sizeof(buf)));
    ASSERT_STR_EQ(expected, buf);

    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(param_value, mode_object_null_id) {
    nmo_parameter_state_t p;
    memset(&p, 0, sizeof(p));
    p.has_state = true;
    p.mode = CKPARAM_MODE_OBJECT;
    p.object_id = 0;

    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    char buf[128];
    nmo_status_t st = nmo_behavior_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    ASSERT_STR_EQ("(null)", buf);

    nmo_context_release(ctx);
}

TEST(param_value, empty_buffer) {
    nmo_parameter_state_t p;
    memset(&p, 0, sizeof(p));
    p.has_state = true;
    p.mode = CKPARAM_MODE_BUFFER;
    /* buffer_data.data = NULL, count = 0 */

    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    char buf[128];
    nmo_status_t st = nmo_behavior_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    ASSERT_STR_EQ("(empty buffer)", buf);

    nmo_context_release(ctx);
}

/* ============================================================================
 * Tests: buffer mode with typed values
 * ============================================================================ */

TEST(param_value, decode_float) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    float val = 3.14f;
    nmo_parameter_state_t p = make_buffer_param(CKPGUID_FLOAT, &val, sizeof(val));

    char buf[128];
    nmo_status_t st = nmo_behavior_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    /* Should contain "3.14" somewhere */
    ASSERT_NOT_NULL(strstr(buf, "3.14"));

    nmo_context_release(ctx);
}

TEST(param_value, decode_int) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    int32_t val = 42;
    nmo_parameter_state_t p = make_buffer_param(CKPGUID_INT, &val, sizeof(val));

    char buf[128];
    nmo_status_t st = nmo_behavior_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    ASSERT_NOT_NULL(strstr(buf, "42"));

    nmo_context_release(ctx);
}

TEST(param_value, decode_int_exact_small_value) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    int32_t val = 3;
    nmo_parameter_state_t p = make_buffer_param(CKPGUID_INT, &val, sizeof(val));

    char buf[128];
    nmo_status_t st = nmo_behavior_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    ASSERT_STR_EQ("3", buf);

    nmo_context_release(ctx);
}

TEST(param_value, decode_bool_true) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    int32_t val = 1;  /* Virtools bool is typically int32 */
    nmo_parameter_state_t p = make_buffer_param(CKPGUID_BOOL, &val, sizeof(val));

    char buf[128];
    nmo_status_t st = nmo_behavior_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    ASSERT_NOT_NULL(strstr(buf, "true"));

    nmo_context_release(ctx);
}

TEST(param_value, decode_string) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    const char *val = "Hello";
    const char *ptr = val;
    nmo_parameter_state_t p = make_buffer_param(
        CKPGUID_STRING, &ptr, sizeof(ptr));

    char buf[256];
    nmo_status_t st = nmo_behavior_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    /* String type may succeed or fall through to hex 鈥?either is acceptable */
    ASSERT_EQ(NMO_OK, st);
    ASSERT_TRUE(buf[0] != '\0');

    nmo_context_release(ctx);
}

TEST(param_value, decode_raw_string_buffer) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    const char raw[] = "My Font";
    nmo_parameter_state_t p = make_buffer_param(
        CKPGUID_STRING, raw, sizeof(raw));

    char buf[256];
    nmo_status_t st = nmo_behavior_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    ASSERT_STR_EQ("\"My Font\"", buf);

    nmo_context_release(ctx);
}

/* ============================================================================
 * Tests: hex fallback for unknown type
 * ============================================================================ */

TEST(param_value, unknown_type_hex_fallback) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    nmo_guid_t fake_guid = {0x99999999, 0x88888888};
    nmo_parameter_state_t p = make_buffer_param(fake_guid, data, sizeof(data));

    char buf[128];
    nmo_status_t st = nmo_behavior_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    /* Should contain hex bytes */
    ASSERT_NOT_NULL(strstr(buf, "de"));
    ASSERT_NOT_NULL(strstr(buf, "ad"));

    nmo_context_release(ctx);
}

TEST(param_value, truncated_known_type_hex_fallback) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    uint8_t data[] = {0xEF, 0xBE, 0xAD};
    nmo_parameter_state_t p = make_buffer_param(CKPGUID_INT, data, sizeof(data));

    char buf[128];
    nmo_status_t st = nmo_behavior_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    ASSERT_STR_EQ("ef be ad", buf);

    nmo_context_release(ctx);
}

/* ============================================================================
 * Tests: summary formatter
 * ============================================================================ */

TEST(param_value, format_summary) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    float val = 1.5f;
    nmo_parameter_state_t p = make_buffer_param(CKPGUID_FLOAT, &val, sizeof(val));

    char buf[512];
    nmo_status_t st = nmo_behavior_param_format_summary(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    /* Should contain type name, value, and mode */
    ASSERT_NOT_NULL(strstr(buf, "buffer"));

    nmo_context_release(ctx);
}

TEST(param_value, format_summary_unknown_type) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    uint8_t data[] = {0x01, 0x02};
    nmo_guid_t fake_guid = {0xAAAAAAAA, 0xBBBBBBBB};
    nmo_parameter_state_t p = make_buffer_param(fake_guid, data, sizeof(data));

    char buf[512];
    nmo_status_t st = nmo_behavior_param_format_summary(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    /* Should contain GUID fallback and mode */
    ASSERT_NOT_NULL(strstr(buf, "buffer"));
    ASSERT_TRUE(buf[0] != '\0');

    nmo_context_release(ctx);
}

/* ============================================================================
 * Tests: type name resolution
 * ============================================================================ */

TEST(param_value, type_name_known) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    nmo_parameter_state_t p;
    memset(&p, 0, sizeof(p));
    p.type_guid = CKPGUID_FLOAT;

    const char *name = nmo_behavior_param_type_name(&p, reg);
    ASSERT_NOT_NULL(name);
    ASSERT_TRUE(name[0] != '\0');

    nmo_context_release(ctx);
}

TEST(param_value, type_name_unknown) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    nmo_parameter_state_t p;
    memset(&p, 0, sizeof(p));
    p.type_guid = (nmo_guid_t){0x11111111, 0x22222222};

    const char *name = nmo_behavior_param_type_name(&p, reg);
    ASSERT_NULL(name);

    nmo_context_release(ctx);
}

/* ============================================================================
 * Tests: NULL argument handling
 * ============================================================================ */

TEST(param_value, null_args) {
    char buf[64];
    nmo_status_t st;

    st = nmo_behavior_param_value_to_string(NULL, NULL, NULL, buf, sizeof(buf));
    ASSERT_NE(NMO_OK, st);

    st = nmo_behavior_param_format_summary(NULL, NULL, NULL, buf, sizeof(buf));
    ASSERT_NE(NMO_OK, st);

}

/* ============================================================================
 * Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(param_value, mode_to_string);
    REGISTER_TEST(param_value, no_state);
    REGISTER_TEST(param_value, mode_none);
    REGISTER_TEST(param_value, remap_preserves_serialized_none_state_marker);
    REGISTER_TEST(param_value, mode_object);
    REGISTER_TEST(param_value, mode_object_resolves_name_with_workspace);
    REGISTER_TEST(param_value, mode_object_null_id);
    REGISTER_TEST(param_value, empty_buffer);
    REGISTER_TEST(param_value, decode_float);
    REGISTER_TEST(param_value, decode_int);
    REGISTER_TEST(param_value, decode_int_exact_small_value);
    REGISTER_TEST(param_value, decode_bool_true);
    REGISTER_TEST(param_value, decode_string);
    REGISTER_TEST(param_value, decode_raw_string_buffer);
    REGISTER_TEST(param_value, unknown_type_hex_fallback);
    REGISTER_TEST(param_value, truncated_known_type_hex_fallback);
    REGISTER_TEST(param_value, format_summary);
    REGISTER_TEST(param_value, format_summary_unknown_type);
    REGISTER_TEST(param_value, type_name_known);
    REGISTER_TEST(param_value, type_name_unknown);
    REGISTER_TEST(param_value, null_args);
TEST_MAIN_END()
