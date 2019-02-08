#include <src/associations.hpp>

// Create a default AssociationOwnersType object with input values
AssociationOwnersType createOwnerAssociation(
    const std::string& path, const std::string& service,
    const std::string& assocPath,
    const boost::container::flat_set<std::string>& endpoints)
{
    AssociationPaths assocPathMap = {{assocPath, endpoints}};
    boost::container::flat_map<std::string, AssociationPaths> serviceMap = {
        {service, assocPathMap}};
    AssociationOwnersType ownerAssoc = {{path, serviceMap}};
    return ownerAssoc;
}

// Create a default AssociationInterfaces object with input values
AssociationInterfaces
    createInterfaceAssociation(const std::string& ifaceObject,
                               sdbusplus::asio::object_server* server,
                               const Endpoints endpoints)
{
    std::shared_ptr<sdbusplus::asio::dbus_interface> iface =
        server->add_interface("/xyz/openbmc_project/test",
                              "xyz.openbmc_project.New.Interface");
    std::tuple<std::shared_ptr<sdbusplus::asio::dbus_interface>, Endpoints>
        serverEndPts = {iface, endpoints};
    AssociationInterfaces interfaceAssoc = {{ifaceObject, serverEndPts}};
    return interfaceAssoc;
}
