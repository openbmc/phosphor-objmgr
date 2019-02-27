#include "src/processing.hpp"
#include "src/test/util/asio_server_class.hpp"
#include "src/test/util/association_objects.hpp"
#include "src/test/util/debug_output.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <gtest/gtest.h>

class TestInterfacesAdded : public AsioServerClassTest
{
};
sdbusplus::asio::object_server*
    TestInterfacesAdded::AsioServerClassTest::server = nullptr;

// This is the data structure that comes in via the InterfacesAdded
// signal
InterfacesAdded createInterfacesAdded()
{
    std::vector<Association> associations = {
        {"inventory", "error",
         "/xyz/openbmc_project/inventory/system/chassis"}};
    sdbusplus::message::variant<std::vector<Association>> sdbVecAssoc = {
        associations};
    std::vector<std::pair<
        std::string, sdbusplus::message::variant<std::vector<Association>>>>
        vecMethToAssoc = {{"associations", sdbVecAssoc}};
    InterfacesAdded intfAdded = {{ASSOCIATIONS_INTERFACE, vecMethToAssoc}};
    return intfAdded;
}

// Verify good path of interfaces added function
TEST_F(TestInterfacesAdded, InterfacesAddedGoodPath)
{
    interface_map_type interfaceMap;
    AssociationOwnersType assocOwners;
    AssociationInterfaces assocInterfaces;
    auto intfAdded = createInterfacesAdded();

    processInterfaceAdded(interfaceMap, DEFAULT_SOURCE_PATH, intfAdded,
                          DEFAULT_DBUS_SVC, assocOwners, assocInterfaces,
                          *server);

    // Interface map will get the following:
    // /logging/entry/1 /logging/entry /logging/ /
    // dump_InterfaceMapType(interfaceMap);
    EXPECT_EQ(interfaceMap.size(), 4);

    // New association ower created so ensure it now contains a single entry
    // dump_AssociationOwnersType(assocOwners);
    EXPECT_EQ(assocOwners.size(), 1);

    // Ensure the 2 association interfaces were created
    // dump_AssociationInterfaces(assocInterfaces);
    EXPECT_EQ(assocInterfaces.size(), 2);
}
