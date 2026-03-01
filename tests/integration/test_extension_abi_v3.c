#include "test_framework.h"
#include "extension/nmo_extension_abi.h"

TEST(extension_abi_v3, version_constant) {
    ASSERT_EQ(3u, NMO_EXTENSION_ABI_VERSION);
}

TEST(extension_abi_v3, manager_descriptor_has_event_hook) {
    nmo_extension_manager_desc_t desc = {0};
    ASSERT_TRUE(desc.on_event == NULL);
}

TEST_MAIN_BEGIN()
REGISTER_TEST(extension_abi_v3, version_constant);
REGISTER_TEST(extension_abi_v3, manager_descriptor_has_event_hook);
TEST_MAIN_END()
