#include "src/associations.hpp"

#include "src/test/util/asio_server_class.hpp"
#include "src/test/util/association_objects.hpp"
#include "src/test/util/debug_output.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

class TestAssociations : public AsioServerClassTest
{
  public:
    boost::asio::io_context io;
    virtual void SetUp() override
    {
        io.run();
    }
};
sdbusplus::asio::object_server* TestAssociations::AsioServerClassTest::server =
    nullptr;

// Verify call when path is not in associated owners
TEST_F(TestAssociations, SourcePathNotInAssociations)
{
    EXPECT_NE(nullptr, server);
    std::string sourcePath = "/xyz/openbmc_project/no/association";
    AssociationMaps assocMaps;

    removeAssociation(io, sourcePath, defaultDbusSvc, *server, assocMaps);
}

// Verify call when owner is not in associated owners
TEST_F(TestAssociations, OwnerNotInAssociations)
{
    AssociationMaps assocMaps;
    assocMaps.owners = createDefaultOwnerAssociation();

    removeAssociation(io, defaultSourcePath, defaultDbusSvc, *server,
                      assocMaps);
}

// Verify call when path is not in associated interfaces
TEST_F(TestAssociations, PathNotInAssocInterfaces)
{
    AssociationMaps assocMaps;

    assocMaps.owners = createDefaultOwnerAssociation();

    removeAssociation(io, defaultSourcePath, defaultDbusSvc, *server,
                      assocMaps);

    EXPECT_TRUE(assocMaps.owners.empty());
}

// Verify call when path is in associated interfaces
TEST_F(TestAssociations, PathIsInAssociatedInterfaces)
{
    // Build up these objects so that an associated interface will match
    // with the associated owner being removed
    AssociationMaps assocMaps;
    assocMaps.owners = createDefaultOwnerAssociation();
    assocMaps.ifaces = createDefaultInterfaceAssociation(server);

    removeAssociation(io, defaultSourcePath, defaultDbusSvc, *server,
                      assocMaps);

    // Verify owner association was deleted
    EXPECT_TRUE(assocMaps.owners.empty());

    // Verify the interface associations without endpoints were deleted
    EXPECT_FALSE(assocMaps.ifaces.contains(defaultFwdPath));
    EXPECT_FALSE(assocMaps.ifaces.contains(defaultRevPath));
}

// Verify call when path is in associated interfaces, with extra endpoints
TEST_F(TestAssociations, PathIsInAssociatedInterfacesExtraEndpoints)
{
    // Build up these objects so that an associated interface will match
    // with the associated owner being removed
    AssociationMaps assocMaps;
    assocMaps.owners = createDefaultOwnerAssociation();
    assocMaps.ifaces = createDefaultInterfaceAssociation(server);

    // Add another endpoint to the assoc interfaces
    addEndpointToInterfaceAssociation(assocMaps.ifaces);

    removeAssociation(io, defaultSourcePath, defaultDbusSvc, *server,
                      assocMaps);

    // Verify owner association was deleted
    EXPECT_TRUE(assocMaps.owners.empty());

    // Verify that only the endpoint owned by the removed source path was
    // deleted from the forward association
    auto fwd = assocMaps.ifaces.find(defaultFwdPath);
    ASSERT_NE(fwd, assocMaps.ifaces.end());
    EXPECT_THAT(std::get<endpointsPos>(fwd->second),
                testing::ElementsAre(extraEndpoint));

    // The reverse association had a single endpoint and is gone
    EXPECT_FALSE(assocMaps.ifaces.contains(defaultRevPath));
}

// Verify no associations or endpoints removed when the change is identical
TEST_F(TestAssociations, checkAssociationEndpointRemovesNoEpRemove)
{
    AssociationPaths newAssocPaths = {{defaultFwdPath, {defaultEndpoint}},
                                      {defaultRevPath, {defaultSourcePath}}};

    AssociationMaps assocMaps;
    assocMaps.owners = createDefaultOwnerAssociation();
    assocMaps.ifaces = createDefaultInterfaceAssociation(server);

    checkAssociationEndpointRemoves(io, defaultSourcePath, defaultDbusSvc,
                                    newAssocPaths, *server, assocMaps);

    // Verify endpoints were not deleted because they matche with what was
    // in the original
    auto intfEndpoints =
        std::get<endpointsPos>(assocMaps.ifaces[defaultFwdPath]);
    EXPECT_EQ(intfEndpoints.size(), 1);
    intfEndpoints = std::get<endpointsPos>(assocMaps.ifaces[defaultRevPath]);
    EXPECT_EQ(intfEndpoints.size(), 1);
}

// Verify endpoint is removed when assoc path is different
TEST_F(TestAssociations, checkAssociationEndpointRemovesEpRemoveApDiff)
{
    AssociationPaths newAssocPaths = {{"/different/path", {defaultEndpoint}}};

    AssociationMaps assocMaps;
    assocMaps.owners = createDefaultOwnerAssociation();
    assocMaps.ifaces = createDefaultInterfaceAssociation(server);

    checkAssociationEndpointRemoves(io, defaultSourcePath, defaultDbusSvc,
                                    newAssocPaths, *server, assocMaps);

    // Verify initial endpoints were deleted because the new path
    auto intfEndpoints =
        std::get<endpointsPos>(assocMaps.ifaces[defaultFwdPath]);
    EXPECT_EQ(intfEndpoints.size(), 0);
    intfEndpoints = std::get<endpointsPos>(assocMaps.ifaces[defaultRevPath]);
    EXPECT_EQ(intfEndpoints.size(), 0);
}

// Verify endpoint is removed when endpoint is different
TEST_F(TestAssociations, checkAssociationEndpointRemovesEpRemoveEpChanged)
{
    AssociationPaths newAssocPaths = {
        {defaultFwdPath, {defaultEndpoint + "/different"}},
        {defaultRevPath, {defaultSourcePath + "/different"}}};

    AssociationMaps assocMaps;
    assocMaps.owners = createDefaultOwnerAssociation();
    assocMaps.ifaces = createDefaultInterfaceAssociation(server);

    checkAssociationEndpointRemoves(io, defaultSourcePath, defaultDbusSvc,
                                    newAssocPaths, *server, assocMaps);

    // Verify initial endpoints were deleted because of different endpoints
    auto intfEndpoints =
        std::get<endpointsPos>(assocMaps.ifaces[defaultFwdPath]);
    EXPECT_EQ(intfEndpoints.size(), 0);
    intfEndpoints = std::get<endpointsPos>(assocMaps.ifaces[defaultRevPath]);
    EXPECT_EQ(intfEndpoints.size(), 0);
}

// Verify existing endpoint deleted when empty endpoint is provided
TEST_F(TestAssociations, associationChangedEmptyEndpoint)
{
    std::vector<Association> associations = {
        {"inventory_cee", "error_cee", ""}};
    InterfaceMapType interfaceMap;

    AssociationMaps assocMaps;
    assocMaps.owners = createDefaultOwnerAssociation();
    assocMaps.ifaces = createDefaultInterfaceAssociation(server);

    // Empty endpoint will result in deletion of corresponding assocInterface
    associationChanged(io, *server, associations, defaultSourcePath,
                       defaultDbusSvc, interfaceMap, assocMaps);

    // Both of these should be 0 since we have an invalid endpoint
    auto intfEndpoints =
        std::get<endpointsPos>(assocMaps.ifaces[defaultFwdPath]);
    EXPECT_EQ(intfEndpoints.size(), 0);
    intfEndpoints = std::get<endpointsPos>(assocMaps.ifaces[defaultRevPath]);
    EXPECT_EQ(intfEndpoints.size(), 0);

    EXPECT_EQ(assocMaps.pending.size(), 0);
}

// Add a new association with endpoint
TEST_F(TestAssociations, associationChangedAddNewAssoc)
{
    std::vector<Association> associations = {
        {"abc", "def", "/xyz/openbmc_project/new/endpoint"}};

    AssociationMaps assocMaps;
    assocMaps.owners = createDefaultOwnerAssociation();
    assocMaps.ifaces = createDefaultInterfaceAssociation(server);

    // Make it look like the assoc endpoints are on D-Bus
    InterfaceMapType interfaceMap = {
        {"/new/source/path", {{defaultDbusSvc, {"a"}}}},
        {"/xyz/openbmc_project/new/endpoint", {{defaultDbusSvc, {"a"}}}}};

    associationChanged(io, *server, associations,
                       sdbusplus::object_path("/new/source/path"),
                       defaultDbusSvc, interfaceMap, assocMaps);

    // Two source paths
    EXPECT_EQ(assocMaps.owners.size(), 2);

    // Four interfaces
    EXPECT_EQ(assocMaps.ifaces.size(), 4);

    // Nothing pending
    EXPECT_EQ(assocMaps.pending.size(), 0);

    // New endpoint so assocMaps.ifaces should be same size
    auto intfEndpoints =
        std::get<endpointsPos>(assocMaps.ifaces[defaultFwdPath]);
    EXPECT_EQ(intfEndpoints.size(), 1);
}

// Add a new association to empty objects
TEST_F(TestAssociations, associationChangedAddNewAssocEmptyObj)
{
    std::vector<Association> associations = {
        {"inventory_canaeo", "error_canaeo",
         "/xyz/openbmc_project/inventory/system/chassis"}};

    // Empty objects because this test will ensure assocOwners adds the
    // changed association and interface
    AssociationMaps assocMaps;

    // Make it look like the assoc endpoints are on D-Bus
    InterfaceMapType interfaceMap = createDefaultInterfaceMap();

    associationChanged(io, *server, associations, defaultSourcePath,
                       defaultDbusSvc, interfaceMap, assocMaps);

    // New associations so ensure it now contains a single entry
    EXPECT_EQ(assocMaps.owners.size(), 1);

    // Nothing pending
    EXPECT_EQ(assocMaps.pending.size(), 0);

    // Verify corresponding assoc paths each have one endpoint in assoc
    // interfaces and that those endpoints match
    auto singleOwner = assocMaps.owners[defaultSourcePath];
    auto singleIntf = singleOwner[defaultDbusSvc];
    for (auto i : singleIntf)
    {
        auto intfEndpoints = std::get<endpointsPos>(assocMaps.ifaces[i.first]);
        EXPECT_EQ(intfEndpoints.size(), 1);
        EXPECT_EQ(intfEndpoints[0], *i.second.begin());
    }
}

// Add a new association to same source path but with new owner
TEST_F(TestAssociations, associationChangedAddNewAssocNewOwner)
{
    std::string newOwner = "xyz.openbmc_project.Test2";
    std::vector<Association> associations = {
        {"inventory_canano", "error_canano",
         "/xyz/openbmc_project/inventory/system/chassis"}};

    // Make it look like the assoc endpoints are on D-Bus
    InterfaceMapType interfaceMap = createDefaultInterfaceMap();

    AssociationMaps assocMaps;
    assocMaps.owners = createDefaultOwnerAssociation();
    assocMaps.ifaces = createDefaultInterfaceAssociation(server);

    associationChanged(io, *server, associations, defaultSourcePath, newOwner,
                       interfaceMap, assocMaps);

    // New endpoint so assocOwners should be same size
    EXPECT_EQ(assocMaps.owners.size(), 1);

    // Ensure only one endpoint under first path
    auto intfEndpoints =
        std::get<endpointsPos>(assocMaps.ifaces[defaultFwdPath]);
    EXPECT_EQ(intfEndpoints.size(), 1);

    // Ensure the 2 new association endpoints are under the new owner
    auto a = assocMaps.owners.find(defaultSourcePath);
    auto o = a->second.find(newOwner);
    EXPECT_EQ(o->second.size(), 2);

    // Nothing pending
    EXPECT_EQ(assocMaps.pending.size(), 0);
}

// Add a new association to existing interface path
TEST_F(TestAssociations, associationChangedAddNewAssocSameInterface)
{
    std::vector<Association> associations = {
        {"abc", "error", "/xyz/openbmc_project/inventory/system/chassis"}};

    // Make it look like the assoc endpoints are on D-Bus
    InterfaceMapType interfaceMap = createDefaultInterfaceMap();

    AssociationMaps assocMaps;
    assocMaps.owners = createDefaultOwnerAssociation();
    assocMaps.ifaces = createDefaultInterfaceAssociation(server);

    associationChanged(io, *server, associations, defaultSourcePath,
                       defaultDbusSvc, interfaceMap, assocMaps);

    // Should have 2 entries in AssociationInterfaces
    // The one missing an endpoint is not added.
    EXPECT_EQ(assocMaps.ifaces.size(), 2);

    // Change to existing interface so it will be removed here
    auto intfEndpoints =
        std::get<endpointsPos>(assocMaps.ifaces[defaultFwdPath]);
    EXPECT_EQ(intfEndpoints.size(), 0);

    // The new endpoint should exist though in it's place
    intfEndpoints = std::get<endpointsPos>(
        assocMaps.ifaces[defaultSourcePath + "/" + "abc"]);
    EXPECT_EQ(intfEndpoints.size(), 1);

    // Added to an existing owner path so still 1
    EXPECT_EQ(assocMaps.owners.size(), 1);

    EXPECT_EQ(assocMaps.pending.size(), 0);
}

// Add 2 pending associations
TEST_F(TestAssociations, addPendingAssocs)
{
    AssociationMaps assocMaps;

    addPendingAssociation(defaultSourcePath, "inventory", defaultEndpoint,
                          "error", defaultDbusSvc, assocMaps);

    EXPECT_TRUE(assocMaps.ifaces.empty());
    EXPECT_TRUE(assocMaps.owners.empty());

    EXPECT_EQ(assocMaps.pending.size(), 1);

    addPendingAssociation("some/other/path", "inventory", defaultEndpoint,
                          "error", defaultDbusSvc, assocMaps);

    EXPECT_TRUE(assocMaps.ifaces.empty());
    EXPECT_TRUE(assocMaps.owners.empty());

    EXPECT_EQ(assocMaps.pending.size(), 2);
}

// Test adding a new endpoint to a pending association
TEST_F(TestAssociations, addPendingAssocsNewEndpoints)
{
    AssociationMaps assocMaps;

    addPendingAssociation(defaultSourcePath, "inventory", defaultEndpoint,
                          "error", defaultDbusSvc, assocMaps);

    EXPECT_EQ(assocMaps.pending.size(), 1);

    addPendingAssociation(defaultSourcePath, "inventory", "some/other/endpoint",
                          "error", defaultDbusSvc, assocMaps);

    // Same pending path, so still just 1 entry
    EXPECT_EQ(assocMaps.pending.size(), 1);

    auto assoc = assocMaps.pending.find(defaultSourcePath);
    EXPECT_NE(assoc, assocMaps.pending.end());

    auto& endpoints = assoc->second;
    EXPECT_EQ(endpoints.size(), 2);
}

// Test adding a new owner to a pending association
TEST_F(TestAssociations, addPendingAssocsNewOwner)
{
    AssociationMaps assocMaps;

    addPendingAssociation(defaultSourcePath, "inventory", defaultEndpoint,
                          "error", defaultDbusSvc, assocMaps);

    EXPECT_EQ(assocMaps.pending.size(), 1);

    addPendingAssociation(defaultSourcePath, "inventory", defaultEndpoint,
                          "error", "new owner", assocMaps);

    EXPECT_EQ(assocMaps.pending.size(), 1);

    auto assoc = assocMaps.pending.find(defaultSourcePath);
    EXPECT_NE(assoc, assocMaps.pending.end());

    auto& endpoints = assoc->second;
    EXPECT_EQ(endpoints.size(), 2);
}

// Add a pending association inside associationChanged
TEST_F(TestAssociations, associationChangedPending)
{
    std::vector<Association> associations = {
        {"abc", "def", "/xyz/openbmc_project/new/endpoint"}};

    AssociationMaps assocMaps;
    InterfaceMapType interfaceMap;

    associationChanged(io, *server, associations,
                       sdbusplus::object_path("/new/source/path"),
                       defaultDbusSvc, interfaceMap, assocMaps);

    // No associations were actually added
    EXPECT_EQ(assocMaps.owners.size(), 0);
    EXPECT_EQ(assocMaps.ifaces.size(), 0);

    // 1 pending association
    EXPECT_EQ(assocMaps.pending.size(), 1);
}

// Test removing pending associations
TEST_F(TestAssociations, testRemoveFromPendingAssociations)
{
    AssociationMaps assocMaps;

    addPendingAssociation(defaultSourcePath, "inventory", defaultEndpoint,
                          "error", defaultDbusSvc, assocMaps);

    addPendingAssociation(defaultSourcePath, "inventory", "some/other/endpoint",
                          "error", defaultDbusSvc, assocMaps);

    EXPECT_EQ(assocMaps.pending.size(), 1);

    removeFromPendingAssociations("some/other/endpoint", defaultDbusSvc,
                                  assocMaps);

    // Still 1 pending entry, but down to 1 endpoint
    EXPECT_EQ(assocMaps.pending.size(), 1);

    auto assoc = assocMaps.pending.find(defaultSourcePath);
    EXPECT_NE(assoc, assocMaps.pending.end());
    auto& endpoints = assoc->second;
    EXPECT_EQ(endpoints.size(), 1);

    // Now nothing pending
    removeFromPendingAssociations(defaultEndpoint, defaultDbusSvc, assocMaps);
    EXPECT_EQ(assocMaps.pending.size(), 0);
}

// Test removing pending associations by multiple owners
TEST_F(TestAssociations, testRemoveFromPendingAssociationsMultipleOwners)
{
    AssociationMaps assocMaps;

    addPendingAssociation(defaultSourcePath, "inventory", defaultEndpoint,
                          "error", "serviceA", assocMaps);

    addPendingAssociation(defaultSourcePath, "inventory", defaultEndpoint,
                          "error", "serviceB", assocMaps);

    EXPECT_EQ(assocMaps.pending.size(), 1);

    removeFromPendingAssociations(defaultEndpoint, "serviceB", assocMaps);

    EXPECT_EQ(assocMaps.pending.size(), 1);

    auto assoc = assocMaps.pending.find(defaultSourcePath);
    EXPECT_NE(assoc, assocMaps.pending.end());
    auto& endpoints = assoc->second;
    EXPECT_EQ(endpoints.size(), 1);
    EXPECT_EQ(std::get<ownerPos>(endpoints[0]), "serviceA");
}

// Test moving a pending association to a real one
TEST_F(TestAssociations, checkIfPending)
{
    AssociationMaps assocMaps;
    InterfaceMapType interfaceMap = {
        {defaultSourcePath, {{defaultDbusSvc, {"a"}}}},
        {defaultEndpoint, {{defaultDbusSvc, {"b"}}}}};

    addPendingAssociation(defaultSourcePath, "inventory_cip", defaultEndpoint,
                          "error_cip", defaultDbusSvc, assocMaps);
    EXPECT_EQ(assocMaps.pending.size(), 1);

    // Move the pending association to a real association
    checkIfPendingAssociation(io, defaultSourcePath, interfaceMap, assocMaps,
                              *server);

    EXPECT_TRUE(assocMaps.pending.empty());
    EXPECT_EQ(assocMaps.owners.size(), 1);
    EXPECT_EQ(assocMaps.ifaces.size(), 2);

    // This shouldn't do anything, since /new/path isn't pending
    checkIfPendingAssociation(io, sdbusplus::object_path("/new/path"),
                              interfaceMap, assocMaps, *server);
    EXPECT_TRUE(assocMaps.pending.empty());
    EXPECT_EQ(assocMaps.owners.size(), 1);
    EXPECT_EQ(assocMaps.ifaces.size(), 2);
}

TEST_F(TestAssociations, findAssociations)
{
    std::vector<std::tuple<std::string, Association>> associationData;
    AssociationMaps assocMaps;

    assocMaps.owners = {
        {"pathA",
         {{"ownerA",
           {{"pathA/typeA", {"endpointA", "endpointB"}},
            {"endpointA/type0", {"pathA"}}}}}},

        {"pathJ",
         {{"ownerC",
           {{"pathJ/typeA", {"endpointF"}}, {"endpointF/type0", {"pathJ"}}}}}},

        {"pathX",
         {{"ownerB",
           {{"pathX/typeB", {"endpointA"}}, {"endpointA/type1", {"pathX"}}}}}}};

    findAssociations("endpointA", assocMaps, associationData);
    ASSERT_EQ(associationData.size(), 2);

    {
        auto it = std::find_if(
            associationData.begin(), associationData.end(),
            [](const auto& ad) { return std::get<0>(ad) == "ownerA"; });
        ASSERT_NE(it, associationData.end());

        auto& a = std::get<1>(*it);
        ASSERT_EQ(std::get<0>(a), "type0");
        ASSERT_EQ(std::get<1>(a), "typeA");
        ASSERT_EQ(std::get<2>(a), "pathA");
    }
    {
        auto it = std::find_if(
            associationData.begin(), associationData.end(),
            [](const auto& ad) { return std::get<0>(ad) == "ownerB"; });
        ASSERT_NE(it, associationData.end());

        auto& a = std::get<1>(*it);
        ASSERT_EQ(std::get<0>(a), "type1");
        ASSERT_EQ(std::get<1>(a), "typeB");
        ASSERT_EQ(std::get<2>(a), "pathX");
    }
}

TEST_F(TestAssociations, moveAssocToPendingNoOp)
{
    AssociationMaps assocMaps;

    // Not an association, so it shouldn't do anything
    moveAssociationToPending(io, defaultEndpoint, assocMaps, *server);

    EXPECT_TRUE(assocMaps.pending.empty());
    EXPECT_TRUE(assocMaps.owners.empty());
    EXPECT_TRUE(assocMaps.ifaces.empty());
}

TEST_F(TestAssociations, moveAssocToPending)
{
    AssociationMaps assocMaps;
    assocMaps.owners = createDefaultOwnerAssociation();
    assocMaps.ifaces = createDefaultInterfaceAssociation(server);

    moveAssociationToPending(io, defaultEndpoint, assocMaps, *server);

    // Check it's now pending
    EXPECT_EQ(assocMaps.pending.size(), 1);
    EXPECT_EQ(assocMaps.pending.begin()->first, defaultEndpoint);

    // No more assoc owners
    EXPECT_TRUE(assocMaps.owners.empty());

    // Check the association interfaces were removed
    EXPECT_FALSE(assocMaps.ifaces.contains(defaultFwdPath));
    EXPECT_FALSE(assocMaps.ifaces.contains(defaultRevPath));
}

// Creating an association object with an invalid D-Bus object path must not
// throw or leave a failed entry in assocMaps.ifaces.
TEST_F(TestAssociations, addSingleAssociationInvalidPath)
{
    AssociationMaps assocMaps;

    EXPECT_NO_THROW(
        addSingleAssociation(io, *server, "//invalid_asaip", defaultEndpoint,
                             defaultDbusSvc, defaultSourcePath, assocMaps));
    EXPECT_TRUE(assocMaps.ifaces.empty());
}

// Associations that would result in an invalid object path must be ignored
TEST_F(TestAssociations, associationChangedInvalidType)
{
    std::vector<Association> associations = {
        {"fan-sensor", "chassis_ici", defaultEndpoint},
        {"inventory_ici", "has/slash", defaultEndpoint},
        {"inventory_ici", "error_ici", "not/an/object/path"}};

    AssociationMaps assocMaps;
    InterfaceMapType interfaceMap = createDefaultInterfaceMap();

    EXPECT_NO_THROW(
        associationChanged(io, *server, associations, defaultSourcePath,
                           defaultDbusSvc, interfaceMap, assocMaps));

    EXPECT_TRUE(assocMaps.owners.empty());
    EXPECT_TRUE(assocMaps.ifaces.empty());
    EXPECT_TRUE(assocMaps.pending.empty());
}

// When no associationOwners entry exists yet for ownerPath,
// addSingleAssociation() must create one under ownerPath rather than endpoint.
TEST_F(TestAssociations, addSingleAssociationOwnerKey)
{
    AssociationMaps assocMaps;

    addSingleAssociation(io, *server, defaultSourcePath + "/fwd_asaok",
                         defaultEndpoint, defaultDbusSvc, defaultSourcePath,
                         assocMaps);

    EXPECT_TRUE(assocMaps.owners.contains(defaultSourcePath));
    EXPECT_FALSE(assocMaps.owners.contains(defaultEndpoint));
}

// A pending association must result in the same association object paths
// as an association for which the endpoint exists, also if the association
// type starts with an underscore.
TEST_F(TestAssociations, checkIfPendingSamePaths)
{
    const std::string endpoint = "/xyz/openbmc_project/new/endpoint_cipsp";
    std::vector<Association> associations = {
        {"_fwd_cipsp", "_rev_cipsp", endpoint}};

    AssociationMaps assocMaps;
    InterfaceMapType interfaceMap = createDefaultInterfaceMap();

    associationChanged(io, *server, associations, defaultSourcePath,
                       defaultDbusSvc, interfaceMap, assocMaps);
    EXPECT_EQ(assocMaps.pending.size(), 1);

    interfaceMap[endpoint][defaultDbusSvc] = {"b"};
    checkIfPendingAssociation(io, sdbusplus::object_path(endpoint),
                              interfaceMap, assocMaps, *server);

    EXPECT_TRUE(assocMaps.pending.empty());
    ASSERT_EQ(assocMaps.ifaces.size(), 2);
    EXPECT_TRUE(assocMaps.ifaces.contains(defaultSourcePath + "/_fwd_cipsp"));
    EXPECT_TRUE(assocMaps.ifaces.contains(endpoint + "/_rev_cipsp"));
}

// A pending association with an empty type must only create and record the
// association object for the non-empty direction once the endpoint shows up.
TEST_F(TestAssociations, checkIfPendingEmptyType)
{
    const std::string endpoint1 = "/xyz/openbmc_project/new/endpoint1_cipet";
    const std::string endpoint2 = "/xyz/openbmc_project/new/endpoint2_cipet";
    std::vector<Association> associations = {{"fwd_cipet", "", endpoint1},
                                             {"", "rev_cipet", endpoint2}};

    AssociationMaps assocMaps;
    InterfaceMapType interfaceMap = createDefaultInterfaceMap();

    associationChanged(io, *server, associations, defaultSourcePath,
                       defaultDbusSvc, interfaceMap, assocMaps);
    EXPECT_EQ(assocMaps.pending.size(), 2);

    // Complete the association with an empty reverse type.
    interfaceMap[endpoint1][defaultDbusSvc] = {"b"};
    checkIfPendingAssociation(io, sdbusplus::object_path(endpoint1),
                              interfaceMap, assocMaps, *server);

    EXPECT_EQ(assocMaps.pending.size(), 1);
    ASSERT_EQ(assocMaps.ifaces.size(), 1);
    EXPECT_TRUE(assocMaps.ifaces.contains(defaultSourcePath + "/fwd_cipet"));
    ASSERT_TRUE(assocMaps.owners.contains(defaultSourcePath));
    ASSERT_TRUE(assocMaps.owners[defaultSourcePath].contains(defaultDbusSvc));
    EXPECT_EQ(assocMaps.owners[defaultSourcePath][defaultDbusSvc].size(), 1);
    EXPECT_TRUE(assocMaps.owners[defaultSourcePath][defaultDbusSvc].contains(
        defaultSourcePath + "/fwd_cipet"));

    // Complete the association with an empty forward type.
    interfaceMap[endpoint2][defaultDbusSvc] = {"b"};
    checkIfPendingAssociation(io, sdbusplus::object_path(endpoint2),
                              interfaceMap, assocMaps, *server);

    EXPECT_TRUE(assocMaps.pending.empty());
    ASSERT_EQ(assocMaps.ifaces.size(), 2);
    EXPECT_TRUE(assocMaps.ifaces.contains(endpoint2 + "/rev_cipet"));
    EXPECT_EQ(assocMaps.owners[defaultSourcePath][defaultDbusSvc].size(), 2);
    EXPECT_TRUE(assocMaps.owners[defaultSourcePath][defaultDbusSvc].contains(
        endpoint2 + "/rev_cipet"));
    EXPECT_FALSE(assocMaps.owners.contains(endpoint1));
    EXPECT_FALSE(assocMaps.owners.contains(endpoint2));

    removeAssociation(io, defaultSourcePath, defaultDbusSvc, *server,
                      assocMaps);
    EXPECT_TRUE(assocMaps.ifaces.empty());
    EXPECT_TRUE(assocMaps.owners.empty());
}

// Removing the associations of a path for which all associations are pending
// must also remove the pending associations.
TEST_F(TestAssociations, removeAssociationOnlyPending)
{
    std::vector<Association> associations = {
        {"callout_raop", "fault_raop", "/xyz/openbmc_project/missing_raop"}};

    AssociationMaps assocMaps;
    InterfaceMapType interfaceMap = createDefaultInterfaceMap();

    associationChanged(io, *server, associations, defaultSourcePath,
                       defaultDbusSvc, interfaceMap, assocMaps);
    EXPECT_TRUE(assocMaps.owners.empty());
    EXPECT_EQ(assocMaps.pending.size(), 1);

    removeAssociation(io, defaultSourcePath, defaultDbusSvc, *server,
                      assocMaps);
    EXPECT_TRUE(assocMaps.pending.empty());
}

// Removing the association owner after its endpoint has disappeared must
// also remove the pending association that moveAssociationToPending() created.
TEST_F(TestAssociations, removeAssociationAfterMoveToPending)
{
    std::vector<Association> associations = {
        {"callout_ramtp", "fault_ramtp", defaultEndpoint}};

    AssociationMaps assocMaps;
    InterfaceMapType interfaceMap = createDefaultInterfaceMap();

    associationChanged(io, *server, associations, defaultSourcePath,
                       defaultDbusSvc, interfaceMap, assocMaps);
    EXPECT_EQ(assocMaps.ifaces.size(), 2);

    moveAssociationToPending(io, defaultEndpoint, assocMaps, *server);
    EXPECT_TRUE(assocMaps.owners.empty());
    EXPECT_EQ(assocMaps.pending.size(), 1);

    removeAssociation(io, defaultSourcePath, defaultDbusSvc, *server,
                      assocMaps);
    EXPECT_TRUE(assocMaps.pending.empty());
}

// Clearing the associations property must remove pending associations
TEST_F(TestAssociations, associationChangedClearsPending)
{
    std::vector<Association> associations = {
        {"callout_accp", "fault_accp", "/xyz/openbmc_project/missing_accp"}};

    AssociationMaps assocMaps;
    InterfaceMapType interfaceMap = createDefaultInterfaceMap();

    associationChanged(io, *server, associations, defaultSourcePath,
                       defaultDbusSvc, interfaceMap, assocMaps);
    EXPECT_EQ(assocMaps.pending.size(), 1);

    associationChanged(io, *server, {}, defaultSourcePath, defaultDbusSvc,
                       interfaceMap, assocMaps);
    EXPECT_TRUE(assocMaps.pending.empty());
}

// Clearing the associations property must remove the associationOwners
// entry such that the association is not resurrected later on.
TEST_F(TestAssociations, associationChangedClearsOwners)
{
    std::vector<Association> associations = {
        {"inventory_acco", "error_acco", defaultEndpoint}};

    AssociationMaps assocMaps;
    InterfaceMapType interfaceMap = createDefaultInterfaceMap();

    associationChanged(io, *server, associations, defaultSourcePath,
                       defaultDbusSvc, interfaceMap, assocMaps);
    EXPECT_EQ(assocMaps.owners.size(), 1);
    EXPECT_EQ(assocMaps.ifaces.size(), 2);

    associationChanged(io, *server, {}, defaultSourcePath, defaultDbusSvc,
                       interfaceMap, assocMaps);
    EXPECT_TRUE(assocMaps.owners.empty());
    EXPECT_TRUE(assocMaps.ifaces.empty());

    // The endpoint disappearing must not create a pending association
    moveAssociationToPending(io, defaultEndpoint, assocMaps, *server);
    EXPECT_TRUE(assocMaps.pending.empty());
}

// Removing some associations from the associations property must only
// remove the association objects and pending associations of the removed
// associations.
TEST_F(TestAssociations, associationChangedPartialUpdate)
{
    const std::string missing = "/xyz/openbmc_project/missing_acpu";
    const Association present{"inventory_acpu", "error_acpu", defaultEndpoint};
    const Association absent{"callout_acpu", "fault_acpu", missing};

    AssociationMaps assocMaps;
    InterfaceMapType interfaceMap = createDefaultInterfaceMap();

    associationChanged(io, *server, {present, absent}, defaultSourcePath,
                       defaultDbusSvc, interfaceMap, assocMaps);
    EXPECT_EQ(assocMaps.ifaces.size(), 2);
    EXPECT_EQ(assocMaps.pending.size(), 1);

    // Remove the association with the missing endpoint
    associationChanged(io, *server, {present}, defaultSourcePath,
                       defaultDbusSvc, interfaceMap, assocMaps);
    EXPECT_EQ(assocMaps.ifaces.size(), 2);
    EXPECT_TRUE(assocMaps.pending.empty());
    EXPECT_TRUE(assocMaps.pendingByOwner.empty());

    const std::string missing2 = "/xyz/openbmc_project/missing2_acpu";

    // Replace the association with the existing endpoint by two pending
    // associations so partial completion and owner removal both exercise
    // pendingByOwner.
    associationChanged(io, *server, {absent, {"f3_acpu", "r3_acpu", missing2}},
                       defaultSourcePath, defaultDbusSvc, interfaceMap,
                       assocMaps);
    EXPECT_TRUE(assocMaps.ifaces.empty());
    EXPECT_TRUE(assocMaps.owners.empty());
    EXPECT_EQ(assocMaps.pending.size(), 2);
    EXPECT_EQ(assocMaps.pendingByOwner.size(), 1);

    // Make the first missing endpoint show up
    interfaceMap[missing][defaultDbusSvc] = {"b"};
    checkIfPendingAssociation(io, sdbusplus::object_path(missing), interfaceMap,
                              assocMaps, *server);
    EXPECT_EQ(assocMaps.pending.size(), 1);
    EXPECT_EQ(assocMaps.pendingByOwner.size(), 1);
    EXPECT_EQ(assocMaps.ifaces.size(), 2);

    removeAssociation(io, defaultSourcePath, defaultDbusSvc, *server,
                      assocMaps);
    EXPECT_TRUE(assocMaps.pending.empty());
    EXPECT_TRUE(assocMaps.pendingByOwner.empty());
    EXPECT_TRUE(assocMaps.ifaces.empty());
}
