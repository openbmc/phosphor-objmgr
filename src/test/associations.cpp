#include "src/associations.hpp"
#include "src/test/util/asio_server_class.hpp"
#include "src/test/util/association_objects.hpp"
#include "src/test/util/debug_output.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <gtest/gtest.h>

class TestAssociations : public AsioServerClassTest
{
};
sdbusplus::asio::object_server* TestAssociations::AsioServerClassTest::server =
    nullptr;

// Verify call when path is not in associated owners
TEST_F(TestAssociations, SourcePathNotInAssociations)
{
    EXPECT_NE(nullptr, server);
    std::string sourcePath = "/xyz/openbmc_project/no/association";
    AssociationOwnersType assocOwners;
    AssociationInterfaces assocInterfaces;

    removeAssociation(sourcePath, DEFAULT_DBUS_SVC, *server, assocOwners,
                      assocInterfaces);
}

// Verify call when owner is not in associated owners
TEST_F(TestAssociations, OwnerNotInAssociations)
{
    AssociationInterfaces assocInterfaces;

    auto assocOwners = createDefaultOwnerAssociation();

    removeAssociation(DEFAULT_SOURCE_PATH, DEFAULT_DBUS_SVC, *server,
                      assocOwners, assocInterfaces);
}

// Verify call when path is not in associated interfaces
TEST_F(TestAssociations, PathNotInAssocInterfaces)
{
    AssociationInterfaces assocInterfaces;

    auto assocOwners = createDefaultOwnerAssociation();

    removeAssociation(DEFAULT_SOURCE_PATH, DEFAULT_DBUS_SVC, *server,
                      assocOwners, assocInterfaces);

    EXPECT_TRUE(assocOwners.empty());
}

// Verify call when path is in associated interfaces
TEST_F(TestAssociations, PathIsInAssociatedInterfaces)
{
    // Build up these objects so that an associated interface will match
    // with the associated owner being removed
    auto assocOwners = createDefaultOwnerAssociation();
    auto assocInterfaces = createDefaultInterfaceAssociation(server);

    removeAssociation(DEFAULT_SOURCE_PATH, DEFAULT_DBUS_SVC, *server,
                      assocOwners, assocInterfaces);

    // Verify owner association was deleted
    EXPECT_TRUE(assocOwners.empty());

    // Verify endpoint was deleted from interface association
    auto intfEndpoints =
        std::get<endpointsPos>(assocInterfaces[DEFAULT_FWD_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 0);
    intfEndpoints = std::get<endpointsPos>(assocInterfaces[DEFAULT_REV_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 0);
}

// Verify call when path is in associated interfaces, with extra endpoints
TEST_F(TestAssociations, PathIsInAssociatedInterfacesExtraEndpoints)
{
    // Build up these objects so that an associated interface will match
    // with the associated owner being removed
    auto assocOwners = createDefaultOwnerAssociation();
    auto assocInterfaces = createDefaultInterfaceAssociation(server);

    // Add another endpoint to the assoc interfaces
    addEndpointToInterfaceAssociation(assocInterfaces);

    removeAssociation(DEFAULT_SOURCE_PATH, DEFAULT_DBUS_SVC, *server,
                      assocOwners, assocInterfaces);

    // Verify owner association was deleted
    EXPECT_TRUE(assocOwners.empty());

    // Verify all endpoints are deleted since source path was deleted
    auto intfEndpoints =
        std::get<endpointsPos>(assocInterfaces[DEFAULT_FWD_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 0);
    intfEndpoints = std::get<endpointsPos>(assocInterfaces[DEFAULT_REV_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 0);
}

// Verify no associations or endpoints removed when the change is identical
TEST_F(TestAssociations, checkAssociationEndpointRemovesNoEpRemove)
{

    AssociationPaths newAssocPaths = {
        {DEFAULT_FWD_PATH, {DEFAULT_ENDPOINT}},
        {DEFAULT_REV_PATH, {DEFAULT_SOURCE_PATH}}};

    auto assocOwners = createDefaultOwnerAssociation();
    auto assocInterfaces = createDefaultInterfaceAssociation(server);

    checkAssociationEndpointRemoves(DEFAULT_SOURCE_PATH, DEFAULT_DBUS_SVC,
                                    newAssocPaths, *server, assocOwners,
                                    assocInterfaces);

    // Verify endpoints were not deleted because they matche with what was
    // in the original
    auto intfEndpoints =
        std::get<endpointsPos>(assocInterfaces[DEFAULT_FWD_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 1);
    intfEndpoints = std::get<endpointsPos>(assocInterfaces[DEFAULT_REV_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 1);
}

// Verify endpoint is removed when assoc path is different
TEST_F(TestAssociations, checkAssociationEndpointRemovesEpRemoveApDiff)
{
    AssociationPaths newAssocPaths = {{"/different/path", {DEFAULT_ENDPOINT}}};

    auto assocOwners = createDefaultOwnerAssociation();
    auto assocInterfaces = createDefaultInterfaceAssociation(server);

    checkAssociationEndpointRemoves(DEFAULT_SOURCE_PATH, DEFAULT_DBUS_SVC,
                                    newAssocPaths, *server, assocOwners,
                                    assocInterfaces);

    // Verify initial endpoints were deleted because the new path
    auto intfEndpoints =
        std::get<endpointsPos>(assocInterfaces[DEFAULT_FWD_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 0);
    intfEndpoints = std::get<endpointsPos>(assocInterfaces[DEFAULT_REV_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 0);
}

// Verify endpoint is removed when endpoint is different
TEST_F(TestAssociations, checkAssociationEndpointRemovesEpRemoveEpChanged)
{
    AssociationPaths newAssocPaths = {
        {DEFAULT_FWD_PATH, {DEFAULT_ENDPOINT + "/different"}},
        {DEFAULT_REV_PATH, {DEFAULT_SOURCE_PATH + "/different"}}};

    auto assocOwners = createDefaultOwnerAssociation();
    auto assocInterfaces = createDefaultInterfaceAssociation(server);

    checkAssociationEndpointRemoves(DEFAULT_SOURCE_PATH, DEFAULT_DBUS_SVC,
                                    newAssocPaths, *server, assocOwners,
                                    assocInterfaces);

    // Verify initial endpoints were deleted because of different endpoints
    auto intfEndpoints =
        std::get<endpointsPos>(assocInterfaces[DEFAULT_FWD_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 0);
    intfEndpoints = std::get<endpointsPos>(assocInterfaces[DEFAULT_REV_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 0);
}

// Verify existing endpoint deleted when empty endpoint is provided
TEST_F(TestAssociations, associationChangedEmptyEndpoint)
{
    std::vector<Association> associations = {{"inventory", "error", ""}};

    auto assocOwners = createDefaultOwnerAssociation();
    auto assocInterfaces = createDefaultInterfaceAssociation(server);

    // Empty endpoint will result in deletion of corresponding assocInterface
    associationChanged(*server, associations, DEFAULT_SOURCE_PATH,
                       DEFAULT_DBUS_SVC, assocOwners, assocInterfaces);

    // TODO - This test case found a bug where the endpoint validity
    // is not checked on the FWD path and is used by default, resulting
    // in there being a "" endpoint value. Will fix this in next commit
    // to keep the refactor of the code separate from the fix
    // (i.e. both of these should be 0 since we have an invalid endpoint)
    auto intfEndpoints =
        std::get<endpointsPos>(assocInterfaces[DEFAULT_FWD_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 1);
    intfEndpoints = std::get<endpointsPos>(assocInterfaces[DEFAULT_REV_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 0);
}

// Add a new association with endpoint
TEST_F(TestAssociations, associationChangedAddNewAssoc)
{
    std::vector<Association> associations = {
        {"abc", "def", "/xyz/openbmc_project/new/endpoint"}};

    auto assocOwners = createDefaultOwnerAssociation();
    auto assocInterfaces = createDefaultInterfaceAssociation(server);

    associationChanged(*server, associations, "/new/source/path",
                       DEFAULT_DBUS_SVC, assocOwners, assocInterfaces);

    // Two source paths
    EXPECT_EQ(assocOwners.size(), 2);

    // Four interfaces
    EXPECT_EQ(assocInterfaces.size(), 4);

    // New endpoint so assocInterfaces should be same size
    auto intfEndpoints =
        std::get<endpointsPos>(assocInterfaces[DEFAULT_FWD_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 1);
}

// Add a new association to empty objects
TEST_F(TestAssociations, associationChangedAddNewAssocEmptyObj)
{
    std::string sourcePath = "/logging/entry/1";
    std::string owner = "xyz.openbmc_project.Test";
    std::vector<Association> associations = {
        {"inventory", "error",
         "/xyz/openbmc_project/inventory/system/chassis"}};

    // Empty objects because this test will ensure assocOwners adds the
    // changed association and interface
    AssociationInterfaces assocInterfaces;
    AssociationOwnersType assocOwners;

    associationChanged(*server, associations, DEFAULT_SOURCE_PATH,
                       DEFAULT_DBUS_SVC, assocOwners, assocInterfaces);

    // New associations so ensure it now contains a single entry
    EXPECT_EQ(assocOwners.size(), 1);

    // Verify corresponding assoc paths each have one endpoint in assoc
    // interfaces and that those endpoints match
    auto singleOwner = assocOwners[DEFAULT_SOURCE_PATH];
    auto singleIntf = singleOwner[DEFAULT_DBUS_SVC];
    for (auto i : singleIntf)
    {
        auto intfEndpoints = std::get<endpointsPos>(assocInterfaces[i.first]);
        EXPECT_EQ(intfEndpoints.size(), 1);
        EXPECT_EQ(intfEndpoints[0], *i.second.begin());
    }
}

// Add a new association to same source path but with new owner
TEST_F(TestAssociations, associationChangedAddNewAssocNewOwner)
{
    std::string newOwner = "xyz.openbmc_project.Test2";
    std::vector<Association> associations = {
        {"inventory", "error",
         "/xyz/openbmc_project/inventory/system/chassis"}};

    auto assocOwners = createDefaultOwnerAssociation();
    auto assocInterfaces = createDefaultInterfaceAssociation(server);

    associationChanged(*server, associations, DEFAULT_SOURCE_PATH, newOwner,
                       assocOwners, assocInterfaces);

    // New endpoint so assocOwners should be same size
    EXPECT_EQ(assocOwners.size(), 1);

    // Ensure only one endpoint under first path
    auto intfEndpoints =
        std::get<endpointsPos>(assocInterfaces[DEFAULT_FWD_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 1);

    // Ensure the 2 new association endpoints are under the new owner
    auto a = assocOwners.find(DEFAULT_SOURCE_PATH);
    auto o = a->second.find(newOwner);
    EXPECT_EQ(o->second.size(), 2);
}

// Add a new association to existing interface path
TEST_F(TestAssociations, associationChangedAddNewAssocSameInterface)
{
    std::vector<Association> associations = {
        {"abc", "error", "/xyz/openbmc_project/inventory/system/chassis"}};

    auto assocOwners = createDefaultOwnerAssociation();
    auto assocInterfaces = createDefaultInterfaceAssociation(server);

    associationChanged(*server, associations, DEFAULT_SOURCE_PATH,
                       DEFAULT_DBUS_SVC, assocOwners, assocInterfaces);

    // Should have 3 entries in AssociationInterfaces, one is just missing an
    // endpoint
    EXPECT_EQ(assocInterfaces.size(), 3);

    // Change to existing interface so it will be removed here
    auto intfEndpoints =
        std::get<endpointsPos>(assocInterfaces[DEFAULT_FWD_PATH]);
    EXPECT_EQ(intfEndpoints.size(), 0);

    // The new endpoint should exist though in it's place
    intfEndpoints = std::get<endpointsPos>(
        assocInterfaces[DEFAULT_SOURCE_PATH + "/" + "abc"]);
    EXPECT_EQ(intfEndpoints.size(), 1);

    // Added to an existing owner path so still 1
    EXPECT_EQ(assocOwners.size(), 1);
}
