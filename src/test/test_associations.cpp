#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <src/associations.hpp>

#include <gtest/gtest.h>

class TestAssociations : public testing::Test
{
  protected:
    // Make this global to the whole test suite since we want to share
    // the asio::object_server accross the test cases
    // NOTE - latest googltest changed to SetUpTestSuite()
    static void SetUpTestCase()
    {
        boost::asio::io_context io;
        auto conn = std::make_shared<sdbusplus::asio::connection>(io);

        conn->request_name("xyz.openbmc_project.ObjMgr.Test");
        server = new sdbusplus::asio::object_server(conn);
    }

    // NOTE - latest googltest changed to TearDownTestSuite()
    static void TearDownTestCase()
    {
        delete server;
        server = nullptr;
    }

    static sdbusplus::asio::object_server* server;
};

sdbusplus::asio::object_server* TestAssociations::server = nullptr;

// Create a default AssociationOwnersType object with input values
AssociationOwnersType createOwnerAssociation(
    const std::string& path, const std::string& service,
    const std::string& assocPath,
    const boost::container::flat_set<std::string>& endpoints)
{
    AssociationPaths assocPathMap = {{assocPath, endpoints}};
    boost::container::flat_map<std::string, AssociationPaths> serviceMap = {
        {service, assocPathMap}};
    AssociationOwnersType ownerAssoc = {{path, serviceMap}};
    return ownerAssoc;
}

// Create a default AssociationInterfaces object with input values
AssociationInterfaces
    createInterfaceAssociation(const std::string& ifaceObject,
                               sdbusplus::asio::object_server* server,
                               const Endpoints endpoints)
{
    std::shared_ptr<sdbusplus::asio::dbus_interface> iface =
        server->add_interface("/xyz/openbmc_project/test",
                              "xyz.openbmc_project.New.Interface");
    std::tuple<std::shared_ptr<sdbusplus::asio::dbus_interface>, Endpoints>
        serverEndPts = {iface, endpoints};
    AssociationInterfaces interfaceAssoc = {{ifaceObject, serverEndPts}};
    return interfaceAssoc;
}

// Verify call when path is not in associated owners
TEST_F(TestAssociations, SourcePathNotInAssociations)
{
    EXPECT_NE(nullptr, server);
    std::string sourcePath = "/xyz/openbmc_project/no/association";
    std::string owner = "xyz.openbmc_project.Test";
    AssociationOwnersType assocOwners;
    AssociationInterfaces assocInterfaces;

    removeAssociation(sourcePath, owner, *server, assocOwners, assocInterfaces);
}

// Verify call when owner is not in associated owners
TEST_F(TestAssociations, OwnerNotInAssociations)
{
    std::string sourcePath = "/logging/entry/1";
    std::string owner = "xyz.openbmc_project.Test";
    std::string assocPath = "/logging/entry/1/callout";
    boost::container::flat_set<std::string> endpoints = {"/system/cpu0"};
    AssociationInterfaces assocInterfaces;

    auto assocOwners =
        createOwnerAssociation(sourcePath, owner, assocPath, endpoints);

    removeAssociation(sourcePath, owner, *server, assocOwners, assocInterfaces);
}

// Verify call when path is not in associated interfaces
TEST_F(TestAssociations, PathNotInAssocInterfaces)
{
    std::string sourcePath = "/logging/entry/1";
    std::string owner = "xyz.openbmc_project.Test";
    std::string assocPath = "/logging/entry/1/callout";
    boost::container::flat_set<std::string> endpoints = {"/system/cpu0"};
    AssociationInterfaces assocInterfaces;

    auto assocOwners =
        createOwnerAssociation(sourcePath, owner, assocPath, endpoints);

    removeAssociation(sourcePath, owner, *server, assocOwners, assocInterfaces);

    EXPECT_EQ(assocOwners.empty(), true);
}

// Verify call when path is in associated interfaces
TEST_F(TestAssociations, PathIsInAssociatedInterfaces)
{
    std::string sourcePath = "/logging/entry/1";
    std::string owner = "xyz.openbmc_project.Test";
    std::string assocPath = "/logging/entry/1/callout";
    std::string endpoint = "/system/cpu0";
    boost::container::flat_set<std::string> assocEndpoints = {endpoint};
    Endpoints intfEndpoints = {endpoint};

    // Build up these objects so that an associated interface will match
    // with the associated owner being removed
    auto assocOwners =
        createOwnerAssociation(sourcePath, owner, assocPath, assocEndpoints);
    auto assocInterfaces =
        createInterfaceAssociation(assocPath, server, intfEndpoints);

    removeAssociation(sourcePath, owner, *server, assocOwners, assocInterfaces);

    // Verify owner association was deleted
    EXPECT_EQ(assocOwners.empty(), true);

    // Verify endpoint was deleted from interface association
    intfEndpoints = std::get<endpointsPos>(assocInterfaces[assocPath]);
    EXPECT_EQ(intfEndpoints.size(), 0);
}

// Verify call when path is in associated interfaces, with extra endpoints
TEST_F(TestAssociations, PathIsInAssociatedInterfacesExtraEndpoints)
{
    std::string sourcePath = "/logging/entry/1";
    std::string owner = "xyz.openbmc_project.Test";
    std::string assocPath = "/logging/entry/1/callout";
    std::string endpoint = "/system/cpu0";
    boost::container::flat_set<std::string> assocEndpoints = {endpoint};
    Endpoints intfEndpoints = {endpoint, "/extra/endpoint"};

    // Build up these objects so that an associated interface will match
    // with the associated owner being removed
    auto assocOwners =
        createOwnerAssociation(sourcePath, owner, assocPath, assocEndpoints);
    auto assocInterfaces =
        createInterfaceAssociation(assocPath, server, intfEndpoints);

    removeAssociation(sourcePath, owner, *server, assocOwners, assocInterfaces);

    // Verify owner association was deleted
    EXPECT_EQ(assocOwners.empty(), true);

    // Verify endpoint was not deleted because there was an extra one added
    intfEndpoints = std::get<endpointsPos>(assocInterfaces[assocPath]);
    EXPECT_EQ(intfEndpoints.size(), 1);
}
