#include "test_framework.h"
#include "extension/nmo_extension_abi.h"

TEST(extension_abi_v4, version_constant) {
    ASSERT_EQ(4u, NMO_EXTENSION_ABI_VERSION);
}

TEST(extension_abi_v4, manager_descriptor_has_event_hook) {
    nmo_extension_manager_desc_t desc = {0};
    ASSERT_TRUE(desc.on_event == NULL);
}

TEST(extension_abi_v4, host_has_register_operations) {
    nmo_extension_host_t host = {0};
    ASSERT_TRUE(host.register_operations == NULL);
}

TEST(extension_abi_v4, operation_desc_zero_init) {
    nmo_extension_operation_desc_t desc = {0};
    ASSERT_EQ(0u, desc.flags);
    ASSERT_EQ(0u, desc.priority);
    ASSERT_TRUE(desc.name == NULL);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(extension_abi_v4, version_constant);
REGISTER_TEST(extension_abi_v4, manager_descriptor_has_event_hook);
REGISTER_TEST(extension_abi_v4, host_has_register_operations);
REGISTER_TEST(extension_abi_v4, operation_desc_zero_init);
TEST_MAIN_END()
