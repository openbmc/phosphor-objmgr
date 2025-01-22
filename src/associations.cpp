#include "associations.hpp"

#include <boost/asio/steady_timer.hpp>
#include <sdbusplus/exception.hpp>

#include <iostream>
#include <string>

void updateEndpointsOnDbus(sdbusplus::asio::object_server& objectServer,
                           const std::string& assocPath,
                           AssociationMaps& assocMaps)
{
    auto iface = assocMaps.ifaces.find(assocPath);
    if (iface == assocMaps.ifaces.end())
    {
        return;
    }
    auto& i = std::get<ifacePos>(iface->second);
    auto& endpoints = std::get<endpointsPos>(iface->second);

    // If the interface already exists, only need to update
    // the property value, otherwise create it
    if (i)
    {
        if (endpoints.empty())
        {
            objectServer.remove_interface(i);
            i = nullptr;
            assocMaps.ifaces.erase(iface);
        }
        else
        {
            i->set_property("endpoints", endpoints);
        }
    }
    else if (!endpoints.empty())
    {
        i = objectServer.add_interface(assocPath, xyzAssociationInterface);
        i->register_property("endpoints", endpoints);
        i->initialize();
    }
}

void scheduleUpdateEndpointsOnDbus(
    boost::asio::io_context& io, sdbusplus::asio::object_server& objectServer,
    const std::string& assocPath, AssociationMaps& assocMaps)
{
    static std::set<std::string> delayedUpdatePaths;

    if (delayedUpdatePaths.contains(assocPath))
    {
        return;
    }

    auto iface = assocMaps.ifaces.find(assocPath);
    if (iface == assocMaps.ifaces.end())
    {
        return;
    }
    auto& endpoints = std::get<endpointsPos>(iface->second);

    if (endpoints.size() > endpointsCountTimerThreshold)
    {
        delayedUpdatePaths.emplace(assocPath);
        auto timer = std::make_shared<boost::asio::steady_timer>(
            io, std::chrono::seconds(endpointUpdateDelaySeconds));
        timer->async_wait([&objectServer, &assocMaps, timer,
                           assocPath](const boost::system::error_code& ec) {
            if (!ec)
            {
                updateEndpointsOnDbus(objectServer, assocPath, assocMaps);
            }
            delayedUpdatePaths.erase(assocPath);
        });
    }
    else
    {
        updateEndpointsOnDbus(objectServer, assocPath, assocMaps);
    }
}

void removeAssociation(boost::asio::io_context& io,
                       const std::string& sourcePath, const std::string& owner,
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
        removeAssociationEndpoints(io, server, assocPath, endpointsToRemove,
                                   assocMaps);
    }

    // Remove the associationOwners entries for this owning path/service.
    owners->second.erase(assocs);
    if (owners->second.empty())
    {
        assocMaps.owners.erase(owners);
    }

    // If we were still waiting on the other side of this association to
    // show up, cancel that wait.
    removeFromPendingAssociations(sourcePath, assocMaps);
}

void removeAssociationEndpoints(
    boost::asio::io_context& io, sdbusplus::asio::object_server& objectServer,
    const std::string& assocPath,
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

    scheduleUpdateEndpointsOnDbus(io, objectServer, assocPath, assocMaps);
}

void checkAssociationEndpointRemoves(
    boost::asio::io_context& io, const std::string& sourcePath,
    const std::string& owner, const AssociationPaths& newAssociations,
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
            removeAssociationEndpoints(io, objectServer, originalAssocPath,
                                       originalEndpoints, assocMaps);
        }
        else
        {
            // The association is still there.  Check if the endpoints
            // changed.
            boost::container::flat_set<std::string> toRemove;

            for (const auto& originalEndpoint : originalEndpoints)
            {
                if (std::find(newEndpoints->second.begin(),
                              newEndpoints->second.end(), originalEndpoint) ==
                    newEndpoints->second.end())
                {
                    toRemove.emplace(originalEndpoint);
                }
            }
            if (!toRemove.empty())
            {
                removeAssociationEndpoints(io, objectServer, originalAssocPath,
                                           toRemove, assocMaps);
            }
        }
    }
}

void addEndpointsToAssocIfaces(
    boost::asio::io_context& io, sdbusplus::asio::object_server& objectServer,
    const std::string& assocPath,
    const boost::container::flat_set<std::string>& endpointPaths,
    AssociationMaps& assocMaps)
{
    auto iface = assocMaps.ifaces.find(assocPath);
    if (iface == assocMaps.ifaces.end())
    {
        return;
    }
    auto& endpoints = std::get<endpointsPos>(iface->second);

    // Only add new endpoints
    for (const auto& e : endpointPaths)
    {
        if (std::find(endpoints.begin(), endpoints.end(), e) == endpoints.end())
        {
            endpoints.push_back(e);
        }
    }
    scheduleUpdateEndpointsOnDbus(io, objectServer, assocPath, assocMaps);
}

void associationChanged(
    boost::asio::io_context& io, sdbusplus::asio::object_server& objectServer,
    const std::vector<Association>& associations, const std::string& path,
    const std::string& owner, const InterfaceMapType& interfaceMap,
    AssociationMaps& assocMaps)
{
    AssociationPaths objects;

    for (const Association& association : associations)
    {
        std::string forward;
        std::string reverse;
        std::string objectPath;
        std::tie(forward, reverse, objectPath) = association;

        if (objectPath.empty())
        {
            std::cerr << "Found invalid association on path " << path << "\n";
            continue;
        }

        // Can't create this association if the endpoint isn't on D-Bus.
        if (interfaceMap.find(objectPath) == interfaceMap.end())
        {
            addPendingAssociation(objectPath, reverse, path, forward, owner,
                                  assocMaps);
            continue;
        }

        if (!forward.empty())
        {
            objects[path + "/" + forward].emplace(objectPath);
        }
        if (!reverse.empty())
        {
            objects[objectPath + "/" + reverse].emplace(path);
        }
    }
    for (const auto& object : objects)
    {
        addEndpointsToAssocIfaces(io, objectServer, object.first, object.second,
                                  assocMaps);
    }

    // Check for endpoints being removed instead of added
    checkAssociationEndpointRemoves(io, path, owner, objects, objectServer,
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

void addPendingAssociation(
    const std::string& objectPath, const std::string& type,
    const std::string& endpointPath, const std::string& endpointType,
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

void removeFromPendingAssociations(const std::string& endpointPath,
                                   AssociationMaps& assocMaps)
{
    auto assoc = assocMaps.pending.begin();
    while (assoc != assocMaps.pending.end())
    {
        auto endpoint = assoc->second.begin();
        while (endpoint != assoc->second.end())
        {
            auto& e = std::get<assocPos>(*endpoint);
            if (std::get<reversePathPos>(e) == endpointPath)
            {
                endpoint = assoc->second.erase(endpoint);
                continue;
            }

            endpoint++;
        }

        if (assoc->second.empty())
        {
            assoc = assocMaps.pending.erase(assoc);
            continue;
        }

        assoc++;
    }
}

void addSingleAssociation(
    boost::asio::io_context& io, sdbusplus::asio::object_server& server,
    const std::string& assocPath, const std::string& endpoint,
    const std::string& owner, const std::string& ownerPath,
    AssociationMaps& assocMaps)
{
    boost::container::flat_set<std::string> endpoints{endpoint};

    addEndpointsToAssocIfaces(io, server, assocPath, endpoints, assocMaps);

    AssociationPaths objects;
    boost::container::flat_set e{endpoint};
    objects.emplace(assocPath, e);

    auto a = assocMaps.owners.find(ownerPath);
    if (a != assocMaps.owners.end())
    {
        auto o = a->second.find(owner);
        if (o != a->second.end())
        {
            auto p = o->second.find(assocPath);
            if (p != o->second.end())
            {
                p->second.emplace(endpoint);
            }
            else
            {
                o->second.emplace(assocPath, e);
            }
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
        assocMaps.owners.emplace(endpoint, owners);
    }
}

void checkIfPendingAssociation(
    boost::asio::io_context& io, const std::string& objectPath,
    const InterfaceMapType& interfaceMap, AssociationMaps& assocMaps,
    sdbusplus::asio::object_server& server)
{
    auto pending = assocMaps.pending.find(objectPath);
    if (pending == assocMaps.pending.end())
    {
        return;
    }

    if (interfaceMap.find(objectPath) == interfaceMap.end())
    {
        return;
    }

    auto endpoint = pending->second.begin();

    while (endpoint != pending->second.end())
    {
        const auto& e = std::get<assocPos>(*endpoint);

        // Ensure the other side of the association still exists
        if (interfaceMap.find(std::get<reversePathPos>(e)) ==
            interfaceMap.end())
        {
            endpoint++;
            continue;
        }

        // Add both sides of the association:
        //  objectPath/forwardType and reversePath/reverseType
        //
        // The ownerPath is the reversePath - i.e. the endpoint that
        // is on D-Bus and owns the org.openbmc.Associations iface.
        //
        const auto& ownerPath = std::get<reversePathPos>(e);
        const auto& owner = std::get<ownerPos>(*endpoint);

        auto assocPath = objectPath + '/' + std::get<forwardTypePos>(e);
        auto endpointPath = ownerPath;

        try
        {
            addSingleAssociation(io, server, assocPath, endpointPath, owner,
                                 ownerPath, assocMaps);

            // Now the reverse direction (still the same owner and ownerPath)
            assocPath = endpointPath + '/' + std::get<reverseTypePos>(e);
            endpointPath = objectPath;
            addSingleAssociation(io, server, assocPath, endpointPath, owner,
                                 ownerPath, assocMaps);
        }
        catch (const sdbusplus::exception_t& e)
        {
            // In some case the interface could not be created on DBus and an
            // exception is thrown. mapper has no control of the interface/path
            // of the associations, so it has to catch the error and drop the
            // association request.
            std::cerr << "Error adding association: assocPath " << assocPath
                      << ", endpointPath " << endpointPath
                      << ", what: " << e.what() << "\n";
        }

        // Not pending anymore
        endpoint = pending->second.erase(endpoint);
    }

    if (pending->second.empty())
    {
        assocMaps.pending.erase(objectPath);
    }
}

void findAssociations(const std::string& endpointPath,
                      AssociationMaps& assocMaps,
                      FindAssocResults& associationData)
{
    for (const auto& [sourcePath, owners] : assocMaps.owners)
    {
        for (const auto& [owner, assocs] : owners)
        {
            for (const auto& [assocPath, endpoints] : assocs)
            {
                if (std::find(endpoints.begin(), endpoints.end(),
                              endpointPath) != endpoints.end())
                {
                    // assocPath is <path>/<type> which tells us what is on the
                    // other side of the association.
                    auto pos = assocPath.rfind('/');
                    auto otherPath = assocPath.substr(0, pos);
                    auto otherType = assocPath.substr(pos + 1);

                    // Now we need to find the endpointPath/<type> ->
                    // [otherPath] entry so that we can get the type for
                    // endpointPath's side of the assoc.  Do this by finding
                    // otherPath as an endpoint, and also checking for
                    // 'endpointPath/*' as the key.
                    auto a = std::find_if(
                        assocs.begin(), assocs.end(),
                        [&endpointPath, &otherPath](const auto& ap) {
                            const auto& endpoints = ap.second;
                            auto endpoint = std::find(
                                endpoints.begin(), endpoints.end(), otherPath);
                            if (endpoint != endpoints.end())
                            {
                                return ap.first.starts_with(endpointPath + '/');
                            }
                            return false;
                        });

                    if (a != assocs.end())
                    {
                        // Pull out the type from endpointPath/<type>
                        pos = a->first.rfind('/');
                        auto thisType = a->first.substr(pos + 1);

                        // Now we know the full association:
                        // endpointPath/thisType -> otherPath/otherType
                        Association association{thisType, otherType, otherPath};
                        associationData.emplace_back(owner, association);
                    }
                }
            }
        }
    }
}

/** @brief Remove an endpoint for a particular association from D-Bus.
 *
 * If the last endpoint is gone, remove the whole association interface,
 * otherwise just update the D-Bus endpoints property.
 *
 * @param[in] assocPath     - the association path
 * @param[in] endpointPath  - the endpoint path to find and remove
 * @param[in,out] assocMaps - the association maps
 * @param[in,out] server    - sdbus system object
 */
void removeAssociationIfacesEntry(
    boost::asio::io_context& io, const std::string& assocPath,
    const std::string& endpointPath, AssociationMaps& assocMaps,
    sdbusplus::asio::object_server& server)
{
    auto assoc = assocMaps.ifaces.find(assocPath);
    if (assoc != assocMaps.ifaces.end())
    {
        auto& endpoints = std::get<endpointsPos>(assoc->second);
        auto e = std::find(endpoints.begin(), endpoints.end(), endpointPath);
        if (e != endpoints.end())
        {
            endpoints.erase(e);

            scheduleUpdateEndpointsOnDbus(io, server, assocPath, assocMaps);
        }
    }
}

/** @brief Remove an endpoint from the association owners map.
 *
 * For a specific association path and owner, remove the endpoint.
 * Remove all remaining artifacts of that endpoint in the owners map
 * based on what frees up after the erase.
 *
 * @param[in] assocPath     - the association object path
 * @param[in] endpointPath  - the endpoint object path
 * @param[in] owner         - the owner of the association
 * @param[in,out] assocMaps - the association maps
 */
void removeAssociationOwnersEntry(
    const std::string& assocPath, const std::string& endpointPath,
    const std::string& owner, AssociationMaps& assocMaps)
{
    auto sources = assocMaps.owners.begin();
    while (sources != assocMaps.owners.end())
    {
        auto owners = sources->second.find(owner);
        if (owners != sources->second.end())
        {
            auto entry = owners->second.find(assocPath);
            if (entry != owners->second.end())
            {
                auto e = std::find(entry->second.begin(), entry->second.end(),
                                   endpointPath);
                if (e != entry->second.end())
                {
                    entry->second.erase(e);
                    if (entry->second.empty())
                    {
                        owners->second.erase(entry);
                    }
                }
            }

            if (owners->second.empty())
            {
                sources->second.erase(owners);
            }
        }

        if (sources->second.empty())
        {
            sources = assocMaps.owners.erase(sources);
            continue;
        }
        sources++;
    }
}

void moveAssociationToPending(
    boost::asio::io_context& io, const std::string& endpointPath,
    AssociationMaps& assocMaps, sdbusplus::asio::object_server& server)
{
    FindAssocResults associationData;

    // Check which associations this path is an endpoint of, and
    // then add them to the pending associations map and remove
    // the associations objects.
    findAssociations(endpointPath, assocMaps, associationData);

    for (const auto& [owner, association] : associationData)
    {
        const auto& forwardPath = endpointPath;
        const auto& forwardType = std::get<forwardTypePos>(association);
        const auto& reversePath = std::get<reversePathPos>(association);
        const auto& reverseType = std::get<reverseTypePos>(association);

        addPendingAssociation(forwardPath, forwardType, reversePath,
                              reverseType, owner, assocMaps);

        // Remove both sides of the association from assocMaps.ifaces
        removeAssociationIfacesEntry(io, forwardPath + '/' + forwardType,
                                     reversePath, assocMaps, server);
        removeAssociationIfacesEntry(io, reversePath + '/' + reverseType,
                                     forwardPath, assocMaps, server);

        // Remove both sides of the association from assocMaps.owners
        removeAssociationOwnersEntry(forwardPath + '/' + forwardType,
                                     reversePath, owner, assocMaps);
        removeAssociationOwnersEntry(reversePath + '/' + reverseType,
                                     forwardPath, owner, assocMaps);
    }
}
