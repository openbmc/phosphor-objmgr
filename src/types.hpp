#pragma once

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <memory>
#include <sdbusplus/asio/object_server.hpp>
#include <string>
#include <tuple>
#include <vector>

/** @brief interface_map_type is the underlying datastructure the mapper uses.
 *
 * The 3 levels of map are
 * object paths
 *   connection names
 *      interface names
 */
using interface_map_type = boost::container::flat_map<
    std::string, boost::container::flat_map<
                     std::string, boost::container::flat_set<std::string>>>;

/**
 *  Associations and some metadata are stored in associationInterfaces.
 *  The fields are:
 *   * ifacePos - holds the D-Bus interface object
 *   * endpointsPos - holds the endpoints array that shadows the property
 */
static constexpr auto ifacePos = 0;
static constexpr auto endpointsPos = 1;
using Endpoints = std::vector<std::string>;

// map[interface path: tuple[dbus_interface,vector[endpoint paths]]]
using AssociationInterfaces = boost::container::flat_map<
    std::string,
    std::tuple<std::shared_ptr<sdbusplus::asio::dbus_interface>, Endpoints>>;

/**
 * The associationOwners map contains information about creators of
 * associations, so that when a org.openbmc.Association interface is
 * removed or its 'associations' property is changed, the mapper owned
 * association objects can be correctly handled.  It is a map of the
 * object path of the org.openbmc.Association owner to a map of the
 * service the path is owned by, to a map of the association objects to
 * their endpoint paths:
 * map[ownerPath : map[service : map[assocPath : [endpoint paths]]]
 * For example:
 * [/logging/entry/1 :
 *   [xyz.openbmc_project.Logging :
 *     [/logging/entry/1/callout : [/system/cpu0],
 *      /system/cpu0/fault : [/logging/entry/1]]]]
 */
using AssociationPaths =
    boost::container::flat_map<std::string,
                               boost::container::flat_set<std::string>>;

using AssociationOwnersType = boost::container::flat_map<
    std::string, boost::container::flat_map<std::string, AssociationPaths>>;

/**
 * Store the contents of the associations property on the interface
 * For example:
 * ["inventory", "activation", "/xyz/openbmc_project/inventory/system/chassis"]
 */
using Association = std::tuple<std::string, std::string, std::string>;

/**
 * Keeps all association related maps together.
 */
struct AssociationMaps
{
    AssociationInterfaces ifaces;
    AssociationOwnersType owners;
};
