#pragma once

#include "types.hpp"

#include <boost/container/flat_map.hpp>

#include <cassert>
#include <span>
#include <string>
#include <vector>

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

/** @brief Get the well known names of input unique name
 *
 * If user passes in well known name then that will be returned.
 *
 * @param[in] owners       - Current list of owners
 * @param[in] request      - The name to look up
 *
 * @return The well known names owned by request. Empty if request is a
 *         unique name for which no well known names are known. The result
 *         refers to request or to the elements of owners and hence is only
 *         valid as long as request and owners are not modified or destroyed.
 */
std::span<const std::string> getWellKnownNames(const NameOwners& owners,
                                               const std::string& request);

/** @brief Whether or not a service implements an interface on a path
 *
 * @param[in] interfaceMap - Global map of interfaces
 * @param[in] path         - Object path
 * @param[in] service      - Well known name of the service
 * @param[in] interface    - Interface name
 *
 * @return True if service implements interface on path, false otherwise
 */
bool hasInterface(const InterfaceMapType& interfaceMap, const std::string& path,
                  const std::string& service, const std::string& interface);

/** @brief Process a change of the associations property
 *
 * Apply the new value of the associations property of path to all
 * well-known names of sender that implement the association definitions
 * interface on path. Ignore the change for other well-known names.
 *
 * @param[in] io           - io context
 * @param[in] nameOwners   - Map of unique name to well known names
 * @param[in] sender       - Sender of the PropertiesChanged signal
 * @param[in] path         - Object path of the associations property
 * @param[in] associations - New value of the associations property
 * @param[in] interfaceMap     - Global map of interfaces
 * @param[in,out] assocMaps    - The association maps
 * @param[in,out] server       - sdbus system object
 */
void processAssociationsChanged(
    boost::asio::io_context& io, const NameOwners& nameOwners,
    const std::string& sender, const std::string& path,
    const std::vector<Association>& associations,
    const InterfaceMapType& interfaceMap, AssociationMaps& assocMaps,
    sdbusplus::asio::object_server& server);

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
 * @param[in,out] nameOwners      - Map of unique name to well known names
 * @param[in]     wellKnown       - Well known name that has new owner
 * @param[in]     oldOwner        - Old unique name
 * @param[in,out] interfaceMap    - Map of interfaces
 * @param[in,out] assocMaps       - The association maps
 * @param[in,out] server          - sdbus system object
 *
 */
void processNameChangeDelete(
    boost::asio::io_context& io, NameOwners& nameOwners,
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
    const sdbusplus::object_path& objPath, const InterfacesAdded& intfAdded,
    const std::string& wellKnown, AssociationMaps& assocMaps,
    sdbusplus::asio::object_server& server);

/** @brief Remove parents of the passed in path that are no longer needed
 *
 * Remove parents of objectPath for owner that only have the default
 * interfaces or no interfaces at all and that have no other children
 * for owner.
 *
 * @param[in]     objectPath    - Path of which to remove the parents
 * @param[in]     owner         - Well known name of the service
 * @param[in,out] interfaceMap  - Global map of interfaces
 */
void removeUnneededParents(const sdbusplus::object_path& objectPath,
                           const std::string& owner,
                           InterfaceMapType& interfaceMap);

/** @brief Handle an interfaces removed signal
 *
 * @param[in] io                    - io context
 * @param[in,out] interfaceMap      - Global map of interfaces
 * @param[in]     objPath           - Path from which interfaces are removed
 * @param[in]     interfacesRemoved - Interfaces that are removed
 * @param[in]     sender            - Well known name of the signal sender
 * @param[in,out] assocMaps         - The association maps
 * @param[in,out] server            - sdbus system object
 *
 */
void processInterfacesRemoved(
    boost::asio::io_context& io, InterfaceMapType& interfaceMap,
    const sdbusplus::object_path& objPath,
    const std::vector<std::string>& interfacesRemoved,
    const std::string& sender, AssociationMaps& assocMaps,
    sdbusplus::asio::object_server& server);
