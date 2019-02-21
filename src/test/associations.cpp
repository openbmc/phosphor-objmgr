#include "src/associations.hpp"

#include "src/test/util/asio_server_class.hpp"
#include "src/test/util/association_objects.hpp"

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
