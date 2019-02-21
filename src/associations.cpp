#include <src/associations.hpp>

void removeAssociation(const std::string& sourcePath, const std::string& owner,
                       sdbusplus::asio::object_server& server,
                       AssociationOwnersType& assocOwners,
                       AssociationInterfaces& assocInterfaces)
{
    // Use associationOwners to find the association paths and endpoints
    // that the passed in object path and service own.  Remove all of
    // these endpoints from the actual association D-Bus objects, and if
    // the endpoints property is then empty, the whole association object
    // can be removed.  Note there can be multiple services that own an
    // association, and also that sourcePath is the path of the object
    // that contains the org.openbmc.Associations interface and not the
    // association path itself.

    // Find the services that have associations for this object path
    auto owners = assocOwners.find(sourcePath);
    if (owners == assocOwners.end())
    {
        return;
    }

    // Find the association paths and endpoints owned by this object
    // path for this service.
    auto assocs = owners->second.find(owner);
    if (assocs == owners->second.end())
    {
        return;
    }

    for (const auto& [assocPath, endpointsToRemove] : assocs->second)
    {
        // Get the association D-Bus object for this assocPath
        auto target = assocInterfaces.find(assocPath);
        if (target == assocInterfaces.end())
        {
            continue;
        }

        // Remove the entries in the endpoints D-Bus property for this
        // path/owner/association-path.
        auto& existingEndpoints = std::get<endpointsPos>(target->second);
        for (const auto& endpointToRemove : endpointsToRemove)
        {
            auto e = std::find(existingEndpoints.begin(),
                               existingEndpoints.end(), endpointToRemove);

            if (e != existingEndpoints.end())
            {
                existingEndpoints.erase(e);
            }
        }

        // Remove the association from D-Bus if there are no more endpoints,
        // otherwise just update the endpoints property.
        if (existingEndpoints.empty())
        {
            server.remove_interface(std::get<ifacePos>(target->second));
            std::get<ifacePos>(target->second) = nullptr;
            std::get<endpointsPos>(target->second).clear();
        }
        else
        {
            std::get<ifacePos>(target->second)
                ->set_property("endpoints", existingEndpoints);
        }
    }

    // Remove the associationOwners entries for this owning path/service.
    owners->second.erase(assocs);
    if (owners->second.empty())
    {
        assocOwners.erase(owners);
    }
}

void removeAssociationEndpoints(
    sdbusplus::asio::object_server& objectServer, const std::string& assocPath,
    const boost::container::flat_set<std::string>& endpointsToRemove,
    AssociationInterfaces& assocInterfaces)
{
    auto assoc = assocInterfaces.find(assocPath);
    if (assoc == assocInterfaces.end())
    {
        return;
    }

    auto& endpointsInDBus = std::get<endpointsPos>(assoc->second);

    for (const auto& endpointToRemove : endpointsToRemove)
    {
        auto e = std::find(endpointsInDBus.begin(), endpointsInDBus.end(),
                           endpointToRemove);

        if (e != endpointsInDBus.end())
        {
            endpointsInDBus.erase(e);
        }
    }

    if (endpointsInDBus.empty())
    {
        objectServer.remove_interface(std::get<ifacePos>(assoc->second));
        std::get<ifacePos>(assoc->second) = nullptr;
        std::get<endpointsPos>(assoc->second).clear();
    }
    else
    {
        std::get<ifacePos>(assoc->second)
            ->set_property("endpoints", endpointsInDBus);
    }
}
