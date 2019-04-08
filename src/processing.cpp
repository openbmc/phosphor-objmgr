#include "processing.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <iostream>

bool getWellKnown(
    const boost::container::flat_map<std::string, std::string>& owners,
    const std::string& request, std::string& wellKnown)
{
    // If it's already a well known name, just return
    if (!boost::starts_with(request, ":"))
    {
        wellKnown = request;
        return true;
    }

    auto it = owners.find(request);
    if (it == owners.end())
    {
        return false;
    }
    wellKnown = it->second;
    return true;
}

bool needToIntrospect(const std::string& processName,
                      const WhiteBlackList& whiteList,
                      const WhiteBlackList& blackList)
{
    auto inWhitelist =
        std::find_if(whiteList.begin(), whiteList.end(),
                     [&processName](const auto& prefix) {
                         return boost::starts_with(processName, prefix);
                     }) != whiteList.end();

    // This holds full service names, not prefixes
    auto inBlacklist = blackList.find(processName) != blackList.end();

    return inWhitelist && !inBlacklist;
}

void processNameChangeDelete(
    boost::container::flat_map<std::string, std::string>& nameOwners,
    const std::string& wellKnown, const std::string& oldOwner,
    interface_map_type& interfaceMap, AssociationMaps& assocMaps,
    sdbusplus::asio::object_server& server)
{
    if (boost::starts_with(oldOwner, ":"))
    {
        auto it = nameOwners.find(oldOwner);
        if (it != nameOwners.end())
        {
            nameOwners.erase(it);
        }
    }
    // Connection removed
    interface_map_type::iterator pathIt = interfaceMap.begin();
    while (pathIt != interfaceMap.end())
    {
        // If an associations interface is being removed,
        // also need to remove the corresponding associations
        // objects and properties.
        auto ifaces = pathIt->second.find(wellKnown);
        if (ifaces != pathIt->second.end())
        {
            auto assoc = std::find_if(
                ifaces->second.begin(), ifaces->second.end(),
                [](const auto& iface) { return isAssocDefIface(iface); });
            if (assoc != ifaces->second.end())
            {
                removeAssociation(pathIt->first, wellKnown, server, assocMaps);
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

void processInterfaceAdded(interface_map_type& interfaceMap,
                           const sdbusplus::message::object_path& objPath,
                           const InterfacesAdded& intfAdded,
                           const std::string& wellKnown,
                           AssociationMaps& assocMaps,
                           sdbusplus::asio::object_server& server)
{
    auto& ifaceList = interfaceMap[objPath.str];

    for (const auto& interfacePair : intfAdded)
    {
        ifaceList[wellKnown].emplace(interfacePair.first);

        if (isAssocDefIface(interfacePair.first))
        {
            const sdbusplus::message::variant<std::vector<Association>>*
                variantAssociations = nullptr;
            for (const auto& interface : interfacePair.second)
            {
                if (interface.first == getAssocDefPropName(interfacePair.first))
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
                sdbusplus::message::variant_ns::get<std::vector<Association>>(
                    *variantAssociations);
            associationChanged(server, associations, objPath.str, wellKnown,
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
    using iface_map_iterator = interface_map_type::iterator;
    using iface_map_value_type =
        boost::container::flat_map<std::string,
                                   boost::container::flat_set<std::string>>;
    using name_map_iterator = iface_map_value_type::iterator;

    static const boost::container::flat_set<std::string> defaultIfaces{
        "org.freedesktop.DBus.Introspectable", "org.freedesktop.DBus.Peer",
        "org.freedesktop.DBus.Properties"};

    std::string parent = objPath.str;
    auto pos = parent.find_last_of('/');

    while (pos != std::string::npos)
    {
        parent = parent.substr(0, pos);

        std::pair<iface_map_iterator, bool> parentEntry =
            interfaceMap.insert(std::make_pair(parent, iface_map_value_type{}));

        std::pair<name_map_iterator, bool> ifaceEntry =
            parentEntry.first->second.insert(
                std::make_pair(wellKnown, defaultIfaces));

        if (!ifaceEntry.second)
        {
            // Entry was already there for this name so done.
            break;
        }

        pos = parent.find_last_of('/');
    }
}
