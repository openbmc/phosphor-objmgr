#include "src/processing.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::ElementsAre;
using ::testing::IsEmpty;

// Verify if name does not start with a : that it is returned
TEST(WellKnownName, NameNotStartColon)
{
    NameOwners owners;
    const std::string request = "test";

    EXPECT_THAT(getWellKnownNames(owners, request), ElementsAre(request));
}

// Verify if name is not found, no names are returned
TEST(WellKnownName, NameNotFound)
{
    NameOwners owners;
    const std::string request = ":test";

    EXPECT_THAT(getWellKnownNames(owners, request), IsEmpty());
}

// Verify if name is found, the correct name is returned
TEST(WellKnownName, NameFound)
{
    NameOwners owners;
    const std::string request = ":1.25";

    owners[request] = {"test"};
    EXPECT_THAT(getWellKnownNames(owners, request), ElementsAre("test"));
}

// Verify that all well known names of a connection are returned
TEST(WellKnownName, MultipleNames)
{
    NameOwners owners;
    const std::string request = ":1.25";

    owners[request] = {"test0", "test1"};
    EXPECT_THAT(getWellKnownNames(owners, request),
                ElementsAre("test0", "test1"));
}
