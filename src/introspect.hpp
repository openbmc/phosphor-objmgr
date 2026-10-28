#pragma once

#include <optional>
#include <string>
#include <vector>

/** @brief The information extracted from an introspection XML document */
struct IntrospectResult
{
    /** @brief Names of the interfaces of the introspected object */
    std::vector<std::string> interfaces;
    /** @brief Object paths of the children of the introspected object */
    std::vector<std::string> childPaths;
};

/** @brief Parse the reply of an org.freedesktop.DBus.Introspectable call
 *
 * Elements without a name attribute and child nodes with a name that is not
 * a valid object path element are ignored.
 *
 * @param[in] path - Object path that has been introspected
 * @param[in] xml  - Introspection XML document
 *
 * @return The interfaces and children of path or std::nullopt if the XML
 *         document could not be parsed.
 */
std::optional<IntrospectResult> parseIntrospectXml(const std::string& path,
                                                   const std::string& xml);
