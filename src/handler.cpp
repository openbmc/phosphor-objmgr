#include "handler.hpp"

#include "types.hpp"

#include <xyz/openbmc_project/Common/error.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

void addObjectMapResult(std::vector<InterfaceMapType::value_type>& objectMap,
                        const std::string& objectPath,
                        const ConnectionNames::value_type& interfaceMap)
{
    // Adds an object path/service name/interface list entry to
    // the results of GetSubTree and GetAncestors.
    // If an entry for the object path already exists, just add the
    // service name and interfaces to that entry, otherwise create
    // a new entry.
    auto entry = std::find_if(
        objectMap.begin(), objectMap.end(),
        [&objectPath](const auto& i) { return objectPath == i.first; });

    if (entry != objectMap.end())
    {
        entry->second.emplace(interfaceMap);
    }
    else
    {
        InterfaceMapType::value_type object;
        object.first = objectPath;
        object.second.emplace(interfaceMap);
        objectMap.push_back(object);
    }
}

std::vector<InterfaceMapType::value_type>
    getAncestors(const InterfaceMapType& interfaceMap, std::string reqPath,
                 std::vector<std::string>& interfaces)
{
    // Interfaces need to be sorted for intersect to function
    std::sort(interfaces.begin(), interfaces.end());

    if (reqPath.ends_with("/"))
    {
        reqPath.pop_back();
    }
    if (!reqPath.empty() && interfaceMap.find(reqPath) == interfaceMap.end())
    {
        throw sdbusplus::xyz::openbmc_project::Common::Error::
            ResourceNotFound();
    }

    std::vector<InterfaceMapType::value_type> ret;
    for (const auto& objectPath : interfaceMap)
    {
        const auto& thisPath = objectPath.first;

        if (reqPath == thisPath)
        {
            continue;
        }

        if (reqPath.starts_with(thisPath))
        {
            if (interfaces.empty())
            {
                ret.emplace_back(objectPath);
            }
            else
            {
                for (const auto& interfaceMap : objectPath.second)
                {
                    std::vector<std::string> output(std::min(
                        interfaces.size(), interfaceMap.second.size()));
                    // Return iterator points at the first output elemtn,
                    // meaning that there are no intersections.
                    if (std::set_intersection(interfaces.begin(),
                                              interfaces.end(),
                                              interfaceMap.second.begin(),
                                              interfaceMap.second.end(),
                                              output.begin()) != output.begin())
                    {
                        addObjectMapResult(ret, thisPath, interfaceMap);
                    }
                }
            }
        }
    }

    return ret;
}

ConnectionNames getObject(const InterfaceMapType& interfaceMap,
                          const std::string& path,
                          std::vector<std::string>& interfaces)
{
    ConnectionNames results;

    // Interfaces need to be sorted for intersect to function
    std::sort(interfaces.begin(), interfaces.end());
    auto pathRef = interfaceMap.find(path);
    if (pathRef == interfaceMap.end())
    {
        throw sdbusplus::xyz::openbmc_project::Common::Error::
            ResourceNotFound();
    }
    if (interfaces.empty())
    {
        return pathRef->second;
    }
    for (const auto& interfaceMap : pathRef->second)
    {
        std::vector<std::string> output(
            std::min(interfaces.size(), interfaceMap.second.size()));
        // Return iterator points at the first output elemtn,
        // meaning that there are no intersections.
        if (std::set_intersection(interfaces.begin(), interfaces.end(),
                                  interfaceMap.second.begin(),
                                  interfaceMap.second.end(),
                                  output.begin()) != output.begin())
        {
            results.emplace(interfaceMap.first, interfaceMap.second);
        }
    }

    if (results.empty())
    {
        throw sdbusplus::xyz::openbmc_project::Common::Error::
            ResourceNotFound();
    }

    return results;
}

std::vector<InterfaceMapType::value_type>
    getSubTree(const InterfaceMapType& interfaceMap, std::string reqPath,
               int32_t depth, std::vector<std::string>& interfaces)
{
    if (depth <= 0)
    {
        depth = std::numeric_limits<int32_t>::max();
    }
    // Interfaces need to be sorted for intersect to function
    std::sort(interfaces.begin(), interfaces.end());

    // reqPath is now guaranteed to have a trailing "/" while reqPathStripped
    // will be guaranteed not to have a trailing "/"
    if (!reqPath.ends_with("/"))
    {
        reqPath += "/";
    }
    std::string_view reqPathStripped =
        std::string_view(reqPath).substr(0, reqPath.size() - 1);

    if (!reqPathStripped.empty() &&
        interfaceMap.find(reqPathStripped) == interfaceMap.end())
    {
        throw sdbusplus::xyz::openbmc_project::Common::Error::
            ResourceNotFound();
    }

    std::vector<InterfaceMapType::value_type> ret;
    for (const auto& objectPath : interfaceMap)
    {
        const auto& thisPath = objectPath.first;

        // Skip exact match on stripped search term
        if (thisPath == reqPathStripped)
        {
            continue;
        }

        if (thisPath.starts_with(reqPath))
        {
            // count the number of slashes past the stripped search term
            int32_t thisDepth = std::count(
                thisPath.begin() + reqPathStripped.size(), thisPath.end(), '/');
            if (thisDepth <= depth)
            {
                for (const auto& interfaceMap : objectPath.second)
                {
                    std::vector<std::string> output(std::min(
                        interfaces.size(), interfaceMap.second.size()));
                    // Return iterator points at the first output elemtn,
                    // meaning that there are no intersections.
                    if (std::set_intersection(
                            interfaces.begin(), interfaces.end(),
                            interfaceMap.second.begin(),
                            interfaceMap.second.end(),
                            output.begin()) != output.begin() ||
                        interfaces.empty())
                    {
                        addObjectMapResult(ret, thisPath, interfaceMap);
                    }
                }
            }
        }
    }

    return ret;
}

std::vector<std::string> getSubTreePaths(const InterfaceMapType& interfaceMap,
                                         std::string reqPath, int32_t depth,
                                         std::vector<std::string>& interfaces)
{
    if (depth <= 0)
    {
        depth = std::numeric_limits<int32_t>::max();
    }
    // Interfaces need to be sorted for intersect to function
    std::sort(interfaces.begin(), interfaces.end());

    // reqPath is now guaranteed to have a trailing "/" while reqPathStripped
    // will be guaranteed not to have a trailing "/"
    if (!reqPath.ends_with("/"))
    {
        reqPath += "/";
    }
    std::string_view reqPathStripped =
        std::string_view(reqPath).substr(0, reqPath.size() - 1);

    if (!reqPathStripped.empty() &&
        interfaceMap.find(reqPathStripped) == interfaceMap.end())
    {
        throw sdbusplus::xyz::openbmc_project::Common::Error::
            ResourceNotFound();
    }

    std::vector<std::string> ret;
    for (const auto& objectPath : interfaceMap)
    {
        const auto& thisPath = objectPath.first;

        // Skip exact match on stripped search term
        if (thisPath == reqPathStripped)
        {
            continue;
        }

        if (thisPath.starts_with(reqPath))
        {
            // count the number of slashes past the stripped search term
            int thisDepth = std::count(
                thisPath.begin() + reqPathStripped.size(), thisPath.end(), '/');
            if (thisDepth <= depth)
            {
                bool add = interfaces.empty();
                for (const auto& interfaceMap : objectPath.second)
                {
                    std::vector<std::string> output(std::min(
                        interfaces.size(), interfaceMap.second.size()));
                    // Return iterator points at the first output elemtn,
                    // meaning that there are no intersections.
                    if (std::set_intersection(interfaces.begin(),
                                              interfaces.end(),
                                              interfaceMap.second.begin(),
                                              interfaceMap.second.end(),
                                              output.begin()) != output.begin())
                    {
                        add = true;
                        break;
                    }
                }
                if (add)
                {
                    // TODO(ed) this is a copy
                    ret.emplace_back(thisPath);
                }
            }
        }
    }

    return ret;
}
