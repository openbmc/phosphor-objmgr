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
