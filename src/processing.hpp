#pragma once

#include "associations.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <string>

/** @brief Define white list and black list data structure */
using WhiteBlackList = boost::container::flat_set<std::string>;

/** @brief Dbus interface which contains org.openbmc Associations */
constexpr const char* ASSOCIATIONS_INTERFACE = "org.openbmc.Associations";

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

/** @brief InterfacesAdded represents the dbus data from the signal
 *
 * There are 2 pairs
 * pair1: D-bus Interface,vector[pair2]
 * pair2: D-bus Method,vector[Associations]
 */
using InterfacesAdded = std::vector<std::pair<
    std::string,
    std::vector<std::pair<
        std::string, sdbusplus::message::variant<std::vector<Association>>>>>>;

/** @brief Get well known name of input unique name
 *
 * If user passes in well known name then that will be returned.
 *
 * @param[in] owners       - Current list of owners
 * @param[in] request      - The name to look up
 * @param[out] wellKnown   - The well known name if found
 *
 * @return True if well known name is found, false otherwise
 */
bool getWellKnown(
    const boost::container::flat_map<std::string, std::string>& owners,
    const std::string& request, std::string& well_known);

/** @brief Determine if dbus service is something to monitor
 *
 * mapper supports a whitelist and blacklist concept. If a whitelist is provided
 * as input then only dbus objects matching that list is monitored. If a
 * blacklist is provided then objects matching it will not be monitored.
 *
 * @param[in] processName   - Dbus service name
 * @param[in] whiteList     - The white list
 * @param[in] blackList     - The black list
 *
 * @return True if input process_name should be monitored, false otherwise
 */
bool needToIntrospect(const std::string& processName,
                      const WhiteBlackList& whiteList,
                      const WhiteBlackList& blackList);

/** @brief Handle the removal of an existing name in objmgr data structures
 *
 * @param[in,out] nameOwners      - Map of unique name to well known name
 * @param[in]     wellKnown       - Well known name that has new owner
 * @param[in]     oldOwner        - Old unique name
 * @param[in,out] interfaceMap    - Map of interfaces
 * @param[in,out] assocOwners     - Owners of associations
 * @param[in,out] assocInterfaces - Associations endpoints
 * @param[in,out] server          - sdbus system object
 *
 */
void processNameChangeDelete(
    boost::container::flat_map<std::string, std::string>& nameOwners,
    const std::string& wellKnown, const std::string& oldOwner,
    interface_map_type& interfaceMap, AssociationOwnersType& assocOwners,
    AssociationInterfaces& assocInterfaces,
    sdbusplus::asio::object_server& server);

/** @brief Handle an interfaces added signal
 *
 * @param[in,out] interfaceMap    - Global map of interfaces
 * @param[in]     objPath         - New path to process
 * @param[in]     interfacesAdded - New interfaces to process
 * @param[in]     wellKnown       - Well known name that has new owner
 * @param[in,out] assocOwners     - Owners of associations
 * @param[in,out] assocInterfaces - Associations endpoints
 * @param[in,out] server          - sdbus system object
 *
 */
void processInterfaceAdded(interface_map_type& interfaceMap,
                           const sdbusplus::message::object_path& objPath,
                           const InterfacesAdded& intfAdded,
                           const std::string& wellKnown,
                           AssociationOwnersType& assocOwners,
                           AssociationInterfaces& assocInterfaces,
                           sdbusplus::asio::object_server& server);
