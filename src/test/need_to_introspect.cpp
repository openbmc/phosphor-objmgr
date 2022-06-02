#include "src/processing.hpp"

#include <gtest/gtest.h>

// Verify if name is empty, false is returned
TEST(NeedToIntrospect, PassEmptyName)
{
    AllowDenyList allowList;
    std::string processName;

    EXPECT_FALSE(needToIntrospect(processName, allowList));
}

// Verify if name is on allowlist, true is returned
TEST(NeedToIntrospect, ValidAllowListName)
{
    AllowDenyList allowList = {"xyz.openbmc_project"};
    std::string processName = "xyz.openbmc_project.State.Host";

    EXPECT_TRUE(needToIntrospect(processName, allowList));
}
