/**
 * @file test_param_value.c
 * @brief Unit tests for parameter value decoding (nmo_param_value.h)
 */

#include "test_framework.h"
#include "nmo.h"
#include "behavior/nmo_param_value.h"
#include "session/nmo_context.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_guids.h"
#include "object/nmo_object_enum_defs.h"
#include "core/nmo_guid.h"

#include <string.h>
#include <math.h>

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
 * Tests: nmo_param_mode_to_string
 * ============================================================================ */

TEST(param_value, mode_to_string) {
    ASSERT_STR_EQ("buffer",   nmo_param_mode_to_string(CKPARAM_MODE_BUFFER));
    ASSERT_STR_EQ("object",   nmo_param_mode_to_string(CKPARAM_MODE_OBJECT));
    ASSERT_STR_EQ("subchunk", nmo_param_mode_to_string(CKPARAM_MODE_SUBCHUNK));
    ASSERT_STR_EQ("manager",  nmo_param_mode_to_string(CKPARAM_MODE_MANAGER));
    ASSERT_STR_EQ("none",     nmo_param_mode_to_string(CKPARAM_MODE_NONE));

}

/* ============================================================================
 * Tests: nmo_param_value_to_string with various modes
 * ============================================================================ */

TEST(param_value, no_state) {
    nmo_parameter_state_t p;
    memset(&p, 0, sizeof(p));
    p.has_state = false;

    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    char buf[128];
    nmo_status_t st = nmo_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
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
    nmo_status_t st = nmo_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    ASSERT_STR_EQ("(no value)", buf);

    nmo_context_release(ctx);

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
    nmo_status_t st = nmo_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    ASSERT_STR_EQ("#42", buf);

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
    nmo_status_t st = nmo_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
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
    nmo_status_t st = nmo_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
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
    nmo_status_t st = nmo_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
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
    nmo_status_t st = nmo_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    ASSERT_NOT_NULL(strstr(buf, "42"));

    nmo_context_release(ctx);
}

TEST(param_value, decode_bool_true) {
    nmo_context_t *ctx = nmo_context_create(NULL);
    ASSERT_NOT_NULL(ctx);
    nmo_type_registry_t *reg = nmo_context_get_type_registry(ctx);

    int32_t val = 1;  /* Virtools bool is typically int32 */
    nmo_parameter_state_t p = make_buffer_param(CKPGUID_BOOL, &val, sizeof(val));

    char buf[128];
    nmo_status_t st = nmo_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
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
    nmo_status_t st = nmo_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    /* String type may succeed or fall through to hex — either is acceptable */
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
    nmo_status_t st = nmo_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
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
    nmo_status_t st = nmo_param_value_to_string(&p, reg, NULL, buf, sizeof(buf));
    ASSERT_EQ(NMO_OK, st);
    /* Should contain hex bytes */
    ASSERT_NOT_NULL(strstr(buf, "de"));
    ASSERT_NOT_NULL(strstr(buf, "ad"));

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
    nmo_status_t st = nmo_param_value_format_summary(&p, reg, NULL, buf, sizeof(buf));
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
    nmo_status_t st = nmo_param_value_format_summary(&p, reg, NULL, buf, sizeof(buf));
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

    const char *name = nmo_param_value_type_name(&p, reg);
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

    const char *name = nmo_param_value_type_name(&p, reg);
    ASSERT_NULL(name);

    nmo_context_release(ctx);
}

/* ============================================================================
 * Tests: NULL argument handling
 * ============================================================================ */

TEST(param_value, null_args) {
    char buf[64];
    nmo_status_t st;

    st = nmo_param_value_to_string(NULL, NULL, NULL, buf, sizeof(buf));
    ASSERT_NE(NMO_OK, st);

    st = nmo_param_value_format_summary(NULL, NULL, NULL, buf, sizeof(buf));
    ASSERT_NE(NMO_OK, st);

}

/* ============================================================================
 * Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(param_value, mode_to_string);
    REGISTER_TEST(param_value, no_state);
    REGISTER_TEST(param_value, mode_none);
    REGISTER_TEST(param_value, mode_object);
    REGISTER_TEST(param_value, mode_object_null_id);
    REGISTER_TEST(param_value, empty_buffer);
    REGISTER_TEST(param_value, decode_float);
    REGISTER_TEST(param_value, decode_int);
    REGISTER_TEST(param_value, decode_bool_true);
    REGISTER_TEST(param_value, decode_string);
    REGISTER_TEST(param_value, decode_raw_string_buffer);
    REGISTER_TEST(param_value, unknown_type_hex_fallback);
    REGISTER_TEST(param_value, format_summary);
    REGISTER_TEST(param_value, format_summary_unknown_type);
    REGISTER_TEST(param_value, type_name_known);
    REGISTER_TEST(param_value, type_name_unknown);
    REGISTER_TEST(param_value, null_args);
TEST_MAIN_END()
