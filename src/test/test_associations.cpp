#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <src/associations.hpp>

#include <gtest/gtest.h>

class TestAssociations : public testing::Test
{
  public:
    TestAssociations()
    {
        boost::asio::io_context io;
        auto conn = std::make_shared<sdbusplus::asio::connection>(io);

        conn->request_name("xyz.openbmc_project.ObjMgr.Test");
        server = new sdbusplus::asio::object_server(conn);
    }

    ~TestAssociations()
    {
    }

    sdbusplus::asio::object_server* server;
};

sdbusplus::asio::object_server getServer()
{
}

// Create a default AssociationOwnersType object with input path and service
AssociationOwnersType createOwnerAssociation(const std::string path,
                                             const std::string service)
{
    boost::container::flat_set<std::string> set = {"/system/cpu0"};
    AssociationPaths assocPath = {{"/logging/entry/1/callout", set}};
    boost::container::flat_map<std::string, AssociationPaths> serviceMap = {
        {service, assocPath}};
    AssociationOwnersType ownerAssoc = {{path, serviceMap}};
    return ownerAssoc;
}

// Verify call when path is not in associated owners
TEST_F(TestAssociations, SourcePathNotInAssociations)
{
    std::string sourcePath = "/xyz/openbmc_project/no/association";
    std::string owner = "xyz.openbmc_project.Test";
    AssociationOwnersType assocOwners;
    AssociationInterfaces assocInterfaces;

    removeAssociation(sourcePath, owner, *server, assocOwners, assocInterfaces);
}

// Verify call when owner is not in associated owners
TEST_F(TestAssociations, OnwerNotInAssociations)
{
    std::string sourcePath = "/logging/entry/1";
    std::string owner = "xyz.openbmc_project.Test";
    AssociationInterfaces assocInterfaces;

    auto assocOwners = createOwnerAssociation("/logging/entry/1",
                                              "xyz.openbmc_project.Logging");

    removeAssociation(sourcePath, owner, *server, assocOwners, assocInterfaces);
}
