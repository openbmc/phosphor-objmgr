#include "src/associations.hpp"
#include "src/processing.hpp"

const std::string defaultSourcePath = "/logging/entry/1";
const std::string defaultDbusSvc = "xyz.openbmc_project.New.Interface";
const std::string defaultFwdPath = {defaultSourcePath + "/" + "inventory"};
const std::string defaultEndpoint =
    "/xyz/openbmc_project/inventory/system/chassis";
const std::string defaultRevPath = {defaultEndpoint + "/" + "error"};
const std::string extraEndpoint = "/xyz/openbmc_project/differnt/endpoint";

// Create a default AssociationOwnersType object
AssociationOwnersType createDefaultOwnerAssociation()
{
    AssociationPaths assocPathMap = {{defaultFwdPath, {defaultEndpoint}},
                                     {defaultRevPath, {defaultSourcePath}}};
    boost::container::flat_map<std::string, AssociationPaths> serviceMap = {
        {defaultDbusSvc, assocPathMap}};
    AssociationOwnersType ownerAssoc = {{defaultSourcePath, serviceMap}};
    return ownerAssoc;
}

// Create a default AssociationInterfaces object
AssociationInterfaces
    createDefaultInterfaceAssociation(sdbusplus::asio::object_server* server)
{
    AssociationInterfaces interfaceAssoc;

    auto& iface = interfaceAssoc[defaultFwdPath];
    auto& endpoints = std::get<endpointsPos>(iface);
    endpoints.push_back(defaultEndpoint);
    server->add_interface(defaultFwdPath, defaultDbusSvc);

    auto& iface2 = interfaceAssoc[defaultRevPath];
    auto& endpoints2 = std::get<endpointsPos>(iface2);
    endpoints2.push_back(defaultSourcePath);
    server->add_interface(defaultRevPath, defaultDbusSvc);

    return interfaceAssoc;
}

// Just add an extra endpoint to the first association
void addEndpointToInterfaceAssociation(AssociationInterfaces& interfaceAssoc)
{
    auto iface = interfaceAssoc[defaultFwdPath];
    auto endpoints = std::get<endpointsPos>(iface);
    endpoints.push_back(extraEndpoint);
}

// Create a default interfaceMapType with input values
InterfaceMapType createInterfaceMap(
    const std::string& path, const std::string& connectionName,
    const boost::container::flat_set<std::string>& interfaceNames)
{
    boost::container::flat_map<std::string,
                               boost::container::flat_set<std::string>>
        connectionMap = {{connectionName, interfaceNames}};
    InterfaceMapType interfaceMap = {{path, connectionMap}};
    return interfaceMap;
}

// Create a default interfaceMapType with 2 entries with the same
// owner.
InterfaceMapType createDefaultInterfaceMap()
{
    InterfaceMapType interfaceMap = {
        {defaultSourcePath, {{defaultDbusSvc, {"a"}}}},
        {defaultEndpoint, {{defaultDbusSvc, {"b"}}}}};

    return interfaceMap;
}
