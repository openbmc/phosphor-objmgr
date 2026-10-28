#include "src/processing.hpp"
#include "src/test/util/asio_server_class.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <gtest/gtest.h>

class TestInterfacesRemoved : public AsioServerClassTest
{
  protected:
    boost::asio::io_context io;
    AssociationMaps assocMaps;
};
sdbusplus::asio::object_server*
    TestInterfacesRemoved::AsioServerClassTest::server = nullptr;

static const std::string service = "xyz.openbmc_project.Test.Service";

// Parents created by processInterfaceAdded() must be removed once the object
// that caused their creation has been removed.
TEST_F(TestInterfacesRemoved, ImplicitParentsRemoved)
{
    InterfaceMapType interfaceMap;
    InterfacesAdded intfAdded = {{"xyz.openbmc_project.Test.Iface", {}}};

    processInterfaceAdded(io, interfaceMap, sdbusplus::object_path("/a/b/c"),
                          intfAdded, service, assocMaps, *server);
    EXPECT_TRUE(interfaceMap.contains("/a/b"));
    EXPECT_TRUE(interfaceMap.contains("/a"));

    processInterfacesRemoved(io, interfaceMap, sdbusplus::object_path("/a/b/c"),
                             {"xyz.openbmc_project.Test.Iface"}, service,
                             assocMaps, *server);

    EXPECT_TRUE(interfaceMap.empty());
}

// Parents of other objects of the same owner must be kept.
TEST_F(TestInterfacesRemoved, ImplicitParentsWithOtherChildKept)
{
    InterfaceMapType interfaceMap;
    InterfacesAdded intfAdded = {{"xyz.openbmc_project.Test.Iface", {}}};

    processInterfaceAdded(io, interfaceMap, sdbusplus::object_path("/a/b/c"),
                          intfAdded, service, assocMaps, *server);
    processInterfaceAdded(io, interfaceMap, sdbusplus::object_path("/a/d"),
                          intfAdded, service, assocMaps, *server);

    processInterfacesRemoved(io, interfaceMap, sdbusplus::object_path("/a/b/c"),
                             {"xyz.openbmc_project.Test.Iface"}, service,
                             assocMaps, *server);

    EXPECT_FALSE(interfaceMap.contains("/a/b/c"));
    EXPECT_FALSE(interfaceMap.contains("/a/b"));
    EXPECT_TRUE(interfaceMap.contains("/a"));
    EXPECT_TRUE(interfaceMap.contains("/a/d"));
    EXPECT_TRUE(interfaceMap.contains("/"));
}

// The empty parent entries that the ObjectMapper creates for its own
// association objects must be removed once the association object has been
// removed, without affecting the entries of other services.
TEST_F(TestInterfacesRemoved, MapperParentsRemoved)
{
    const std::string mapper = "xyz.openbmc_project.ObjectMapper";
    const std::string assocIface = "xyz.openbmc_project.Association";
    const std::string entry = "/xyz/openbmc_project/logging/entry/1";

    InterfaceMapType interfaceMap = {
        {entry, {{service, {"xyz.openbmc_project.Logging.Entry"}}}}};

    processInterfaceAdded(io, interfaceMap,
                          sdbusplus::object_path(entry + "/callout"),
                          {{assocIface, {}}}, mapper, assocMaps, *server);
    ASSERT_TRUE(interfaceMap[entry].contains(mapper));

    processInterfacesRemoved(io, interfaceMap,
                             sdbusplus::object_path(entry + "/callout"),
                             {assocIface}, mapper, assocMaps, *server);

    EXPECT_FALSE(interfaceMap.contains(entry + "/callout"));
    ASSERT_TRUE(interfaceMap.contains(entry));
    EXPECT_FALSE(interfaceMap[entry].contains(mapper));
    EXPECT_TRUE(interfaceMap[entry].contains(service));
    for (const auto& [path, owners] : interfaceMap)
    {
        EXPECT_FALSE(owners.contains(mapper)) << path;
    }
}
