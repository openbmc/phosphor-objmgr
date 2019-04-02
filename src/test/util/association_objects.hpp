#include "src/associations.hpp"

const std::string DEFAULT_SOURCE_PATH = "/logging/entry/1";
const std::string DEFAULT_DBUS_SVC = "xyz.openbmc_project.New.Interface";
const std::string DEFAULT_FWD_PATH = {DEFAULT_SOURCE_PATH + "/" + "inventory"};
const std::string DEFAULT_ENDPOINT =
    "/xyz/openbmc_project/inventory/system/chassis";
const std::string DEFAULT_REV_PATH = {DEFAULT_ENDPOINT + "/" + "error"};
const std::string EXTRA_ENDPOINT = "/xyz/openbmc_project/differnt/endpoint";

// Create a default AssociationOwnersType object
AssociationOwnersType createDefaultOwnerAssociation()
{
    AssociationPaths assocPathMap = {{DEFAULT_FWD_PATH, {DEFAULT_ENDPOINT}},
                                     {DEFAULT_REV_PATH, {DEFAULT_SOURCE_PATH}}};
    boost::container::flat_map<std::string, AssociationPaths> serviceMap = {
        {DEFAULT_DBUS_SVC, assocPathMap}};
    AssociationOwnersType ownerAssoc = {{DEFAULT_SOURCE_PATH, serviceMap}};
    return ownerAssoc;
}

// Create a default AssociationInterfaces object
AssociationInterfaces
    createDefaultInterfaceAssociation(sdbusplus::asio::object_server* server)
{
    AssociationInterfaces interfaceAssoc;

    auto& iface = interfaceAssoc[DEFAULT_FWD_PATH];
    auto& endpoints = std::get<endpointsPos>(iface);
    endpoints.push_back(DEFAULT_ENDPOINT);
    server->add_interface(DEFAULT_FWD_PATH, DEFAULT_DBUS_SVC);

    auto& iface2 = interfaceAssoc[DEFAULT_REV_PATH];
    auto& endpoints2 = std::get<endpointsPos>(iface2);
    endpoints2.push_back(DEFAULT_SOURCE_PATH);
    server->add_interface(DEFAULT_REV_PATH, DEFAULT_DBUS_SVC);

    return interfaceAssoc;
}

// Just add an extra endpoint to the first association
void addEndpointToInterfaceAssociation(AssociationInterfaces& interfaceAssoc)
{
    auto iface = interfaceAssoc[DEFAULT_FWD_PATH];
    auto endpoints = std::get<endpointsPos>(iface);
    endpoints.push_back(EXTRA_ENDPOINT);
}
