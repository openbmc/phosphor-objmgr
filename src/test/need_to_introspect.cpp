#include "src/processing.hpp"

#include <gtest/gtest.h>

// Verify if name is empty, false is returned
TEST(NeedToIntrospect, PassEmptyName)
{
    AllowDenyList whiteList;
    AllowDenyList blackList;
    std::string process_name;

    EXPECT_FALSE(needToIntrospect(process_name, whiteList, blackList));
}

// Verify if name is on whitelist, true is returned
TEST(NeedToIntrospect, ValidWhiteListName)
{
    AllowDenyList whiteList = {"xyz.openbmc_project"};
    AllowDenyList blackList;
    std::string process_name = "xyz.openbmc_project.State.Host";

    EXPECT_TRUE(needToIntrospect(process_name, whiteList, blackList));
}

// Verify if name is on blacklist, false is returned
TEST(NeedToIntrospect, ValidBlackListName)
{
    AllowDenyList whiteList;
    AllowDenyList blackList = {"xyz.openbmc_project.State.Host"};
    std::string process_name = "xyz.openbmc_project.State.Host";

    EXPECT_FALSE(needToIntrospect(process_name, whiteList, blackList));
}

// Verify if name is on whitelist and blacklist, false is returned
TEST(NeedToIntrospect, ValidWhiteAndBlackListName)
{
    AllowDenyList whiteList = {"xyz.openbmc_project"};
    AllowDenyList blackList = {"xyz.openbmc_project.State.Host"};
    std::string process_name = "xyz.openbmc_project.State.Host";

    EXPECT_FALSE(needToIntrospect(process_name, whiteList, blackList));
}
