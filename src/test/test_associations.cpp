#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <src/associations.hpp>
#include <src/test/util/asio_server_class.hpp>
#include <src/test/util/association_objects.hpp>

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

// Verify no associations or endpoints removed when the change is identical
TEST_F(TestAssociations, checkAssociationEndpointRemovesNoEpRemove)
{
    std::string sourcePath = "/logging/entry/1";
    std::string owner = "xyz.openbmc_project.Test";
    std::string assocPath = "/logging/entry/1/callout";
    std::string endpoint = "/system/cpu0";
    boost::container::flat_set<std::string> assocEndpoints = {endpoint};
    AssociationPaths newAssocPaths = {{assocPath, assocEndpoints}};
    Endpoints intfEndpoints = {endpoint};

    // Build up these objects so that an associated interface will match
    // with the associated owner being removed
    auto assocOwners =
        createOwnerAssociation(sourcePath, owner, assocPath, assocEndpoints);
    auto assocInterfaces =
        createInterfaceAssociation(assocPath, server, intfEndpoints);

    checkAssociationEndpointRemoves(sourcePath, owner, newAssocPaths, *server,
                                    assocOwners, assocInterfaces);

    // Verify endpoint was not deleted because it matches with what was
    // in the original
    intfEndpoints = std::get<endpointsPos>(assocInterfaces[assocPath]);
    EXPECT_EQ(intfEndpoints.size(), 1);
}

// Verify endpoint is removed when assoc path is different
TEST_F(TestAssociations, checkAssociationEndpointRemovesEpRemoveApDiff)
{
    std::string sourcePath = "/logging/entry/1";
    std::string owner = "xyz.openbmc_project.Test";
    std::string assocPath = "/logging/entry/1/callout";
    std::string assocPath2 = "/logging/entry/2/callout";
    std::string endpoint = "/system/cpu0";
    boost::container::flat_set<std::string> assocEndpoints = {endpoint};
    AssociationPaths newAssocPaths = {{assocPath2, assocEndpoints}};
    Endpoints intfEndpoints = {endpoint};

    // Build up these objects so that an associated interface will match
    // with the associated owner being removed
    auto assocOwners =
        createOwnerAssociation(sourcePath, owner, assocPath, assocEndpoints);
    auto assocInterfaces =
        createInterfaceAssociation(assocPath, server, intfEndpoints);

    checkAssociationEndpointRemoves(sourcePath, owner, newAssocPaths, *server,
                                    assocOwners, assocInterfaces);

    // Verify endpoint was deleted since associated path was different
    intfEndpoints = std::get<endpointsPos>(assocInterfaces[assocPath]);
    EXPECT_EQ(intfEndpoints.size(), 0);
}

// Verify endpoint is removed when endpoint is different
TEST_F(TestAssociations, checkAssociationEndpointRemovesEpRemoveEpChanged)
{
    std::string sourcePath = "/logging/entry/1";
    std::string owner = "xyz.openbmc_project.Test";
    std::string assocPath = "/logging/entry/1/callout";
    std::string endpoint = "/system/cpu0";
    boost::container::flat_set<std::string> assocEndpoints = {endpoint};
    boost::container::flat_set<std::string> extraAssocEndpoints = {
        "/different/endpoint"};
    AssociationPaths newAssocPaths = {{assocPath, extraAssocEndpoints}};
    Endpoints intfEndpoints = {endpoint};

    // Build up these objects so that an associated interface will match
    // with the associated owner being removed
    auto assocOwners =
        createOwnerAssociation(sourcePath, owner, assocPath, assocEndpoints);
    auto assocInterfaces =
        createInterfaceAssociation(assocPath, server, intfEndpoints);

    checkAssociationEndpointRemoves(sourcePath, owner, newAssocPaths, *server,
                                    assocOwners, assocInterfaces);

    // Verify endpoint was deleted since endpoint path was different
    intfEndpoints = std::get<endpointsPos>(assocInterfaces[assocPath]);
    EXPECT_EQ(intfEndpoints.size(), 0);
}
