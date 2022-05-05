#include "src/processing.hpp"

#include <gtest/gtest.h>

// Verify if name does not start with a : that it is returned
TEST(WellKnownName, NameNotStartColon)
{
    boost::container::flat_map<std::string, std::string> owners;
    const std::string request = "test";
    std::string wellKnown;

    EXPECT_TRUE(getWellKnown(owners, request, wellKnown));
    EXPECT_EQ(wellKnown, request);
}

// Verify if name is not found, false is returned
TEST(WellKnownName, NameNotFound)
{
    boost::container::flat_map<std::string, std::string> owners;
    const std::string request = ":test";
    std::string wellKnown;

    EXPECT_FALSE(getWellKnown(owners, request, wellKnown));
    EXPECT_TRUE(wellKnown.empty());
}

// Verify if name is found, true is returned and name is correct
TEST(WellKnownName, NameFound)
{
    boost::container::flat_map<std::string, std::string> owners;
    const std::string request = ":1.25";
    std::string wellKnown;

    owners[request] = "test";
    EXPECT_TRUE(getWellKnown(owners, request, wellKnown));
    EXPECT_EQ(wellKnown, "test");
}
