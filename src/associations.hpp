#pragma once

#include "types.hpp"

constexpr const char* XYZ_ASSOCIATION_INTERFACE =
    "xyz.openbmc_project.Association";

/** @brief Remove input association
 *
 * @param[in] sourcePath          - Path of the object that contains the
 *                                  org.openbmc.Associations
 * @param[in] owner               - The Dbus service having its associations
 *                                  removed
 * @param[in,out] server          - sdbus system object
 * @param[in,out] assocMaps       - The association maps
 *
 * @return Void, server, assocMaps updated if needed
 */
void removeAssociation(const std::string& sourcePath, const std::string& owner,
                       sdbusplus::asio::object_server& server,
                       AssociationMaps& assocMaps);

/** @brief Remove input paths from endpoints of an association
 *
 * If the last endpoint was removed, then remove the whole
 * association object, otherwise just set the property
 *
 * @param[in] objectServer        - sdbus system object
 * @param[in] assocPath           - Path of the object that contains the
 *                                  org.openbmc.Associations
 * @param[in] endpointsToRemove   - Endpoints to remove
 * @param[in,out] assocMaps       - The association maps
 *
 * @return Void, objectServer and assocMaps updated if needed
 */
void removeAssociationEndpoints(
    sdbusplus::asio::object_server& objectServer, const std::string& assocPath,
    const boost::container::flat_set<std::string>& endpointsToRemove,
    AssociationMaps& assocMaps);

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
 * @param[in,out] assocMaps      - The association maps
 *
 * @return Void, objectServer and assocMaps updated if needed
 */
void checkAssociationEndpointRemoves(
    const std::string& sourcePath, const std::string& owner,
    const AssociationPaths& newAssociations,
    sdbusplus::asio::object_server& objectServer, AssociationMaps& assocMaps);

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
 * @param[in] interfaceMap        - The full interface map
 * @param[in,out] assocMaps       - The association maps
 *
 * @return Void, objectServer and assocMaps updated if needed
 */
void associationChanged(sdbusplus::asio::object_server& objectServer,
                        const std::vector<Association>& associations,
                        const std::string& path, const std::string& owner,
                        const interface_map_type& interfaceMap,
                        AssociationMaps& assocMaps);

/** @brief Add a pending associations entry
 *
 *  Used when a client wants to create an association between
 *  2 D-Bus endpoint paths, but one of the paths doesn't exist.
 *  When the path does show up in D-Bus, if there is a pending
 *  association then the real association objects can be created.
 *
 * @param[in] objectPath    - The D-Bus object path that should be an
 *                            association endpoint but doesn't exist
 *                            on D-Bus.
 * @param[in] type          - The association type.  Gets used in the final
 *                            association path of <objectPath>/<type>.
 * @param[in] endpointPath  - The D-Bus path on the other side
 *                            of the association. This path exists.
 * @param[in] endpointType  - The endpoint association type. Gets used
 *                            in the final association path of
 *                            <endpointPath>/<endpointType>.
 * @param[in] owner         - The service name that owns the association.
 * @param[in,out] assocMaps - The association maps
 */
void addPendingAssociation(const std::string& objectPath,
                           const std::string& type,
                           const std::string& endpointPath,
                           const std::string& endpointType,
                           const std::string& owner,
                           AssociationMaps& assocMaps);
