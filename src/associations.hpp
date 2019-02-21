#pragma once

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <memory>
#include <sdbusplus/asio/object_server.hpp>
#include <string>
#include <tuple>
#include <vector>

constexpr const char* XYZ_ASSOCIATION_INTERFACE =
    "xyz.openbmc_project.Association";

//  Associations and some metadata are stored in associationInterfaces.
//  The fields are:
//   * ifacePos - holds the D-Bus interface object
//   * endpointsPos - holds the endpoints array that shadows the property
static constexpr auto ifacePos = 0;
static constexpr auto endpointsPos = 1;
using Endpoints = std::vector<std::string>;

// map[interface path: tuple[dbus_interface,vector[endpoint paths]]]
using AssociationInterfaces = boost::container::flat_map<
    std::string,
    std::tuple<std::shared_ptr<sdbusplus::asio::dbus_interface>, Endpoints>>;

// The associationOwners map contains information about creators of
// associations, so that when a org.openbmc.Association interface is
// removed or its 'associations' property is changed, the mapper owned
// association objects can be correctly handled.  It is a map of the
// object path of the org.openbmc.Association owner to a map of the
// service the path is owned by, to a map of the association objects to
// their endpoint paths:
// map[ownerPath : map[service : map[assocPath : [endpoint paths]]]
// For example:
// [/logging/entry/1 :
//   [xyz.openbmc_project.Logging :
//     [/logging/entry/1/callout : [/system/cpu0],
//      /system/cpu0/fault : [/logging/entry/1]]]]

using AssociationPaths =
    boost::container::flat_map<std::string,
                               boost::container::flat_set<std::string>>;

using AssociationOwnersType = boost::container::flat_map<
    std::string, boost::container::flat_map<std::string, AssociationPaths>>;

using Association = std::tuple<std::string, std::string, std::string>;

/** @brief Remove input association
 *
 * @param[in] sourcePath          - Path of the object that contains the
 *                                  org.openbmc.Associations
 * @param[in] owner               - The Dbus service having its associations
 *                                  removed
 * @param[in,out] server          - sdbus system object
 * @param[in,out] assocOwners     - Owners of associations
 * @param[in,out] assocInterfaces - Associations endpoints
 *
 * @return Void, server, assocOwners, and assocInterfaces updated if needed
 */
void removeAssociation(const std::string& sourcePath, const std::string& owner,
                       sdbusplus::asio::object_server& server,
                       AssociationOwnersType& assocOwners,
                       AssociationInterfaces& assocInterfaces);

/** @brief Remove input paths from endpoints of an association
 *
 * If the last endpoint was removed, then remove the whole
 * association object, otherwise just set the property
 *
 * @param[in] objectServer        - sdbus system object
 * @param[in] assocPath           - Path of the object that contains the
 *                                  org.openbmc.Associations
 * @param[in] endpointsToRemove   - Endpoints to remove
 * @param[in,out] assocInterfaces - Associations endpoints
 *
 * @return Void, objectServer and assocInterfaces updated if needed
 */
void removeAssociationEndpoints(
    sdbusplus::asio::object_server& objectServer, const std::string& assocPath,
    const boost::container::flat_set<std::string>& endpointsToRemove,
    AssociationInterfaces& assocInterfaces);

/** @brief Check and remove any changed associations
 *
 * Based on the latest values of the org.openbmc.Associations.associations
 * property, passed in via the newAssociations param, check if any of the
 * paths in the xyz.openbmc_project.Association.endpoints D-Bus property
 * for that association need to be removed.  If the last path is removed
 * from the endpoints property, remove that whole association object from
 * D-Bus.
 *
 * @param[in] sourcePath         - Path of the object that contains the
 *                                 org.openbmc.Associations
 * @param[in] owner              - The Dbus service having it's associatons
 *                                 changed
 * @param[in] newAssociations    - New associations to look at for change
 * @param[in,out] objectServer   - sdbus system object
 * @param[in,out] assocOwners    - Owners of associations
 * @param[in,out] assocInterfaces - Associations endpoints
 *
 * @return Void, objectServer and assocOwners updated if needed
 */
void checkAssociationEndpointRemoves(
    const std::string& sourcePath, const std::string& owner,
    const AssociationPaths& newAssociations,
    sdbusplus::asio::object_server& objectServer,
    AssociationOwnersType& assocOwners, AssociationInterfaces& assocInterfaces);

/** @brief Handle new or changed association interfaces
 *
 * Called when either a new org.openbmc.Associations interface was
 * created, or the associations property on that interface changed
 *
 * @param[in,out] objectServer    - sdbus system object
 * @param[in] associations        - New associations to look at for change
 * @param[in] path                - Path of the object that contains the
 *                                  org.openbmc.Associations
 * @param[in] owner               - The Dbus service having it's associatons
 *                                  changed
 * @param[in,out] assocOwners     - Owners of associations
 * @param[in,out] assocInterfaces - Associations endpoints
 *
 * @return Void, objectServer and assocOwners updated if needed
 */
void associationChanged(sdbusplus::asio::object_server& objectServer,
                        const std::vector<Association>& associations,
                        const std::string& path, const std::string& owner,
                        AssociationOwnersType& assocOwners,
                        AssociationInterfaces& assocInterfaces);
