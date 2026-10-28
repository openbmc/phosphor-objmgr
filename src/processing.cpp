#include "processing.hpp"

#include "associations.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <string_view>

std::span<const std::string> getWellKnownNames(const NameOwners& owners,
                                               const std::string& request)
{
    // If it's already a well known name, just return
    if (!request.starts_with(":"))
    {
        return {&request, 1};
    }

    auto it = owners.find(request);
    if (it == owners.end() || it->second.empty())
    {
        return {};
    }
    // A flat_set stores its elements contiguously.
    return {&*it->second.begin(), it->second.size()};
}

bool needToIntrospect(const std::string& processName)
{
    using namespace std::string_view_literals;
    static constexpr std::array<std::string_view, 2> skipNamespaces{
        ":"sv, "org.freedesktop"sv};

    auto inSkipList = std::find_if(skipNamespaces.begin(), skipNamespaces.end(),
                                   [&processName](auto prefix) {
                                       return processName.starts_with(prefix);
                                   }) != skipNamespaces.end();
    return !(inSkipList || processName.empty());
}

void processNameChangeDelete(
    boost::asio::io_context& io, NameOwners& nameOwners,
    const std::string& wellKnown, const std::string& oldOwner,
    InterfaceMapType& interfaceMap, AssociationMaps& assocMaps,
    sdbusplus::asio::object_server& server)
{
    if (oldOwner.starts_with(":"))
    {
        // The old owner may still own other well-known names.
        auto it = nameOwners.find(oldOwner);
        if (it != nameOwners.end())
        {
            it->second.erase(wellKnown);
            if (it->second.empty())
            {
                nameOwners.erase(it);
            }
        }
    }
    // Connection removed
    InterfaceMapType::iterator pathIt = interfaceMap.begin();
    while (pathIt != interfaceMap.end())
    {
        // If an associations interface is being removed,
        // also need to remove the corresponding associations
        // objects and properties.
        auto ifaces = pathIt->second.find(wellKnown);
        if (ifaces != pathIt->second.end())
        {
            auto assoc = std::find(ifaces->second.begin(), ifaces->second.end(),
                                   assocDefsInterface);
            if (assoc != ifaces->second.end())
            {
                removeAssociation(io, pathIt->first, wellKnown, server,
                                  assocMaps);
            }

            // Instead of checking if every single path is the endpoint of an
            // association that needs to be moved to pending, only check when
            // we own this path as well, which would be because of an
            // association.
            if ((pathIt->second.size() == 2) &&
                (pathIt->second.find("xyz.openbmc_project.ObjectMapper") !=
                 pathIt->second.end()))
            {
                // Remove the 2 association D-Bus paths and move the
                // association to pending.
                moveAssociationToPending(io, pathIt->first, assocMaps, server);
            }
        }
        pathIt->second.erase(wellKnown);
        if (pathIt->second.empty())
        {
            // If the last connection to the object is gone,
            // delete the top level object
            pathIt = interfaceMap.erase(pathIt);
            continue;
        }
        pathIt++;
    }
}

void processInterfaceAdded(
    boost::asio::io_context& io, InterfaceMapType& interfaceMap,
    const sdbusplus::object_path& objPath, const InterfacesAdded& intfAdded,
    const std::string& wellKnown, AssociationMaps& assocMaps,
    sdbusplus::asio::object_server& server)
{
    auto& ifaceList = interfaceMap[objPath.string()];

    for (const auto& interfacePair : intfAdded)
    {
        ifaceList[wellKnown].emplace(interfacePair.first);

        if (interfacePair.first == assocDefsInterface)
        {
            const std::variant<std::vector<Association>>* variantAssociations =
                nullptr;
            for (const auto& interface : interfacePair.second)
            {
                if (interface.first == assocDefsProperty)
                {
                    variantAssociations = &(interface.second);
                }
            }
            if (variantAssociations == nullptr)
            {
                std::cerr << "Illegal association found on " << wellKnown
                          << "\n";
                continue;
            }
            std::vector<Association> associations =
                std::get<std::vector<Association>>(*variantAssociations);
            associationChanged(io, server, associations, objPath, wellKnown,
                               interfaceMap, assocMaps);
        }
    }

    // To handle the case where an object path is being created
    // with 2 or more new path segments, check if the parent paths
    // of this path are already in the interface map, and add them
    // if they aren't with just the default freedesktop interfaces.
    // This would be done via introspection if they would have
    // already existed at startup.  While we could also introspect
    // them now to do the work, we know there aren't any other
    // interfaces or we would have gotten signals for them as well,
    // so take a shortcut to speed things up.
    //
    // This is all needed so that mapper operations can be done
    // on the new parent paths.
    using iface_map_iterator = InterfaceMapType::iterator;
    using name_map_iterator = ConnectionNames::iterator;

    auto parent = objPath;

    while (parent != "/")
    {
        parent = parent.parent_path();

        std::pair<iface_map_iterator, bool> parentEntry =
            interfaceMap.emplace(parent, ConnectionNames{});

        std::pair<name_map_iterator, bool> ifaceEntry =
            parentEntry.first->second.emplace(wellKnown, InterfaceNames{});

        if (!ifaceEntry.second)
        {
            // Entry was already there for this name so done.
            break;
        }
    }

    // The new interface might have an association pending
    checkIfPendingAssociation(io, objPath, interfaceMap, assocMaps, server);
}

// Whether or not an interface set only contains interfaces that sd-bus
// implements for every object. This is also the case for an empty interface
// set, which is what processInterfaceAdded() stores for parent paths.
static bool hasOnlyDefaultInterfaces(const InterfaceNames& interfaces)
{
    static constexpr std::array<std::string_view, 3> defaultInterfaces{
        "org.freedesktop.DBus.Introspectable", "org.freedesktop.DBus.Peer",
        "org.freedesktop.DBus.Properties"};

    return std::ranges::all_of(interfaces, [](const std::string& interface) {
        return std::ranges::find(defaultInterfaces, interface) !=
               defaultInterfaces.end();
    });
}

// Whether or not owner owns a descendant of path.
static bool hasChildForOwner(const InterfaceMapType& interfaceMap,
                             const std::string& path, const std::string& owner)
{
    const std::string childPrefix = path == "/" ? path : path + '/';

    // interfaceMap is sorted, so all descendants of path follow childPrefix.
    for (auto it = interfaceMap.lower_bound(childPrefix);
         it != interfaceMap.end() && it->first.starts_with(childPrefix); ++it)
    {
        if (it->first != path && it->second.contains(owner))
        {
            return true;
        }
    }
    return false;
}

void removeUnneededParents(const sdbusplus::object_path& objectPath,
                           const std::string& owner,
                           InterfaceMapType& interfaceMap)
{
    auto parent = objectPath;

    while (true)
    {
        if (parent == "/")
        {
            break;
        }

        parent = parent.parent_path();

        auto parentIt = interfaceMap.find(parent);
        if (parentIt == interfaceMap.end())
        {
            break;
        }

        auto ifacesIt = parentIt->second.find(owner);
        if (ifacesIt == parentIt->second.end())
        {
            break;
        }

        if (!hasOnlyDefaultInterfaces(ifacesIt->second))
        {
            break;
        }

        // Remove this parent if there isn't a remaining child on this owner
        if (hasChildForOwner(interfaceMap, parent, owner))
        {
            break;
        }

        parentIt->second.erase(ifacesIt);
        if (parentIt->second.empty())
        {
            interfaceMap.erase(parentIt);
        }
    }
}

void processInterfacesRemoved(
    boost::asio::io_context& io, InterfaceMapType& interfaceMap,
    const sdbusplus::object_path& objPath,
    const std::vector<std::string>& interfacesRemoved,
    const std::string& sender, AssociationMaps& assocMaps,
    sdbusplus::asio::object_server& server)
{
    auto connectionMap = interfaceMap.find(objPath);
    if (connectionMap == interfaceMap.end())
    {
        return;
    }

    auto interfaceSet = connectionMap->second.find(sender);
    if (interfaceSet == connectionMap->second.end())
    {
        removeUnneededParents(objPath, sender, interfaceMap);
        return;
    }

    for (const std::string& interface : interfacesRemoved)
    {
        if (interface == assocDefsInterface)
        {
            removeAssociation(io, objPath, sender, server, assocMaps);
        }

        interfaceSet->second.erase(interface);
    }

    // Objects that have been discovered through introspection also have the
    // default sd-bus interfaces. These are not reported in the
    // InterfacesRemoved signal. Hence, also consider the object removed if
    // only default interfaces remain, unless the object is a parent of
    // another object of the same service.
    if (hasOnlyDefaultInterfaces(interfaceSet->second) &&
        !hasChildForOwner(interfaceMap, objPath, sender))
    {
        // If this was the last interface on this connection,
        // erase the connection
        connectionMap->second.erase(interfaceSet);

        // Instead of checking if every single path is the endpoint of an
        // association that needs to be moved to pending, only check when
        // the only remaining owner of this path is ourself, which would be
        // because we still own the association path.
        if ((connectionMap->second.size() == 1) &&
            (connectionMap->second.begin()->first ==
             "xyz.openbmc_project.ObjectMapper"))
        {
            // Remove the 2 association D-Bus paths and move the
            // association to pending.
            moveAssociationToPending(io, objPath, assocMaps, server);
        }
    }

    // If this was the last connection on this object path,
    // erase the object path
    if (connectionMap->second.empty())
    {
        interfaceMap.erase(connectionMap);
    }

    removeUnneededParents(objPath, sender, interfaceMap);
}
