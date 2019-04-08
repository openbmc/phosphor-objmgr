#include "associations.hpp"

#include <iostream>

void removeAssociation(const std::string& sourcePath, const std::string& owner,
                       sdbusplus::asio::object_server& server,
                       AssociationMaps& assocMaps)
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
    auto owners = assocMaps.owners.find(sourcePath);
    if (owners == assocMaps.owners.end())
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
        removeAssociationEndpoints(server, assocPath, endpointsToRemove,
                                   assocMaps);
    }

    // Remove the associationOwners entries for this owning path/service.
    owners->second.erase(assocs);
    if (owners->second.empty())
    {
        assocMaps.owners.erase(owners);
    }
}

void removeAssociationEndpoints(
    sdbusplus::asio::object_server& objectServer, const std::string& assocPath,
    const boost::container::flat_set<std::string>& endpointsToRemove,
    AssociationMaps& assocMaps)
{
    auto assoc = assocMaps.ifaces.find(assocPath);
    if (assoc == assocMaps.ifaces.end())
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

void checkAssociationEndpointRemoves(
    const std::string& sourcePath, const std::string& owner,
    const AssociationPaths& newAssociations,
    sdbusplus::asio::object_server& objectServer, AssociationMaps& assocMaps)
{
    // Find the services that have associations on this path.
    auto originalOwners = assocMaps.owners.find(sourcePath);
    if (originalOwners == assocMaps.owners.end())
    {
        return;
    }

    // Find the associations for this service
    auto originalAssociations = originalOwners->second.find(owner);
    if (originalAssociations == originalOwners->second.end())
    {
        return;
    }

    // Compare the new endpoints versus the original endpoints, and
    // remove any of the original ones that aren't in the new list.
    for (const auto& [originalAssocPath, originalEndpoints] :
         originalAssociations->second)
    {
        // Check if this source even still has each association that
        // was there previously, and if not, remove all of its endpoints
        // from the D-Bus endpoints property which will cause the whole
        // association path to be removed if no endpoints remain.
        auto newEndpoints = newAssociations.find(originalAssocPath);
        if (newEndpoints == newAssociations.end())
        {
            removeAssociationEndpoints(objectServer, originalAssocPath,
                                       originalEndpoints, assocMaps);
        }
        else
        {
            // The association is still there.  Check if the endpoints
            // changed.
            boost::container::flat_set<std::string> toRemove;

            for (auto& originalEndpoint : originalEndpoints)
            {
                if (std::find(newEndpoints->second.begin(),
                              newEndpoints->second.end(),
                              originalEndpoint) == newEndpoints->second.end())
                {
                    toRemove.emplace(originalEndpoint);
                }
            }
            if (!toRemove.empty())
            {
                removeAssociationEndpoints(objectServer, originalAssocPath,
                                           toRemove, assocMaps);
            }
        }
    }
}

void associationChanged(sdbusplus::asio::object_server& objectServer,
                        const std::vector<Association>& associations,
                        const std::string& path, const std::string& owner,
                        const interface_map_type& interfaceMap,
                        AssociationMaps& assocMaps)
{
    AssociationPaths objects;

    for (const Association& association : associations)
    {
        std::string forward;
        std::string reverse;
        std::string endpoint;
        std::tie(forward, reverse, endpoint) = association;

        if (endpoint.empty())
        {
            std::cerr << "Found invalid association on path " << path << "\n";
            continue;
        }

        // Can't create this association if the endpoint isn't on D-Bus.
        if (interfaceMap.find(endpoint) == interfaceMap.end())
        {
            addPendingAssociation(endpoint, reverse, path, forward, owner,
                                  assocMaps);
            continue;
        }

        if (forward.size())
        {
            objects[path + "/" + forward].emplace(endpoint);
        }
        if (reverse.size())
        {
            objects[endpoint + "/" + reverse].emplace(path);
        }
    }
    for (const auto& object : objects)
    {
        // the mapper exposes the new association interface but intakes
        // the old

        auto& iface = assocMaps.ifaces[object.first];
        auto& i = std::get<ifacePos>(iface);
        auto& endpoints = std::get<endpointsPos>(iface);

        // Only add new endpoints
        for (auto& e : object.second)
        {
            if (std::find(endpoints.begin(), endpoints.end(), e) ==
                endpoints.end())
            {
                endpoints.push_back(e);
            }
        }

        // If the interface already exists, only need to update
        // the property value, otherwise create it
        if (i)
        {
            i->set_property("endpoints", endpoints);
        }
        else
        {
            i = objectServer.add_interface(object.first,
                                           XYZ_ASSOCIATION_INTERFACE);
            i->register_property("endpoints", endpoints);
            i->initialize();
        }
    }

    // Check for endpoints being removed instead of added
    checkAssociationEndpointRemoves(path, owner, objects, objectServer,
                                    assocMaps);

    if (!objects.empty())
    {
        // Update associationOwners with the latest info
        auto a = assocMaps.owners.find(path);
        if (a != assocMaps.owners.end())
        {
            auto o = a->second.find(owner);
            if (o != a->second.end())
            {
                o->second = std::move(objects);
            }
            else
            {
                a->second.emplace(owner, std::move(objects));
            }
        }
        else
        {
            boost::container::flat_map<std::string, AssociationPaths> owners;
            owners.emplace(owner, std::move(objects));
            assocMaps.owners.emplace(path, owners);
        }
    }
}

void addPendingAssociation(const std::string& objectPath,
                           const std::string& type,
                           const std::string& endpointPath,
                           const std::string& endpointType,
                           const std::string& owner, AssociationMaps& assocMaps)
{
    Association assoc{type, endpointType, endpointPath};

    auto p = assocMaps.pending.find(objectPath);
    if (p == assocMaps.pending.end())
    {
        ExistingEndpoints ee;
        ee.emplace_back(owner, std::move(assoc));
        assocMaps.pending.emplace(objectPath, std::move(ee));
    }
    else
    {
        // Already waiting on this path for another association,
        // so just add this endpoint and owner.
        auto& endpoints = p->second;
        auto e =
            std::find_if(endpoints.begin(), endpoints.end(),
                         [&assoc, &owner](const auto& endpoint) {
                             return (std::get<ownerPos>(endpoint) == owner) &&
                                    (std::get<assocPos>(endpoint) == assoc);
                         });
        if (e == endpoints.end())
        {
            endpoints.emplace_back(owner, std::move(assoc));
        }
    }
}
