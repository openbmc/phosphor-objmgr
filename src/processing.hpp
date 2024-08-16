#pragma once

#include "associations.hpp"
#include "types.hpp"

#include <boost/container/flat_map.hpp>

#include <cassert>
#include <string>

/** @brief The associations definitions interface */
constexpr const char* assocDefsInterface =
    "xyz.openbmc_project.Association.Definitions";

/** @brief The associations definitions property name */
constexpr const char* assocDefsProperty = "Associations";

/** @brief InterfacesAdded represents the dbus data from the signal
 *
 * There are 2 pairs
 * pair1: D-bus Interface,vector[pair2]
 * pair2: D-bus Method,vector[Associations]
 */
using InterfacesAdded = std::vector<std::pair<
    std::string, std::vector<std::pair<
                     std::string, std::variant<std::vector<Association>>>>>>;

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
    const std::string& request, std::string& wellKnown);

/** @brief Determine if dbus service is something to monitor
 *
 * mapper does not monitor all DBus services.  needToIntrospect determines
 * whether or not a service is to be monitored.
 *
 * @param[in] processName   - Dbus service name
 *
 * @return True if input processName should be monitored, false otherwise
 */
bool needToIntrospect(const std::string& processName);

/** @brief Handle the removal of an existing name in objmgr data structures
 *
 * @param[in] io                  - io context
 * @param[in,out] nameOwners      - Map of unique name to well known name
 * @param[in]     wellKnown       - Well known name that has new owner
 * @param[in]     oldOwner        - Old unique name
 * @param[in,out] interfaceMap    - Map of interfaces
 * @param[in,out] assocMaps       - The association maps
 * @param[in,out] server          - sdbus system object
 *
 */
void processNameChangeDelete(
    boost::asio::io_context& io,
    boost::container::flat_map<std::string, std::string>& nameOwners,
    const std::string& wellKnown, const std::string& oldOwner,
    InterfaceMapType& interfaceMap, AssociationMaps& assocMaps,
    sdbusplus::asio::object_server& server);

/** @brief Handle an interfaces added signal
 *
 * @param[in] io                  - io context
 * @param[in,out] interfaceMap    - Global map of interfaces
 * @param[in]     objPath         - New path to process
 * @param[in]     interfacesAdded - New interfaces to process
 * @param[in]     wellKnown       - Well known name that has new owner
 * @param[in,out] assocMaps       - The association maps
 * @param[in,out] server          - sdbus system object
 *
 */
void processInterfaceAdded(
    boost::asio::io_context& io, InterfaceMapType& interfaceMap,
    const sdbusplus::message::object_path& objPath,
    const InterfacesAdded& intfAdded, const std::string& wellKnown,
    AssociationMaps& assocMaps, sdbusplus::asio::object_server& server);
