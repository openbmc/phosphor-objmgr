#include <src/processing.hpp>
#include <src/test/util/asio_server_class.hpp>
#include <src/test/util/association_objects.hpp>

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

    process_name_change_delete(nameOwners, wellKnown, oldOwner, interfaceMap,
                               assocOwners, assocInterfaces, *server);
    EXPECT_EQ(nameOwners.size(), 0);
}

// Verify path removed from interface map and association objects
TEST_F(TestNameChange, UniqueNameAssociationsAndInterface)
{
    boost::container::flat_map<std::string, std::string> nameOwners = {
        {":1.99", "test-name"}};
    std::string wellKnown = {"test-name"};
    std::string oldOwner = {":1.99"};
    std::string sourcePath = "/logging/entry/1";
    std::string owner = "xyz.openbmc_project.Test";
    std::string assocPath = "/logging/entry/1/callout";
    std::string endpoint = "/system/cpu0";
    boost::container::flat_set<std::string> assocEndpoints = {endpoint};
    Endpoints intfEndpoints = {endpoint};
    boost::container::flat_set<std::string> assocInterfacesSet = {
        ASSOCIATIONS_INTERFACE};

    // Build up these objects so that an associated interface will match
    // with the associated owner being removed
    auto assocOwners = createOwnerAssociation(sourcePath, wellKnown, assocPath,
                                              assocEndpoints);
    auto assocInterfaces =
        createInterfaceAssociation(assocPath, server, intfEndpoints);
    auto interfaceMap =
        createInterfaceMap(sourcePath, wellKnown, assocInterfacesSet);

    process_name_change_delete(nameOwners, wellKnown, oldOwner, interfaceMap,
                               assocOwners, assocInterfaces, *server);
    EXPECT_EQ(nameOwners.size(), 0);

    // Verify owner association was deleted
    EXPECT_EQ(assocOwners.empty(), true);

    // Verify endpoint was deleted from interface association
    intfEndpoints = std::get<endpointsPos>(assocInterfaces[assocPath]);
    EXPECT_EQ(intfEndpoints.size(), 0);

    // Verify interface map was deleted
    EXPECT_EQ(interfaceMap.empty(), true);
}
