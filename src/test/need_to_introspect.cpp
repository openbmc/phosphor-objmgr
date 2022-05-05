#include "src/processing.hpp"

#include <gtest/gtest.h>

// Verify if name is empty, false is returned
TEST(NeedToIntrospect, PassEmptyName)
{
    AllowDenyList allowList;
    AllowDenyList denyList;
    std::string process_name;

    EXPECT_FALSE(needToIntrospect(process_name, allowList, denyList));
}

// Verify if name is on allowlist, true is returned
TEST(NeedToIntrospect, ValidAllowListName)
{
    AllowDenyList allowList = {"xyz.openbmc_project"};
    AllowDenyList denyList;
    std::string process_name = "xyz.openbmc_project.State.Host";

    EXPECT_TRUE(needToIntrospect(process_name, allowList, denyList));
}

// Verify if name is on denylist, false is returned
TEST(NeedToIntrospect, ValidDenyListName)
{
    AllowDenyList allowList;
    AllowDenyList denyList = {"xyz.openbmc_project.State.Host"};
    std::string process_name = "xyz.openbmc_project.State.Host";

    EXPECT_FALSE(needToIntrospect(process_name, allowList, denyList));
}

// Verify if name is on allowlist and denylist, false is returned
TEST(NeedToIntrospect, ValidAllowAndDenyListName)
{
    AllowDenyList allowList = {"xyz.openbmc_project"};
    AllowDenyList denyList = {"xyz.openbmc_project.State.Host"};
    std::string process_name = "xyz.openbmc_project.State.Host";

    EXPECT_FALSE(needToIntrospect(process_name, allowList, denyList));
}
