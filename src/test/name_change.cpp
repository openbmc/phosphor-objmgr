#include "src/processing.hpp"
#include "src/test/util/asio_server_class.hpp"
#include "src/test/util/association_objects.hpp"

#include <gtest/gtest.h>

class TestNameChange : public AsioServerClassTest
{
  public:
    boost::asio::io_context io;
    virtual void SetUp() override
    {
        io.run();
    }
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
    InterfaceMapType interfaceMap;
    AssociationMaps assocMaps;

    processNameChangeDelete(io, nameOwners, wellKnown, oldOwner, interfaceMap,
                            assocMaps, *server);
    EXPECT_EQ(nameOwners.size(), 0);
}

// Verify path removed from interface map and association objects
TEST_F(TestNameChange, UniqueNameAssociationsAndInterface)
{
    boost::container::flat_map<std::string, std::string> nameOwners = {
        {":1.99", defaultDbusSvc}};
    std::string oldOwner = {":1.99"};
    InterfaceNames assocInterfacesSet = {AssociationDefinitions::interface};

    // Build up these objects so that an associated interface will match
    // with the associated owner being removed
    AssociationMaps assocMaps;
    assocMaps.owners = createDefaultOwnerAssociation();
    assocMaps.ifaces = createDefaultInterfaceAssociation(server);
    auto interfaceMap = createInterfaceMap(defaultSourcePath, defaultDbusSvc,
                                           assocInterfacesSet);

    processNameChangeDelete(io, nameOwners, defaultDbusSvc, oldOwner,
                            interfaceMap, assocMaps, *server);
    EXPECT_EQ(nameOwners.size(), 0);

    // Verify owner association was deleted
    EXPECT_TRUE(assocMaps.owners.empty());

    // Verify endpoint was deleted from interface association
    auto intfEndpoints =
        std::get<endpointsPos>(assocMaps.ifaces[defaultFwdPath]);
    EXPECT_EQ(intfEndpoints.size(), 0);
    intfEndpoints = std::get<endpointsPos>(assocMaps.ifaces[defaultRevPath]);
    EXPECT_EQ(intfEndpoints.size(), 0);

    // Verify interface map was deleted
    EXPECT_TRUE(interfaceMap.empty());
}
