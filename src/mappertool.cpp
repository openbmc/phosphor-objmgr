// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

/**
 * mappertool - CLI utility for inspecting D-Bus associations via
 * xyz.openbmc_project.ObjectMapper.
 *
 * Usage:
 *   mappertool assocs            dump all associations
 *   mappertool assocs -n <str>   filter: path must contain <str>
 *   mappertool assocs -t <type>  filter: forward or reverse type matches <type>
 *   mappertool assocs -p         path mode: show raw assoc paths
 */

#include <CLI/CLI.hpp>
#include <sdbusplus/bus.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <print>
#include <string>
#include <variant>
#include <vector>

// Property value variant covering all common D-Bus types (sv in
// GetManagedObjects).
using DbusVariantType =
    std::variant<std::vector<std::string>, std::string, bool, uint8_t, int16_t,
                 uint16_t, int32_t, uint32_t, int64_t, uint64_t, double,
                 std::vector<uint8_t>>;

// a{oa{sa{sv}}}
using ManagedObjectType =
    std::map<sdbusplus::object_path,
             std::map<std::string, std::map<std::string, DbusVariantType>>>;

static constexpr auto mapperService = "xyz.openbmc_project.ObjectMapper";
static constexpr auto mapperRoot = "/";
static constexpr auto objManagerIface = "org.freedesktop.DBus.ObjectManager";
static constexpr auto assocIface = "xyz.openbmc_project.Association";

struct AssocRow
{
    sdbusplus::object_path ownerPath;
    std::string fwdType;
    std::string revType;
    sdbusplus::object_path endpoint;
    sdbusplus::object_path assocPath;
    sdbusplus::object_path reverseAssocPath;
};

static void dumpAssocs(const std::string& nameFilter,
                       const std::string& typeFilter, bool pathMode)
{
    auto bus = sdbusplus::bus::new_default();

    auto req = bus.new_method_call(mapperService, mapperRoot, objManagerIface,
                                   "GetManagedObjects");
    ManagedObjectType objects;
    try
    {
        auto reply = bus.call(req);
        reply.read(objects);
    }
    catch (const sdbusplus::exception_t& e)
    {
        std::println(stderr, "GetManagedObjects failed: {}", e.what());
        return;
    }

    // Gather all paths that have xyz.openbmc_project.Association
    std::map<sdbusplus::object_path, std::vector<sdbusplus::object_path>>
        assocEndpoints;
    for (const auto& [objPath, interfaces] : objects)
    {
        auto assocIt = interfaces.find(assocIface);
        if (assocIt == interfaces.end())
        {
            continue;
        }
        auto epIt = assocIt->second.find("endpoints");
        if (epIt == assocIt->second.end())
        {
            continue;
        }
        const auto* eps =
            std::get_if<std::vector<std::string>>(&epIt->second);
        if (eps == nullptr)
        {
            continue;
        }
        std::vector<sdbusplus::object_path> epPaths(eps->begin(), eps->end());
        assocEndpoints[objPath] = std::move(epPaths);
    }

    // Resolve forward/reverse pairs into rows and apply filters.
    // Dedup by only keeping rows where assocPath < reverseAssocPath.
    // Assumption: each owner->endpoint pair has at most one reverse type.
    std::vector<AssocRow> rows;
    for (const auto& [assocPath, endpoints] : assocEndpoints)
    {
        const sdbusplus::object_path ownerPath = assocPath.parent_path();
        const std::string fwdType = assocPath.filename();
        if (std::string(ownerPath).empty() || ownerPath == "/")
        {
            continue;
        }

        for (const auto& endpoint : endpoints)
        {
            sdbusplus::object_path reverseAssocPath;
            std::string revType;
            const std::string prefix = std::string(endpoint) + "/";
            for (auto it = assocEndpoints.lower_bound(endpoint);
                 it != assocEndpoints.end() &&
                 std::string(it->first).starts_with(prefix);
                 ++it)
            {
                for (const auto& ep : it->second)
                {
                    if (ep == ownerPath)
                    {
                        reverseAssocPath = it->first;
                        revType = it->first.filename();
                        break;
                    }
                }
                if (!std::string(reverseAssocPath).empty())
                {
                    break;
                }
            }

            if (!std::string(reverseAssocPath).empty() &&
                reverseAssocPath < assocPath)
            {
                continue;
            }

            if (!nameFilter.empty() &&
                !std::string(ownerPath).contains(nameFilter) &&
                !std::string(endpoint).contains(nameFilter))
            {
                continue;
            }

            if (!typeFilter.empty() && fwdType != typeFilter &&
                revType != typeFilter)
            {
                continue;
            }

            rows.push_back({ownerPath, fwdType, revType, endpoint, assocPath,
                            reverseAssocPath});
        }
    }

    if (pathMode)
    {
        for (const auto& r : rows)
        {
            if (std::string(r.reverseAssocPath).empty())
            {
                std::println("{} <-> (no reverse found)",
                             std::string(r.assocPath));
            }
            else
            {
                std::println("{} <-> {}", std::string(r.assocPath),
                             std::string(r.reverseAssocPath));
            }
        }
        return;
    }

    // Two-line grouped format: ownerPath  fwdType
    //                          endpoint   revType
    // Column width derived from the longest path across all pairs.
    std::size_t pathWidth = 0;
    for (const auto& r : rows)
    {
        pathWidth = std::max(pathWidth, std::string(r.ownerPath).size());
        pathWidth = std::max(pathWidth, std::string(r.endpoint).size());
    }

    bool first = true;
    for (const auto& r : rows)
    {
        if (!first)
        {
            std::println("");
        }
        first = false;
        const std::string& rev =
            r.revType.empty() ? "(no reverse)" : r.revType;
        std::println("{:{}}  | {}", std::string(r.ownerPath), pathWidth,
                     r.fwdType);
        std::println("{:{}}  | {}", std::string(r.endpoint), pathWidth, rev);
    }
}

int main(int argc, char** argv)
{
    CLI::App app{"mappertool - OpenBMC mapper inspection utility"};
    app.require_subcommand(1);

    auto* assocs = app.add_subcommand("assocs", "Dump association objects");

    std::string nameFilter;
    assocs->add_option("-n,--name", nameFilter,
                       "Show only associations where the path contains <str>");

    std::string typeFilter;
    assocs->add_option(
        "-t,--type", typeFilter,
        "Show only associations where the forward or reverse type matches");

    bool pathMode = false;
    assocs->add_flag("-p,--paths", pathMode,
                     "Print raw association paths instead of human form");

    assocs->callback([&]() { dumpAssocs(nameFilter, typeFilter, pathMode); });

    CLI11_PARSE(app, argc, argv);
    return 0;
}
