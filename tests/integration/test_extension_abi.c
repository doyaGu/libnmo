#include "test_framework.h"
#include "extension/nmo_extension_abi.h"
#include "object/nmo_serialize_context.h"

TEST(extension_abi, version_constant) {
    ASSERT_EQ(5u, NMO_EXTENSION_ABI_VERSION);
}

TEST(extension_abi, manager_descriptor_has_event_hook) {
    nmo_extension_manager_desc_t desc = {0};
    ASSERT_TRUE(desc.on_event == NULL);
}

TEST(extension_abi, host_has_register_operations) {
    nmo_extension_host_t host = {0};
    ASSERT_TRUE(host.register_operations == NULL);
}

TEST(extension_abi, operation_desc_zero_init) {
    nmo_extension_operation_desc_t desc = {0};
    ASSERT_EQ(0u, desc.flags);
    ASSERT_EQ(0u, desc.priority);
    ASSERT_TRUE(desc.name == NULL);
}

TEST(extension_abi, serialize_context_initializes_optional_registry) {
    nmo_serialize_context_t plain = nmo_serialize_context_create(NULL, NULL, 0u, 0u);
    nmo_serialize_context_t scratch = nmo_serialize_context_create_with_scratch(
        NULL, NULL, NULL, 0u, 0u);
    nmo_serialize_context_t nonfile = nmo_serialize_context_create_nonfile(NULL, NULL, 0u);
    ASSERT_NULL(plain.type_registry);
    ASSERT_NULL(nmo_serialize_context_get_type_registry(&plain));
    ASSERT_NULL(nmo_serialize_context_get_type_registry(&scratch));
    ASSERT_NULL(nmo_serialize_context_get_type_registry(&nonfile));
    ASSERT_NULL(nmo_serialize_context_get_type_registry(NULL));
}

TEST_MAIN_BEGIN()
REGISTER_TEST(extension_abi, version_constant);
REGISTER_TEST(extension_abi, manager_descriptor_has_event_hook);
REGISTER_TEST(extension_abi, host_has_register_operations);
REGISTER_TEST(extension_abi, operation_desc_zero_init);
REGISTER_TEST(extension_abi, serialize_context_initializes_optional_registry);
TEST_MAIN_END()
