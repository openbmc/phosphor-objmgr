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

// Property value variant covering all common D-Bus types (sv in GetManagedObjects).
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
    std::string ownerPath;
    std::string fwdType;
    std::string revType;
    std::string endpoint;
    std::string assocPath;
    std::string reverseAssocPath;
};

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

    assocs->callback([&]() {
        auto bus = sdbusplus::bus::new_default();

        auto req = bus.new_method_call(mapperService, mapperRoot,
                                       objManagerIface, "GetManagedObjects");
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
        std::map<std::string, std::vector<std::string>> assocEndpoints;
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
            assocEndpoints[objPath.string()] = *eps;
        }

        // Resolve forward/reverse pairs into rows and apply filters.
        // Dedup by only keeping rows where assocPath < reverseAssocPath.
        // Assumption: each owner->endpoint pair has at most one reverse type.
        std::vector<AssocRow> rows;
        for (const auto& [assocPath, endpoints] : assocEndpoints)
        {
            auto lastSlash = assocPath.rfind('/');
            if (lastSlash == std::string::npos || lastSlash == 0)
            {
                continue;
            }
            const std::string ownerPath = assocPath.substr(0, lastSlash);
            const std::string fwdType = assocPath.substr(lastSlash + 1);

            for (const auto& endpoint : endpoints)
            {
                std::string reverseAssocPath;
                std::string revType;
                const std::string prefix = endpoint + "/";
                for (auto it = assocEndpoints.lower_bound(prefix);
                     it != assocEndpoints.end() &&
                     it->first.starts_with(prefix);
                     ++it)
                {
                    for (const auto& ep : it->second)
                    {
                        if (ep == ownerPath)
                        {
                            reverseAssocPath = it->first;
                            revType = it->first.substr(prefix.size());
                            break;
                        }
                    }
                    if (!reverseAssocPath.empty())
                    {
                        break;
                    }
                }

                if (!reverseAssocPath.empty() && assocPath >= reverseAssocPath)
                {
                    continue;
                }

                if (!nameFilter.empty() && !ownerPath.contains(nameFilter) &&
                    !endpoint.contains(nameFilter))
                {
                    continue;
                }

                if (!typeFilter.empty() && fwdType != typeFilter &&
                    revType != typeFilter)
                {
                    continue;
                }

                rows.push_back({ownerPath, fwdType, revType, endpoint,
                                assocPath, reverseAssocPath});
            }
        }

        if (pathMode)
        {
            for (const auto& r : rows)
            {
                if (r.reverseAssocPath.empty())
                {
                    std::println("{} <-> (no reverse found)", r.assocPath);
                }
                else
                {
                    std::println("{} <-> {}", r.assocPath, r.reverseAssocPath);
                }
            }
            return;
        }

        // Compute column widths from actual data then print aligned
        std::size_t ownerWidth = 0;
        std::size_t fwdWidth = 0;
        std::size_t revWidth = 0;
        for (const auto& r : rows)
        {
            ownerWidth = std::max(ownerWidth, r.ownerPath.size());
            fwdWidth = std::max(fwdWidth, r.fwdType.size());
            revWidth = std::max(revWidth,
                                r.revType.empty()
                                    ? std::string_view("(no reverse)").size()
                                    : r.revType.size());
        }

        for (const auto& r : rows)
        {
            const std::string& rev =
                r.revType.empty() ? "(no reverse)" : r.revType;
            std::println("{:{}}  {:{}}  <->  {:{}}  {}", r.ownerPath,
                         ownerWidth, r.fwdType, fwdWidth, rev, revWidth,
                         r.endpoint);
        }
    });

    CLI11_PARSE(app, argc, argv);
    return 0;
}
