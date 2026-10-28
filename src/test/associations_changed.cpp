#include "src/processing.hpp"
#include "src/test/util/asio_server_class.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <gtest/gtest.h>

class TestAssociationsChanged : public AsioServerClassTest
{
  protected:
    boost::asio::io_context io;
    AssociationMaps assocMaps;
};
sdbusplus::asio::object_server*
    TestAssociationsChanged::AsioServerClassTest::server = nullptr;

static const std::string owner = "xyz.openbmc_project.Test.Owner";
static const std::string ownerUnique = ":1.10";
static const std::string other = "xyz.openbmc_project.Test.Other";
static const std::string otherUnique = ":1.11";
static const std::string source = "/xyz/openbmc_project/test/source_tac";
static const std::string endpoint = "/xyz/openbmc_project/test/endpoint_tac";

TEST(HasInterface, Basic)
{
    InterfaceMapType interfaceMap = {
        {"/a", {{"svc0", {"iface0"}}, {"svc1", {"iface1"}}}}};

    EXPECT_TRUE(hasInterface(interfaceMap, "/a", "svc0", "iface0"));
    EXPECT_FALSE(hasInterface(interfaceMap, "/a", "svc0", "iface1"));
    EXPECT_FALSE(hasInterface(interfaceMap, "/a", "svc2", "iface0"));
    EXPECT_FALSE(hasInterface(interfaceMap, "/b", "svc0", "iface0"));
}

// An associations change from the service that implements the association
// definitions interface on the path must be applied.
TEST_F(TestAssociationsChanged, OwnerAccepted)
{
    NameOwners nameOwners = {{ownerUnique, {owner}}, {otherUnique, {other}}};
    InterfaceMapType interfaceMap = {
        {source, {{owner, {assocDefsInterface}}}},
        {endpoint, {{other, {"xyz.openbmc_project.Test.Iface"}}}}};

    processAssociationsChanged(io, nameOwners, ownerUnique, source,
                               {{"fwd_tac", "rev_tac", endpoint}}, interfaceMap,
                               assocMaps, *server);

    EXPECT_EQ(assocMaps.ifaces.size(), 2);
    EXPECT_TRUE(assocMaps.owners.contains(source));
}

// An associations change from another connection must be ignored.
TEST_F(TestAssociationsChanged, OtherSenderIgnored)
{
    NameOwners nameOwners = {{ownerUnique, {owner}}, {otherUnique, {other}}};
    InterfaceMapType interfaceMap = {
        {source, {{owner, {assocDefsInterface}}}},
        {endpoint, {{other, {"xyz.openbmc_project.Test.Iface"}}}}};

    processAssociationsChanged(io, nameOwners, otherUnique, source,
                               {{"fwd_tac", "rev_tac", endpoint}}, interfaceMap,
                               assocMaps, *server);
    processAssociationsChanged(io, nameOwners, ":1.12", source,
                               {{"fwd_tac", "rev_tac", endpoint}}, interfaceMap,
                               assocMaps, *server);

    EXPECT_TRUE(assocMaps.ifaces.empty());
    EXPECT_TRUE(assocMaps.owners.empty());
}

// When one connection owns multiple well-known names, only the name that
// implements the association definitions interface on the path must be used.
TEST_F(TestAssociationsChanged, MultipleWellKnownNamesOnlyOwnerAccepted)
{
    NameOwners nameOwners = {{ownerUnique, {owner, other}}};
    const InterfaceMapType interfaceMap = {
        {source,
         {{owner, {assocDefsInterface}},
          {other, {"xyz.openbmc_project.Test.Iface"}}}},
        {endpoint, {{other, {"xyz.openbmc_project.Test.Iface"}}}}};

    processAssociationsChanged(io, nameOwners, ownerUnique, source,
                               {{"fwd_mwn", "rev_mwn", endpoint}}, interfaceMap,
                               assocMaps, *server);

    EXPECT_EQ(assocMaps.ifaces.size(), 2);
    ASSERT_TRUE(assocMaps.owners.contains(source));
    EXPECT_TRUE(assocMaps.owners[source].contains(owner));
    EXPECT_FALSE(assocMaps.owners[source].contains(other));
}
