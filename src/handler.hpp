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
 * @param associationMaps  Map of assocition between objects
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
 * @param associationMaps  Map of assocition between objects
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