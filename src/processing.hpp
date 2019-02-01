#pragma once

#include <boost/container/flat_map.hpp>
#include <string>

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