#include "src/introspect.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Optional;

TEST(Introspect, InterfacesAndChildren)
{
    const std::string xml = R"(
<node>
  <interface name="org.freedesktop.DBus.Peer"/>
  <interface name="xyz.openbmc_project.Test"/>
  <node name="child0"/>
  <node name="child1"/>
</node>)";

    EXPECT_THAT(
        parseIntrospectXml("/a/b", xml),
        Optional(AllOf(Field(&IntrospectResult::interfaces,
                             ElementsAre("org.freedesktop.DBus.Peer",
                                         "xyz.openbmc_project.Test")),
                       Field(&IntrospectResult::childPaths,
                             ElementsAre("/a/b/child0", "/a/b/child1")))));
}

TEST(Introspect, RootChildren)
{
    EXPECT_THAT(parseIntrospectXml("/", R"(<node><node name="xyz"/></node>)"),
                Optional(AllOf(Field(&IntrospectResult::interfaces, IsEmpty()),
                               Field(&IntrospectResult::childPaths,
                                     ElementsAre("/xyz")))));
}

// Elements without a name attribute must be skipped. This used to trigger an
// infinite loop for interface elements.
TEST(Introspect, NamelessElements)
{
    const std::string xml = R"(
<node>
  <interface/>
  <interface name="xyz.openbmc_project.Test"/>
  <node/>
  <node name="child"/>
</node>)";

    EXPECT_THAT(parseIntrospectXml("/a", xml),
                Optional(AllOf(Field(&IntrospectResult::interfaces,
                                     ElementsAre("xyz.openbmc_project.Test")),
                               Field(&IntrospectResult::childPaths,
                                     ElementsAre("/a/child")))));
}

TEST(Introspect, InvalidXml)
{
    EXPECT_FALSE(parseIntrospectXml("/", "<node").has_value());
    EXPECT_FALSE(parseIntrospectXml("/", "<foo/>").has_value());
}
