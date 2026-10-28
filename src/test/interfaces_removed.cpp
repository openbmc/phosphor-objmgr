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

// The root path must only be removed for an owner if that owner has no other
// objects.
TEST_F(TestInterfacesRemoved, RootKeptWithOtherChild)
{
    const InterfaceNames defaults = {"org.freedesktop.DBus.Introspectable",
                                     "org.freedesktop.DBus.Peer",
                                     "org.freedesktop.DBus.Properties"};
    InterfaceNames withIface = defaults;
    withIface.emplace("xyz.openbmc_project.Test.Iface");

    InterfaceMapType interfaceMap = {
        {"/", {{service, defaults}}},     {"/a", {{service, defaults}}},
        {"/a/x", {{service, withIface}}}, {"/b", {{service, defaults}}},
        {"/b/y", {{service, withIface}}},
    };

    // "/" and "/a" only have default interfaces. "/" must be kept since
    // service still owns "/b/y".
    interfaceMap.erase("/a/x");
    removeUnneededParents(sdbusplus::object_path("/a/x"), service,
                          interfaceMap);
    EXPECT_FALSE(interfaceMap.contains("/a"));
    EXPECT_TRUE(interfaceMap.contains("/"));

    interfaceMap.erase("/b/y");
    removeUnneededParents(sdbusplus::object_path("/b/y"), service,
                          interfaceMap);
    EXPECT_TRUE(interfaceMap.empty());
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

// An object found through introspection also has the default interfaces. It
// must be removed once all its other interfaces have been removed.
TEST_F(TestInterfacesRemoved, IntrospectedObjectRemoved)
{
    const InterfaceNames defaults = {"org.freedesktop.DBus.Introspectable",
                                     "org.freedesktop.DBus.Peer",
                                     "org.freedesktop.DBus.Properties"};
    InterfaceNames withIface = defaults;
    withIface.emplace("xyz.openbmc_project.Sensor.Value");

    InterfaceMapType interfaceMap = {
        {"/", {{service, defaults}}},
        {"/sensors", {{service, defaults}}},
        {"/sensors/t1", {{service, withIface}}},
        {"/sensors/t2", {{service, withIface}}},
    };

    processInterfacesRemoved(
        io, interfaceMap, sdbusplus::object_path("/sensors/t1"),
        {"xyz.openbmc_project.Sensor.Value"}, service, assocMaps, *server);

    EXPECT_FALSE(interfaceMap.contains("/sensors/t1"));
    EXPECT_TRUE(interfaceMap.contains("/sensors/t2"));
    EXPECT_TRUE(interfaceMap.contains("/sensors"));
}

// An object that only has the default interfaces left but that has children
// of the same service must be kept.
TEST_F(TestInterfacesRemoved, IntrospectedParentKept)
{
    const InterfaceNames defaults = {"org.freedesktop.DBus.Introspectable",
                                     "org.freedesktop.DBus.Peer",
                                     "org.freedesktop.DBus.Properties"};
    InterfaceNames withIface = defaults;
    withIface.emplace("xyz.openbmc_project.Test.Iface");

    InterfaceMapType interfaceMap = {
        {"/", {{service, defaults}}},
        {"/a", {{service, withIface}}},
        {"/a/b", {{service, withIface}}},
    };

    processInterfacesRemoved(io, interfaceMap, sdbusplus::object_path("/a"),
                             {"xyz.openbmc_project.Test.Iface"}, service,
                             assocMaps, *server);

    ASSERT_TRUE(interfaceMap.contains("/a"));
    EXPECT_EQ(interfaceMap["/a"][service], defaults);
    EXPECT_TRUE(interfaceMap.contains("/a/b"));
}

// Removing the last non-default interface of an introspected association
// endpoint must move the association to pending.
TEST_F(TestInterfacesRemoved, IntrospectedEndpointMovedToPending)
{
    const std::string mapper = "xyz.openbmc_project.ObjectMapper";
    const std::string otherService = "xyz.openbmc_project.Test.Other";
    const std::string endpoint = "/xyz/openbmc_project/inventory/fan_iemtp";
    const std::string source = "/xyz/openbmc_project/sensors/s_iemtp";
    const InterfaceNames defaults = {"org.freedesktop.DBus.Introspectable",
                                     "org.freedesktop.DBus.Peer",
                                     "org.freedesktop.DBus.Properties"};
    InterfaceNames withIface = defaults;
    withIface.emplace("xyz.openbmc_project.Inventory.Item");

    InterfaceMapType interfaceMap = {
        {endpoint, {{service, withIface}, {mapper, {}}}},
        {source, {{otherService, {assocDefsInterface}}}},
    };

    associationChanged(io, *server, {{"inventory", "sensors", endpoint}},
                       sdbusplus::object_path(source), otherService,
                       interfaceMap, assocMaps);
    ASSERT_EQ(assocMaps.ifaces.size(), 2);

    processInterfacesRemoved(io, interfaceMap, sdbusplus::object_path(endpoint),
                             {"xyz.openbmc_project.Inventory.Item"}, service,
                             assocMaps, *server);

    EXPECT_FALSE(interfaceMap[endpoint].contains(service));
    EXPECT_TRUE(assocMaps.ifaces.empty());
    EXPECT_TRUE(assocMaps.pending.contains(endpoint));
}

// An object added via InterfacesAdded that loses its interface must still be
// kept with an empty interface set while the same service owns children.
TEST_F(TestInterfacesRemoved, InterfacesAddedParentKept)
{
    InterfaceMapType interfaceMap;
    InterfacesAdded intfAdded = {{"xyz.openbmc_project.Test.Iface", {}}};

    processInterfaceAdded(io, interfaceMap, sdbusplus::object_path("/a"),
                          intfAdded, service, assocMaps, *server);
    processInterfaceAdded(io, interfaceMap, sdbusplus::object_path("/a/b"),
                          intfAdded, service, assocMaps, *server);

    processInterfacesRemoved(io, interfaceMap, sdbusplus::object_path("/a"),
                             {"xyz.openbmc_project.Test.Iface"}, service,
                             assocMaps, *server);

    ASSERT_TRUE(interfaceMap.contains("/a"));
    EXPECT_TRUE(interfaceMap["/a"][service].empty());
    EXPECT_TRUE(interfaceMap.contains("/a/b"));

    processInterfacesRemoved(io, interfaceMap, sdbusplus::object_path("/a/b"),
                             {"xyz.openbmc_project.Test.Iface"}, service,
                             assocMaps, *server);
    EXPECT_TRUE(interfaceMap.empty());
}
