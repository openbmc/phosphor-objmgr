#pragma once

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <string>

/** @brief Define white list and black list data structure */
using WhiteBlackList = boost::container::flat_set<std::string>;

/** @brief Get well known name of input unique name
 *
 * If user passes in well known name then that will be returned.
 *
 * @param[in] owners       - Current list of owners
 * @param[in] request      - The name to look up
 * @param[out] well_known  - The well known name if found
 *
 * @return True if well known name is found, false otherwise
 */
bool get_well_known(
    boost::container::flat_map<std::string, std::string>& owners,
    const std::string& request, std::string& well_known);

/** @brief Determine if dbus service is something to monitor
 *
 * mapper supports a whitelist and blacklist concept. If a whitelist is provided
 * as input then only dbus objects matching that list is monitored. If a
 * blacklist is provided then objects matching it will not be monitored.
 *
 * @param[in] process_name  - Dbus service name
 * @param[in] whiteList     - The white list
 * @param[in] blackList     - The black list
 *
 * @return True if input process_name should be monitored, false otherwise
 */
bool need_to_introspect(const std::string& process_name,
                        const WhiteBlackList& whiteList,
                        const WhiteBlackList& blackList);