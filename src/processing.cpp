#include <boost/algorithm/string/predicate.hpp>
#include <src/processing.hpp>

bool get_well_known(
    boost::container::flat_map<std::string, std::string>& owners,
    const std::string& request, std::string& well_known)
{
    // If it's already a well known name, just return
    if (!boost::starts_with(request, ":"))
    {
        well_known = request;
        return true;
    }

    auto it = owners.find(request);
    if (it == owners.end())
    {
        return false;
    }
    well_known = it->second;
    return true;
}

bool need_to_introspect(const std::string& process_name,
                        const WhiteBlackList& whiteList,
                        const WhiteBlackList& blackList)
{
    auto inWhitelist =
        std::find_if(whiteList.begin(), whiteList.end(),
                     [&process_name](const auto& prefix) {
                         return boost::starts_with(process_name, prefix);
                     }) != whiteList.end();

    // This holds full service names, not prefixes
    auto inBlacklist = blackList.find(process_name) != blackList.end();

    return inWhitelist && !inBlacklist;
}

void process_name_change_delete(
    boost::container::flat_map<std::string, std::string>& nameOwners,
    const std::string& wellKnown, const std::string& oldOwner,
    interface_map_type& interfaceMap, AssociationOwnersType& assocOwners,
    AssociationInterfaces& assocInterfaces,
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
            auto assoc = std::find(ifaces->second.begin(), ifaces->second.end(),
                                   ASSOCIATIONS_INTERFACE);
            if (assoc != ifaces->second.end())
            {
                removeAssociation(pathIt->first, wellKnown, server,
                                  assocOwners, assocInterfaces);
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
