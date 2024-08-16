#include "src/handler.hpp"

#include "src/types.hpp"

#include <xyz/openbmc_project/Common/error.hpp>

#include <span>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::ElementsAre;

class TestHandler : public testing::Test
{
  protected:
    InterfaceMapType interfaceMap = {
        {
            "/test/object_path_0",
            {{"test_object_connection_0", {"test_interface_0"}}},
        },
        {
            "/test/object_path_0/child",
            {{"test_object_connection_1", {"test_interface_1"}}},
        },
        {
            "/test/object_path_0/child/grandchild",
            {{"test_object_connection_2", {"test_interface_2"}}},
        },
        {
            "/test/object_path_0/child/grandchild/dog",
            {{"test_object_connection_3", {"test_interface_3"}}},
        }};

    AssociationMaps associationMap = {
        .ifaces =
            {
                {
                    "/test/object_path_0/descendent",
                    {
                        std::shared_ptr<sdbusplus::asio::dbus_interface>(),
                        {
                            "/test/object_path_0/child",
                            "/test/object_path_0/child/grandchild",
                        },
                    },
                },
                {
                    "/test/object_path_0/child/descendent",
                    {
                        std::shared_ptr<sdbusplus::asio::dbus_interface>(),
                        {
                            "/test/object_path_0/child/grandchild",
                        },
                    },
                },
            },
        .owners = {},
        .pending = {},
    };
};

TEST_F(TestHandler, AddObjectMapResult)
{
    std::vector<InterfaceMapType::value_type> interfaceMaps;
    addObjectMapResult(interfaceMaps, "test_object_path",
                       std::pair<std::string, InterfaceNames>(
                           "test_object_connection_0", {
                                                           "test_interface_0",
                                                           "test_interface_1",
                                                       }));

    addObjectMapResult(interfaceMaps, "test_object_path",
                       std::pair<std::string, InterfaceNames>(
                           "test_object_connection_1", {
                                                           "test_interface_0",
                                                           "test_interface_1",
                                                       }));
    ASSERT_EQ(interfaceMaps.size(), 1);

    auto entry = std::find_if(
        interfaceMaps.begin(), interfaceMaps.end(),
        [](const auto& i) { return "test_object_path" == i.first; });
    ASSERT_NE(entry, interfaceMap.end());
    for (const auto& [_, interfaces] : entry->second)
    {
        ASSERT_THAT(interfaces,
                    ElementsAre("test_interface_0", "test_interface_1"));
    }

    // Change the interface, but expect it to be unchanged
    addObjectMapResult(interfaceMaps, "test_object_path",
                       std::pair<std::string, InterfaceNames>(
                           "test_object_connection_0", {"test_interface_2"}));
    addObjectMapResult(interfaceMaps, "test_object_path",
                       std::pair<std::string, InterfaceNames>(
                           "test_object_connection_1", {"test_interface_2"}));
    entry = std::find_if(
        interfaceMaps.begin(), interfaceMaps.end(),
        [](const auto& i) { return "test_object_path" == i.first; });
    ASSERT_NE(entry, interfaceMaps.end());
    for (const auto& [_, interfaces] : entry->second)
    {
        ASSERT_THAT(interfaces,
                    ElementsAre("test_interface_0", "test_interface_1"));
    }
}

TEST_F(TestHandler, getAncestorsBad)
{
    std::string path = "/test/object_path_0/child/grandchild";
    std::vector<std::string> interfaces = {"bad_interface"};
    std::vector<InterfaceMapType::value_type> ancestors =
        getAncestors(interfaceMap, path, interfaces);
    ASSERT_TRUE(ancestors.empty());

    path = "/invalid_path";
    EXPECT_THROW(
        getAncestors(interfaceMap, path, interfaces),
        sdbusplus::xyz::openbmc_project::Common::Error::ResourceNotFound);
}

TEST_F(TestHandler, getAncestorsGood)
{
    std::string path = "/test/object_path_0/child/grandchild";
    std::vector<std::string> interfaces = {"test_interface_0",
                                           "test_interface_1"};
    std::vector<InterfaceMapType::value_type> ancestors =
        getAncestors(interfaceMap, path, interfaces);
    ASSERT_EQ(ancestors.size(), 2);

    // Grand Parent
    EXPECT_EQ(ancestors[0].first, "/test/object_path_0");
    ASSERT_EQ(ancestors[0].second.size(), 1);
    auto grandParent = ancestors[0].second.find("test_object_connection_0");
    ASSERT_NE(grandParent, ancestors[0].second.end());
    ASSERT_THAT(grandParent->second, ElementsAre("test_interface_0"));

    // Parent
    ASSERT_EQ(ancestors[1].first, "/test/object_path_0/child");
    ASSERT_EQ(ancestors[1].second.size(), 1);
    auto parent = ancestors[1].second.find("test_object_connection_1");
    ASSERT_NE(parent, ancestors[1].second.end());
    ASSERT_THAT(parent->second, ElementsAre("test_interface_1"));
}

TEST_F(TestHandler, getObjectBad)
{
    std::string path = "/test/object_path_0";
    std::vector<std::string> interfaces = {"bad_interface"};
    EXPECT_THROW(
        getObject(interfaceMap, path, interfaces),
        sdbusplus::xyz::openbmc_project::Common::Error::ResourceNotFound);

    path = "/invalid_path";
    EXPECT_THROW(
        getObject(interfaceMap, path, interfaces),
        sdbusplus::xyz::openbmc_project::Common::Error::ResourceNotFound);

    path = "/";
    EXPECT_THROW(
        getObject(interfaceMap, path, interfaces),
        sdbusplus::xyz::openbmc_project::Common::Error::ResourceNotFound);
}

TEST_F(TestHandler, getObjectGood)
{
    std::string path = "/test/object_path_0";
    std::vector<std::string> interfaces = {"test_interface_0",
                                           "test_interface_1"};
    ConnectionNames connection = getObject(interfaceMap, path, interfaces);
    auto object = connection.find("test_object_connection_0");
    ASSERT_NE(object, connection.end());
    ASSERT_THAT(object->second, ElementsAre("test_interface_0"));

    path = "/test/object_path_0/child";
    connection = getObject(interfaceMap, path, interfaces);
    object = connection.find("test_object_connection_1");
    ASSERT_NE(object, connection.end());
    ASSERT_THAT(object->second, ElementsAre("test_interface_1"));
}

TEST_F(TestHandler, getSubTreeBad)
{
    std::string path = "/test/object_path_0";
    std::vector<std::string> interfaces = {"bad_interface"};
    std::vector<InterfaceMapType::value_type> subtree =
        getSubTree(interfaceMap, path, 0, interfaces);
    ASSERT_TRUE(subtree.empty());

    path = "/invalid_path";
    EXPECT_THROW(
        getSubTree(interfaceMap, path, 0, interfaces),
        sdbusplus::xyz::openbmc_project::Common::Error::ResourceNotFound);
}

void verifySubtree(std::span<InterfaceMapType::value_type> subtree)
{
    ASSERT_EQ(subtree.size(), 2);
    ConnectionNames connection = subtree[0].second;
    auto object = connection.find("test_object_connection_1");
    ASSERT_NE(object, connection.end());
    ASSERT_THAT(object->second, ElementsAre("test_interface_1"));

    connection = subtree[1].second;
    object = connection.find("test_object_connection_3");
    ASSERT_NE(object, connection.end());
    ASSERT_THAT(object->second, ElementsAre("test_interface_3"));
}

TEST_F(TestHandler, getSubTreeGood)
{
    std::string path0 = "/test/object_path_0";
    std::string path1 = "/test/object_path_0/child/grandchild";
    std::vector<std::string> interfaces = {"test_interface_1",
                                           "test_interface_3"};
    // Root
    std::vector<InterfaceMapType::value_type> subtree =
        getSubTree(interfaceMap, "/", 0, interfaces);
    verifySubtree(subtree);

    // Path0
    subtree = getSubTree(interfaceMap, path0, 0, interfaces);
    verifySubtree(subtree);

    // Path0 with Depth path of 1
    subtree = getSubTree(interfaceMap, path0, 1, interfaces);
    ASSERT_EQ(subtree.size(), 1);
    ConnectionNames connection = subtree[0].second;
    auto object = connection.find("test_object_connection_1");
    ASSERT_NE(object, connection.end());
    ASSERT_THAT(object->second, ElementsAre("test_interface_1"));

    // Path1
    subtree = getSubTree(interfaceMap, path1, 0, interfaces);
    ASSERT_EQ(subtree.size(), 1);
    connection = subtree[0].second;
    object = connection.find("test_object_connection_3");
    ASSERT_NE(object, connection.end());
    ASSERT_THAT(object->second, ElementsAre("test_interface_3"));
}

TEST_F(TestHandler, getSubTreePathsBad)
{
    std::string path = "/test/object_path_0";
    std::vector<std::string> interfaces = {"bad_interface"};
    std::vector<std::string> subtreePath =
        getSubTreePaths(interfaceMap, path, 0, interfaces);
    ASSERT_TRUE(subtreePath.empty());

    path = "/invalid_path";
    EXPECT_THROW(
        getSubTreePaths(interfaceMap, path, 0, interfaces),
        sdbusplus::xyz::openbmc_project::Common::Error::ResourceNotFound);
}

TEST_F(TestHandler, getSubTreePathsGood)
{
    std::string path0 = "/test/object_path_0";
    std::string path1 = "/test/object_path_0/child/grandchild";
    std::vector<std::string> interfaces = {"test_interface_1",
                                           "test_interface_3"};
    // Root
    std::vector<std::string> subtreePath =
        getSubTreePaths(interfaceMap, "/", 0, interfaces);
    ASSERT_THAT(subtreePath,
                ElementsAre("/test/object_path_0/child",
                            "/test/object_path_0/child/grandchild/dog"));

    // Path0
    subtreePath = getSubTreePaths(interfaceMap, path0, 0, interfaces);
    ASSERT_THAT(subtreePath,
                ElementsAre("/test/object_path_0/child",
                            "/test/object_path_0/child/grandchild/dog"));

    // Path0 + Depth path of 1
    subtreePath = getSubTreePaths(interfaceMap, path0, 1, interfaces);
    ASSERT_THAT(subtreePath, ElementsAre("/test/object_path_0/child"));

    // Path1
    subtreePath = getSubTreePaths(interfaceMap, path1, 0, interfaces);
    ASSERT_THAT(subtreePath,
                ElementsAre("/test/object_path_0/child/grandchild/dog"));
}

TEST_F(TestHandler, getAssociatedSubTreeBad)
{
    sdbusplus::message::object_path path("/test/object_path_0");
    sdbusplus::message::object_path validAssociatedPath = path / "descendent";
    std::vector<std::string> invalidInterfaces = {"test_interface_3"};
    std::vector<std::string> validInterfaces = {"test_interface_1",
                                                "test_interface_2"};
    // Associated path, but invalid interface
    ASSERT_TRUE(
        getAssociatedSubTree(interfaceMap, associationMap, validAssociatedPath,
                             path, 0, invalidInterfaces)
            .empty());

    // Valid interface, not associated
    ASSERT_TRUE(getAssociatedSubTree(interfaceMap, associationMap, path / "dog",
                                     path, 0, validInterfaces)
                    .empty());

    // Invalid path, with valid association
    path = sdbusplus::message::object_path("/invalid_path");
    EXPECT_THROW(
        getAssociatedSubTree(interfaceMap, associationMap, validAssociatedPath,
                             path, 0, validInterfaces),
        sdbusplus::xyz::openbmc_project::Common::Error::ResourceNotFound);
}

TEST_F(TestHandler, getAssociatedSubTreeGood)
{
    sdbusplus::message::object_path path0("/test/object_path_0");
    sdbusplus::message::object_path path1("/test/object_path_0/child");
    sdbusplus::message::object_path associatedPath = path0 / "descendent";
    std::vector<std::string> interfaces = {
        "test_interface_1", "test_interface_2",
        // Not associated to path
        "test_interface_3"};

    // Path0
    std::vector<InterfaceMapType::value_type> subtree = getAssociatedSubTree(
        interfaceMap, associationMap, associatedPath, path0, 0, interfaces);
    ASSERT_EQ(subtree.size(), 2);
    ConnectionNames connection = subtree[0].second;
    auto object = connection.find("test_object_connection_1");
    ASSERT_NE(object, connection.end());
    ASSERT_THAT(object->second, ElementsAre("test_interface_1"));

    connection = subtree[1].second;
    object = connection.find("test_object_connection_2");
    ASSERT_NE(object, connection.end());
    ASSERT_THAT(object->second, ElementsAre("test_interface_2"));

    // Path0 with Depth path of 1
    subtree = getAssociatedSubTree(interfaceMap, associationMap, associatedPath,
                                   path0, 1, interfaces);
    ASSERT_EQ(subtree.size(), 1);
    connection = subtree[0].second;
    object = connection.find("test_object_connection_1");
    ASSERT_NE(object, connection.end());
    ASSERT_THAT(object->second, ElementsAre("test_interface_1"));

    // Path1
    subtree = getAssociatedSubTree(interfaceMap, associationMap,
                                   path1 / "descendent", path1, 0, interfaces);
    ASSERT_EQ(subtree.size(), 1);
    connection = subtree[0].second;
    object = connection.find("test_object_connection_2");
    ASSERT_NE(object, connection.end());
    ASSERT_THAT(object->second, ElementsAre("test_interface_2"));
}

TEST_F(TestHandler, getAssociatedSubTreePathsBad)
{
    sdbusplus::message::object_path path("/test/object_path_0");
    sdbusplus::message::object_path validAssociatedPath = path / "descendent";
    std::vector<std::string> invalidInterfaces = {"test_interface_3"};
    std::vector<std::string> validInterfaces = {"test_interface_1",
                                                "test_interface_2"};
    // Associated path, but invalid interface
    ASSERT_TRUE(getAssociatedSubTreePaths(interfaceMap, associationMap,
                                          validAssociatedPath, path, 0,
                                          invalidInterfaces)
                    .empty());

    // Valid interface, not associated
    ASSERT_TRUE(
        getAssociatedSubTreePaths(interfaceMap, associationMap, path / "dog",
                                  path, 0, validInterfaces)
            .empty());

    // Invalid path, with valid association
    path = sdbusplus::message::object_path("/invalid_path");
    EXPECT_THROW(
        getAssociatedSubTreePaths(interfaceMap, associationMap,
                                  validAssociatedPath, path, 0,
                                  validInterfaces),
        sdbusplus::xyz::openbmc_project::Common::Error::ResourceNotFound);
}

TEST_F(TestHandler, getAssociatedSubTreePathsGood)
{
    sdbusplus::message::object_path path0("/test/object_path_0");
    sdbusplus::message::object_path path1("/test/object_path_0/child");
    sdbusplus::message::object_path associatedPath = path0 / "descendent";
    std::vector<std::string> interfaces = {
        "test_interface_1", "test_interface_2",
        // Not associated to path
        "test_interface_3"};

    // Path0
    std::vector<std::string> subtreePath = getAssociatedSubTreePaths(
        interfaceMap, associationMap, associatedPath, path0, 0, interfaces);
    ASSERT_THAT(subtreePath,
                ElementsAre("/test/object_path_0/child",
                            "/test/object_path_0/child/grandchild"));

    // Path0 with Depth path of 1
    subtreePath = getAssociatedSubTreePaths(
        interfaceMap, associationMap, associatedPath, path0, 1, interfaces);
    ASSERT_THAT(subtreePath, ElementsAre("/test/object_path_0/child"));

    // Path1
    subtreePath =
        getAssociatedSubTreePaths(interfaceMap, associationMap,
                                  path1 / "descendent", path1, 0, interfaces);
    ASSERT_THAT(subtreePath,
                ElementsAre("/test/object_path_0/child/grandchild"));
}
