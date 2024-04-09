#pragma once

#include "types.hpp"

#include <string>
#include <vector>

void addObjectMapResult(std::vector<InterfaceMapType::value_type>& objectMap,
                        const std::string& objectPath,
                        const ConnectionNames::value_type& interfaceMap);

std::vector<InterfaceMapType::value_type>
    getAncestors(const InterfaceMapType& interfaceMap, std::string reqPath,
                 std::vector<std::string>& interfaces);

ConnectionNames getObject(const InterfaceMapType& interfaceMap,
                          const std::string& path,
                          std::vector<std::string>& interfaces);

std::vector<InterfaceMapType::value_type>
    getSubTree(const InterfaceMapType& interfaceMap, std::string reqPath,
               int32_t depth, std::vector<std::string>& interfaces);

std::vector<std::string> getSubTreePaths(const InterfaceMapType& interfaceMap,
                                         std::string reqPath, int32_t depth,
                                         std::vector<std::string>& interfaces);

/**
 * @brief Get the Associated Sub Tree object
 *
 * @param interfaceMap     Mapper Structure storing all associations
 * @param associationMaps  Map of association between objects
 * @param associationPath  Object path to get the endpoint from
 * @param reqPath          Base path to search for the subtree
 * @param depth            Level of depth to search into the base path
 * @param interfaces       Interface filter
 *
 * Use getSubTree and return only the dbus objects that are in the endpoint
 * of associationPath.
 *
 * @return std::vector<InterfaceMapType::value_type>
 */
std::vector<InterfaceMapType::value_type>
    getAssociatedSubTree(const InterfaceMapType& interfaceMap,
                         const AssociationMaps& associationMaps,
                         const sdbusplus::message::object_path& associationPath,
                         const sdbusplus::message::object_path& reqPath,
                         int32_t depth, std::vector<std::string>& interfaces);

/**
 * @brief Get the Associated Sub Tree Paths object
 *
 * @param interfaceMap     Mapper Structure storing all associations
 * @param associationMaps  Map of association between objects
 * @param associationPath  Object path to get the endpoint from
 * @param reqPath          Base path to search for the subtree
 * @param depth            Level of depth to search into the base path
 * @param interfaces       Interface filter
 *
 * Use getSubTreePaths and return only the dbus objects that are in the
 * endpoint of associationPath.
 *
 * @return std::vector<std::string>
 */
std::vector<std::string> getAssociatedSubTreePaths(
    const InterfaceMapType& interfaceMap,
    const AssociationMaps& associationMaps,
    const sdbusplus::message::object_path& associationPath,
    const sdbusplus::message::object_path& reqPath, int32_t depth,
    std::vector<std::string>& interfaces);

/**
 * @brief Get the Associated Sub Tree Paths object by id
 *
 * @param interfaceMap       Mapper Structure storing all associations
 * @param associationMaps    Map of association between objects
 * @param id                 Identifier to search for the subtree
 * @param objectPath         Base path to search for the subtree
 * @param subtreeInterfaces  Interface filter for the subtree
 * @param association        The endpoint association
 * @param endpointInterfaces Interface filter for the endpoint association
 *
 * Use getAssociatedSubTree and return only the dbus objects that
 * are associated with the provided identifier, filtering based on on their
 * endpoint association.
 *
 * @return std::vector<InterfaceMapType::value_type>
 */
std::vector<InterfaceMapType::value_type> getAssociatedSubTreeById(
    const InterfaceMapType& interfaceMap,
    const AssociationMaps& associationMaps, const std::string& id,
    const std::string& objectPath, std::vector<std::string>& subtreeInterfaces,
    const std::string& association,
    std::vector<std::string>& endpointInterfaces);

/**
 * @brief Get the Associated Sub Tree Paths object by id
 *
 * @param interfaceMap       Mapper Structure storing all associations
 * @param associationMaps    Map of association between objects
 * @param id                 Identifier to search for the subtree
 * @param objectPath         Base path to search for the subtree
 * @param subtreeInterfaces  Interface filter for the subtree
 * @param association        The endpoint association
 * @param endpointInterfaces Interface filter for the endpoint association
 *
 * Use getAssociatedSubTreePaths and return only the dbus objects that
 * are associated with the provided identifier, filtering based on on their
 * endpoint association.
 *
 * @return std::vector<std::string>
 */
std::vector<std::string> getAssociatedSubTreePathsById(
    const InterfaceMapType& interfaceMap,
    const AssociationMaps& associationMaps, const std::string& id,
    const std::string& objectPath, std::vector<std::string>& subtreeInterfaces,
    const std::string& association,
    std::vector<std::string>& endpointInterfaces);
