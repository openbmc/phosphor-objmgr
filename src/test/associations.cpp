#include "src/associations.hpp"

#include "src/test/util/asio_server_class.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <gtest/gtest.h>

class TestAssociations : public AsioServerClassTest
{
};
sdbusplus::asio::object_server* TestAssociations::AsioServerClassTest::server =
    nullptr;

const std::string DEFAULT_SOURCE_PATH = "/logging/entry/1";
const std::string DEFAULT_DBUS_SVC = "xyz.openbmc_project.New.Interface";
const std::string DEFAULT_FWD_PATH = {DEFAULT_SOURCE_PATH + "/" + "inventory"};
const std::string DEFAULT_ENDPOINT =
    "/xyz/openbmc_project/inventory/system/chassis";
const std::string DEFAULT_REV_PATH = {DEFAULT_ENDPOINT + "/" + "error"};
const std::string EXTRA_ENDPOINT = "/xyz/openbmc_project/differnt/endpoint";

// Create a default AssociationOwnersType object
AssociationOwnersType createDefaultOwnerAssociation()
{
    AssociationPaths assocPathMap = {{DEFAULT_FWD_PATH, {DEFAULT_ENDPOINT}},
                                     {DEFAULT_REV_PATH, {DEFAULT_SOURCE_PATH}}};
    boost::container::flat_map<std::string, AssociationPaths> serviceMap = {
        {DEFAULT_DBUS_SVC, assocPathMap}};
    AssociationOwnersType ownerAssoc = {{DEFAULT_SOURCE_PATH, serviceMap}};
    return ownerAssoc;
}

// Create a default AssociationInterfaces object
AssociationInterfaces
    createDefaultInterfaceAssociation(sdbusplus::asio::object_server* server)
{
    AssociationInterfaces interfaceAssoc;

    auto& iface = interfaceAssoc[DEFAULT_FWD_PATH];
    auto& endpoints = std::get<endpointsPos>(iface);
    endpoints.push_back(DEFAULT_ENDPOINT);
    server->add_interface(DEFAULT_FWD_PATH, DEFAULT_DBUS_SVC);

    auto& iface2 = interfaceAssoc[DEFAULT_REV_PATH];
    auto& endpoints2 = std::get<endpointsPos>(iface2);
    endpoints2.push_back(DEFAULT_SOURCE_PATH);
    server->add_interface(DEFAULT_REV_PATH, DEFAULT_DBUS_SVC);

    return interfaceAssoc;
}

// Just add an extra endpoint to the first association
void addEndpointToInterfaceAssociation(AssociationInterfaces& interfaceAssoc)
{
    auto iface = interfaceAssoc[DEFAULT_FWD_PATH];
    auto endpoints = std::get<endpointsPos>(iface);
    endpoints.push_back(EXTRA_ENDPOINT);
}

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
