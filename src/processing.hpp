#pragma once

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <src/associations.hpp>
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
bool get_well_known(
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
bool need_to_introspect(const std::string& processName,
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
void process_name_change_delete(
    boost::container::flat_map<std::string, std::string>& nameOwners,
    const std::string& wellKnown, const std::string& oldOwner,
    interface_map_type& interfaceMap, AssociationOwnersType& assocOwners,
    AssociationInterfaces& assocInterfaces,
    sdbusplus::asio::object_server& server);
