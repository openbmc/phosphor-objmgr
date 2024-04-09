#include "handler.hpp"

#include "types.hpp"

#include <xyz/openbmc_project/Common/error.hpp>

#include <algorithm>
#include <string>
#include <unordered_set>
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
                    if (std::set_intersection(
                            interfaces.begin(), interfaces.end(),
                            interfaceMap.second.begin(),
                            interfaceMap.second.end(), output.begin()) !=
                        output.begin())
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
                                  interfaceMap.second.end(), output.begin()) !=
            output.begin())
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

std::vector<std::string>
    getSubTreePaths(const InterfaceMapType& interfaceMap, std::string reqPath,
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
                    if (std::set_intersection(
                            interfaces.begin(), interfaces.end(),
                            interfaceMap.second.begin(),
                            interfaceMap.second.end(), output.begin()) !=
                        output.begin())
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

std::vector<InterfaceMapType::value_type> getAssociatedSubTree(
    const InterfaceMapType& interfaceMap,
    const AssociationMaps& associationMaps,
    const sdbusplus::message::object_path& associationPath,
    const sdbusplus::message::object_path& reqPath, int32_t depth,
    std::vector<std::string>& interfaces)
{
    auto findEndpoint = associationMaps.ifaces.find(associationPath.str);
    if (findEndpoint == associationMaps.ifaces.end())
    {
        return {};
    }
    const std::vector<std::string>& association =
        std::get<endpointsPos>(findEndpoint->second);
    std::unordered_set<std::string> associationSet(association.begin(),
                                                   association.end());
    const std::vector<InterfaceMapType::value_type>& interfacePairs =
        getSubTree(interfaceMap, reqPath, depth, interfaces);

    std::vector<InterfaceMapType::value_type> output;
    for (const InterfaceMapType::value_type& interfacePair : interfacePairs)
    {
        if (associationSet.contains(interfacePair.first))
        {
            output.emplace_back(interfacePair);
        }
    }
    return output;
}

std::vector<std::string> getAssociatedSubTreePaths(
    const InterfaceMapType& interfaceMap,
    const AssociationMaps& associationMaps,
    const sdbusplus::message::object_path& associationPath,
    const sdbusplus::message::object_path& reqPath, int32_t depth,
    std::vector<std::string>& interfaces)
{
    auto findEndpoint = associationMaps.ifaces.find(associationPath.str);
    if (findEndpoint == associationMaps.ifaces.end())
    {
        return {};
    }
    const std::vector<std::string>& association =
        std::get<endpointsPos>(findEndpoint->second);
    std::unordered_set<std::string> associationSet(association.begin(),
                                                   association.end());
    const std::vector<std::string>& paths =
        getSubTreePaths(interfaceMap, reqPath, depth, interfaces);

    std::vector<std::string> output;
    for (const auto& path : paths)
    {
        if (associationSet.contains(path))
        {
            output.emplace_back(path);
        }
    }
    return output;
}

// This function works like getSubTreePaths() but only matching id with
// the leaf-name instead of full path.
std::vector<std::string> getSubTreePathsById(
    const InterfaceMapType& interfaceMap, const std::string& id,
    const std::string& objectPath, std::vector<std::string>& interfaces)
{
    std::sort(interfaces.begin(), interfaces.end());

    std::string localObjectPath = objectPath;

    if (!localObjectPath.ends_with("/"))
    {
        localObjectPath += "/";
    }
    std::string_view objectPathStripped =
        std::string_view(localObjectPath).substr(0, localObjectPath.size() - 1);

    if (!objectPathStripped.empty() &&
        interfaceMap.find(objectPathStripped) == interfaceMap.end())
    {
        throw sdbusplus::xyz::openbmc_project::Common::Error::
            ResourceNotFound();
    }

    std::vector<std::string> output;
    for (const auto& path : interfaceMap)
    {
        const auto& thisPath = path.first;

        // Skip exact match on stripped search term or
        // the path does not end with the id
        if (thisPath == objectPathStripped || !thisPath.ends_with("/" + id))
        {
            continue;
        }

        if (thisPath.starts_with(objectPath))
        {
            for (const auto& interfaceMap : path.second)
            {
                std::vector<std::string> tempoutput(
                    std::min(interfaces.size(), interfaceMap.second.size()));
                if (std::set_intersection(
                        interfaces.begin(), interfaces.end(),
                        interfaceMap.second.begin(), interfaceMap.second.end(),
                        tempoutput.begin()) != tempoutput.begin())
                {
                    output.emplace_back(thisPath);
                    break;
                }
            }
        }
    }
    if (output.empty())
    {
        throw sdbusplus::xyz::openbmc_project::Common::Error::
            ResourceNotFound();
    }
    return output;
}

std::vector<InterfaceMapType::value_type> getAssociatedSubTreeById(
    const InterfaceMapType& interfaceMap,
    const AssociationMaps& associationMaps, const std::string& id,
    const std::string& objectPath, std::vector<std::string>& subtreeInterfaces,
    const std::string& association,
    std::vector<std::string>& endpointInterfaces)
{
    const std::vector<std::string>& subtreePaths =
        getSubTreePathsById(interfaceMap, id, objectPath, subtreeInterfaces);

    std::vector<InterfaceMapType::value_type> output;
    for (const auto& subtreePath : subtreePaths)
    {
        // Form the association path
        std::string associationPathStr = subtreePath + "/" + association;
        sdbusplus::message::object_path associationPath(associationPathStr);

        auto associatedSubTree =
            getAssociatedSubTree(interfaceMap, associationMaps, associationPath,
                                 objectPath, 0, endpointInterfaces);

        output.insert(output.end(), associatedSubTree.begin(),
                      associatedSubTree.end());
    }
    return output;
}

std::vector<std::string> getAssociatedSubTreePathsById(
    const InterfaceMapType& interfaceMap,
    const AssociationMaps& associationMaps, const std::string& id,
    const std::string& objectPath, std::vector<std::string>& subtreeInterfaces,
    const std::string& association,
    std::vector<std::string>& endpointInterfaces)
{
    const std::vector<std::string>& subtreePaths =
        getSubTreePathsById(interfaceMap, id, objectPath, subtreeInterfaces);
    std::vector<std::string> output;
    for (const auto& subtreePath : subtreePaths)
    {
        // Form the association path
        std::string associationPathStr = subtreePath + "/" + association;
        sdbusplus::message::object_path associationPath(associationPathStr);

        auto associatedSubTree = getAssociatedSubTreePaths(
            interfaceMap, associationMaps, associationPath, objectPath, 0,
            endpointInterfaces);

        output.insert(output.end(), associatedSubTree.begin(),
                      associatedSubTree.end());
    }

    return output;
}
