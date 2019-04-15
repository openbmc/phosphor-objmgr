#include "src/processing.hpp"
#include "src/test/util/asio_server_class.hpp"
#include "src/test/util/association_objects.hpp"

#include <gtest/gtest.h>

class TestNameChange : public AsioServerClassTest
{
};
sdbusplus::asio::object_server* TestNameChange::AsioServerClassTest::server =
    nullptr;

// Verify unique name is removed from nameOwners
TEST_F(TestNameChange, UniqueNameNoInterfaces)
{
    boost::container::flat_map<std::string, std::string> nameOwners = {
        {":1.99", "test-name"}};
    std::string wellKnown = {"test-name"};
    std::string oldOwner = {":1.99"};
    interface_map_type interfaceMap;
    AssociationOwnersType assocOwners;
    AssociationInterfaces assocInterfaces;

    processNameChangeDelete(nameOwners, wellKnown, oldOwner, interfaceMap,
                            assocOwners, assocInterfaces, *server);
    EXPECT_EQ(nameOwners.size(), 0);
}

// Verify path removed from interface map and association objects
TEST_F(TestNameChange, UniqueNameAssociationsAndInterface)
{
    boost::container::flat_map<std::string, std::string> nameOwners = {
        {":1.99", DEFAULT_DBUS_SVC}};
    std::string oldOwner = {":1.99"};
    boost::container::flat_set<std::string> assocInterfacesSet = {
        assocDefsInterface};

    // Build up these objects so that an associated interface will match
    // with the associated owner being removed
    auto assocOwners = createDefaultOwnerAssociation();
    auto assocInterfaces = createDefaultInterfaceAssociation(server);
    auto interfaceMap = createInterfaceMap(
        DEFAULT_SOURCE_PATH, DEFAULT_DBUS_SVC, assocInterfacesSet);

    processNameChangeDelete(nameOwners, DEFAULT_DBUS_SVC, oldOwner,
                            interfaceMap, assocOwners, assocInterfaces,
                            *server);
    EXPECT_EQ(nameOwners.size(), 0);

    // Verify owner association was deleted
    EXPECT_TRUE(assocOwners.empty());

    // Verify endpoint was deleted from interface association
    auto intfEndpoints =
        std::get<endpointsPos>(assocInterfaces[DEFAULT_FWD_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 0);
    intfEndpoints = std::get<endpointsPos>(assocInterfaces[DEFAULT_REV_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 0);

    // Verify interface map was deleted
    EXPECT_TRUE(interfaceMap.empty());
}
