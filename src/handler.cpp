#include "handler.hpp"

#include "path.hpp"
#include "types.hpp"

#include <xyz/openbmc_project/Common/error.hpp>

#include <algorithm>
#include <cassert>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

// Whether or not two sorted ranges have at least one element in common. This
// is faster than std::set_intersection() because no output is generated.
static bool intersects(const std::vector<std::string>& interfaces,
                       const InterfaceNames& objectInterfaces)
{
    auto a = interfaces.begin();
    auto b = objectInterfaces.begin();
    while (a != interfaces.end() && b != objectInterfaces.end())
    {
        if (*a < *b)
        {
            ++a;
        }
        else if (*b < *a)
        {
            ++b;
        }
        else
        {
            return true;
        }
    }
    return false;
}

void addObjectMapResult(std::vector<InterfaceMapType::value_type>& objectMap,
                        const std::string& objectPath,
                        const ConnectionNames::value_type& interfaceMap)
{
    // Adds an object path/service name/interface list entry to
    // the results of GetSubTree and GetAncestors.
    // If an entry for the object path already exists, just add the
    // service name and interfaces to that entry, otherwise create
    // a new entry.
    //
    // The callers add entries in sorted object path order. Hence, if an
    // entry for objectPath exists, it is the last entry. Only checking the
    // last entry avoids quadratic behavior.
    assert(objectMap.empty() || objectMap.back().first <= objectPath);
    if (!objectMap.empty() && objectMap.back().first == objectPath)
    {
        objectMap.back().second.emplace(interfaceMap);
        return;
    }
    InterfaceMapType::value_type object;
    object.first = objectPath;
    object.second.emplace(interfaceMap);
    objectMap.push_back(std::move(object));
}

std::vector<InterfaceMapType::value_type> getAncestors(
    const InterfaceMapType& interfaceMap, std::string reqPath,
    std::vector<std::string>& interfaces)
{
    // Interfaces need to be sorted for intersect to function
    std::sort(interfaces.begin(), interfaces.end());

    if (reqPath.ends_with("/"))
    {
        reqPath.pop_back();
    }
    if (!reqPath.empty() && !interfaceMap.contains(reqPath))
    {
        throw sdbusplus::xyz::openbmc_project::Common::Error::
            ResourceNotFound();
    }

    // Look up the ancestors of reqPath instead of iterating over the entire
    // interface map. Walking up the path components also guarantees that
    // e.g. /a/b is not reported as an ancestor of /a/bc.
    std::vector<InterfaceMapType::const_iterator> ancestors;
    std::string_view path = reqPath;
    // The root path has no ancestors.
    while (!path.empty() && path != "/")
    {
        auto pos = path.rfind('/');
        if (pos == std::string_view::npos)
        {
            break;
        }
        path = pos == 0 ? std::string_view("/") : path.substr(0, pos);
        auto it = interfaceMap.find(path);
        if (it != interfaceMap.end())
        {
            ancestors.push_back(it);
        }
    }
    // Report the ancestors in sorted order, starting with the root.
    std::reverse(ancestors.begin(), ancestors.end());

    std::vector<InterfaceMapType::value_type> ret;
    for (const auto& ancestor : ancestors)
    {
        const auto& objectPath = *ancestor;
        const auto& thisPath = objectPath.first;

        if (interfaces.empty())
        {
            ret.emplace_back(objectPath);
            continue;
        }
        for (const auto& connectionInterfaces : objectPath.second)
        {
            if (intersects(interfaces, connectionInterfaces.second))
            {
                addObjectMapResult(ret, thisPath, connectionInterfaces);
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
    for (const auto& connectionInterfaces : pathRef->second)
    {
        if (intersects(interfaces, connectionInterfaces.second))
        {
            results.emplace(connectionInterfaces.first,
                            connectionInterfaces.second);
        }
    }

    if (results.empty())
    {
        throw sdbusplus::xyz::openbmc_project::Common::Error::
            ResourceNotFound();
    }

    return results;
}

std::vector<InterfaceMapType::value_type> getSubTree(
    const InterfaceMapType& interfaceMap, std::string reqPath, int32_t depth,
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

    std::vector<InterfaceMapType::value_type> ret;
    // All paths that start with reqPath follow reqPath in the sorted
    // interface map. Hence, there is no need to examine other paths.
    for (auto objectIt = interfaceMap.lower_bound(reqPath);
         objectIt != interfaceMap.end() && objectIt->first.starts_with(reqPath);
         ++objectIt)
    {
        const auto& objectPath = *objectIt;
        const auto& thisPath = objectPath.first;

        // Skip exact match on stripped search term
        if (thisPath == reqPathStripped)
        {
            continue;
        }

        if (thisPath.starts_with(reqPath))
        {
            // count the number of slashes past the stripped search term
            auto thisDepth = std::count(
                thisPath.begin() + std::distance(reqPathStripped.begin(),
                                                 reqPathStripped.end()),
                thisPath.end(), '/');
            if (thisDepth <= depth)
            {
                for (const auto& connectionInterfaces : objectPath.second)
                {
                    if (interfaces.empty() ||
                        intersects(interfaces, connectionInterfaces.second))
                    {
                        addObjectMapResult(ret, thisPath, connectionInterfaces);
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
    // All paths that start with reqPath follow reqPath in the sorted
    // interface map. Hence, there is no need to examine other paths.
    for (auto objectIt = interfaceMap.lower_bound(reqPath);
         objectIt != interfaceMap.end() && objectIt->first.starts_with(reqPath);
         ++objectIt)
    {
        const auto& objectPath = *objectIt;
        const auto& thisPath = objectPath.first;

        // Skip exact match on stripped search term
        if (thisPath == reqPathStripped)
        {
            continue;
        }

        if (thisPath.starts_with(reqPath))
        {
            // count the number of slashes past the stripped search term
            auto thisDepth = std::count(
                thisPath.begin() + std::distance(reqPathStripped.begin(),
                                                 reqPathStripped.end()),
                thisPath.end(), '/');
            if (thisDepth <= depth)
            {
                bool add = interfaces.empty();
                for (const auto& connectionInterfaces : objectPath.second)
                {
                    if (intersects(interfaces, connectionInterfaces.second))
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
    const sdbusplus::object_path& associationPath,
    const sdbusplus::object_path& reqPath, int32_t depth,
    std::vector<std::string>& interfaces)
{
    auto findEndpoint = associationMaps.ifaces.find(associationPath);
    if (findEndpoint == associationMaps.ifaces.end())
    {
        return {};
    }
    const std::vector<std::string>& association =
        std::get<endpointsPos>(findEndpoint->second);
    std::unordered_set<std::string> associationSet(association.begin(),
                                                   association.end());
    const std::vector<InterfaceMapType::value_type> interfacePairs =
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
    const sdbusplus::object_path& associationPath,
    const sdbusplus::object_path& reqPath, int32_t depth,
    std::vector<std::string>& interfaces)
{
    auto findEndpoint = associationMaps.ifaces.find(associationPath);
    if (findEndpoint == associationMaps.ifaces.end())
    {
        return {};
    }
    const std::vector<std::string>& association =
        std::get<endpointsPos>(findEndpoint->second);
    std::unordered_set<std::string> associationSet(association.begin(),
                                                   association.end());
    const std::vector<std::string> paths =
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
static std::vector<std::string> getSubTreePathsById(
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

    bool validId = false;
    std::vector<std::string> output;
    for (const auto& path : interfaceMap)
    {
        const auto& thisPath = path.first;

        // Skip the path does not end with the id
        if (!thisPath.ends_with("/" + id))
        {
            continue;
        }

        // Valid if id is matching
        validId = true;

        // Skip exact match on stripped search term
        if (thisPath == objectPathStripped)
        {
            continue;
        }
        if (thisPath.starts_with(localObjectPath))
        {
            for (const auto& connectionInterfaces : path.second)
            {
                if (intersects(interfaces, connectionInterfaces.second))
                {
                    output.emplace_back(thisPath);
                    break;
                }
            }
        }
    }
    if (!validId)
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
    std::vector<std::string> subtreePaths =
        getSubTreePathsById(interfaceMap, id, objectPath, subtreeInterfaces);

    std::vector<InterfaceMapType::value_type> output;
    for (const auto& subtreePath : subtreePaths)
    {
        // Form the association path
        std::string associationPathStr =
            appendPathSegment(subtreePath, association);
        sdbusplus::object_path associationPath(associationPathStr);

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
    std::vector<std::string> subtreePaths =
        getSubTreePathsById(interfaceMap, id, objectPath, subtreeInterfaces);
    std::vector<std::string> output;
    for (const auto& subtreePath : subtreePaths)
    {
        // Form the association path
        std::string associationPathStr =
            appendPathSegment(subtreePath, association);
        sdbusplus::object_path associationPath(associationPathStr);

        auto associatedSubTree = getAssociatedSubTreePaths(
            interfaceMap, associationMaps, associationPath, objectPath, 0,
            endpointInterfaces);

        output.insert(output.end(), associatedSubTree.begin(),
                      associatedSubTree.end());
    }

    return output;
}
