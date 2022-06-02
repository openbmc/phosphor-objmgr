#include "src/processing.hpp"

#include <gtest/gtest.h>

// Verify if name is empty, false is returned
TEST(NeedToIntrospect, PassEmptyName)
{
    std::string processName;

    EXPECT_FALSE(needToIntrospect(processName));
}

// Verify if name is org, true is returned
TEST(NeedToIntrospect, NameOrg)
{
    std::string processName = "org";

    EXPECT_TRUE(needToIntrospect(processName));
}

// Verify if name is org.freedesktop, false is returned
TEST(NeedToIntrospect, NameOrgFreedesktop)
{
    std::string processName = "org.freedesktop";

    EXPECT_FALSE(needToIntrospect(processName));
}

// Verify if name is org.freedesktop.foo, false is returned
TEST(NeedToIntrospect, nameOrgFreeDesktopFoo)
{
    std::string processName = "org.freedesktop.foo";

    EXPECT_FALSE(needToIntrospect(processName));
}

// Verify if name is org.openbmc, true is returned
TEST(NeedToIntrospect, nameOrgOpenBMC)
{
    std::string processName = "org.openbmc";

    EXPECT_TRUE(needToIntrospect(processName));
}
