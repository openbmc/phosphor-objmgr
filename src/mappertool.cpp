// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

/**
 * mappertool - CLI utility for inspecting D-Bus associations via
 * xyz.openbmc_project.ObjectMapper.
 *
 * Usage:
 *   mappertool assocs                    dump all associations
 *   mappertool assocs -n <str>           filter by path containing <str>
 *   mappertool assocs -t <type>          filter by association type
 *   mappertool assocs -p                 show raw assoc paths
 *   mappertool getobject <path>          show services/interfaces for a path
 *   mappertool getobject <path> -i <if>  restrict to services with interface
 */

#include <CLI/CLI.hpp>
#include <sdbusplus/bus.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <print>
#include <set>
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
static constexpr auto mapperObj = "/xyz/openbmc_project/object_mapper";
static constexpr auto mapperIface = "xyz.openbmc_project.ObjectMapper";
static constexpr auto objManagerIface = "org.freedesktop.DBus.ObjectManager";
static constexpr auto assocIface = "xyz.openbmc_project.Association";

// An unordered pair of object paths, canonicalized so (a,b) == (b,a).
// We do not attempt to match specific forward/reverse type names to each other
// (e.g. "contains" <-> "contained_by") because that correspondence is only a
// YAML naming convention, not something present in the bus data. Instead we
// report the full set of association types seen in each direction.
struct PathPairKey
{
    sdbusplus::object_path a;
    sdbusplus::object_path b;

    bool operator<(const PathPairKey& other) const
    {
        return std::tie(a, b) < std::tie(other.a, other.b);
    }
};

struct PairInfo
{
    std::set<std::string> aToB; // association types where a is the owner
    std::set<std::string> bToA; // association types where b is the owner
    std::vector<sdbusplus::object_path> aToBPaths;
    std::vector<sdbusplus::object_path> bToAPaths;
};

static PathPairKey makeKey(const sdbusplus::object_path& x,
                           const sdbusplus::object_path& y)
{
    return (std::string(x) < std::string(y)) ? PathPairKey{x, y}
                                             : PathPairKey{y, x};
}

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

    // Gather all paths that have xyz.openbmc_project.Association, i.e. every
    // directed edge: assocPath (owner/type) -> list of endpoint paths.
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
        const auto* eps = std::get_if<std::vector<std::string>>(&epIt->second);
        if (eps == nullptr)
        {
            continue;
        }
        std::vector<sdbusplus::object_path> epPaths(eps->begin(), eps->end());
        assocEndpoints[objPath] = std::move(epPaths);
    }

    // Fold every directed edge into an unordered-pair bucket, recording
    // which side each type came from.
    std::map<PathPairKey, PairInfo> pairs;
    for (const auto& [assocPath, endpoints] : assocEndpoints)
    {
        const sdbusplus::object_path owner = assocPath.parent_path();
        const std::string type = assocPath.filename();
        if (owner == "/")
        {
            continue;
        }

        for (const auto& endpoint : endpoints)
        {
            PathPairKey key = makeKey(owner, endpoint);
            PairInfo& info = pairs[key];
            if (owner == key.a)
            {
                info.aToB.insert(type);
                info.aToBPaths.push_back(assocPath);
            }
            else
            {
                info.bToA.insert(type);
                info.bToAPaths.push_back(assocPath);
            }
        }
    }

    // Apply filters and collect matching rows.
    std::vector<std::pair<PathPairKey, PairInfo>> rows;
    for (auto& [key, info] : pairs)
    {
        if (!nameFilter.empty() && !std::string(key.a).contains(nameFilter) &&
            !std::string(key.b).contains(nameFilter))
        {
            continue;
        }

        if (!typeFilter.empty())
        {
            if (!info.aToB.contains(typeFilter) &&
                !info.bToA.contains(typeFilter))
            {
                continue;
            }
        }

        rows.emplace_back(key, std::move(info));
    }

    auto joinTypes = [](const std::set<std::string>& types) -> std::string {
        if (types.empty())
        {
            return "(none)";
        }
        std::string out;
        for (const auto& t : types)
        {
            if (!out.empty())
            {
                out += ", ";
            }
            out += t;
        }
        return out;
    };

    if (pathMode)
    {
        for (const auto& [key, info] : rows)
        {
            std::string aTypes = joinTypes(info.aToB);
            std::string bTypes = joinTypes(info.bToA);
            std::string aStr = info.aToB.size() == 1
                                   ? std::string(key.a) + "/" + aTypes
                                   : std::string(key.a) + "/(" + aTypes + ")";
            std::string bStr = info.bToA.size() == 1
                                   ? std::string(key.b) + "/" + bTypes
                                   : std::string(key.b) + "/(" + bTypes + ")";
            std::println("{} <-> {}", aStr, bStr);
        }
        return;
    }

    // Two-line grouped format: pathA  | types (a->b)
    //                          pathB  | types (b->a)
    // Column width derived from the longest path across all rows.
    std::size_t pathWidth = 0;
    for (const auto& [key, info] : rows)
    {
        pathWidth = std::max(pathWidth, std::string(key.a).size());
        pathWidth = std::max(pathWidth, std::string(key.b).size());
    }

    bool first = true;
    for (const auto& [key, info] : rows)
    {
        if (!first)
        {
            std::println("");
        }
        first = false;
        std::println("{:{}}  | {}", std::string(key.a), pathWidth,
                     joinTypes(info.aToB));
        std::println("{:{}}  | {}", std::string(key.b), pathWidth,
                     joinTypes(info.bToA));
    }
}

static void getObject(const std::string& path,
                      const std::vector<std::string>& interfaces)
{
    auto bus = sdbusplus::bus::new_default();

    auto req =
        bus.new_method_call(mapperService, mapperObj, mapperIface, "GetObject");
    req.append(path, interfaces);

    // a{sas}: map of service name -> list of interfaces
    std::map<std::string, std::vector<std::string>> result;
    try
    {
        auto reply = bus.call(req);
        reply.read(result);
    }
    catch (const sdbusplus::exception_t& e)
    {
        std::println(stderr, "GetObject failed: {}", e.what());
        return;
    }

    bool first = true;
    for (const auto& [service, ifaces] : result)
    {
        if (ifaces.empty())
        {
            continue;
        }
        if (!first)
        {
            std::println("");
        }
        first = false;
        std::println("{}", service);
        for (const auto& iface : ifaces)
        {
            std::println("  {}", iface);
        }
    }
}

int main(int argc, char** argv)
{
    CLI::App app{"mappertool - OpenBMC mapper inspection utility"};
    app.require_subcommand(1);

    // assocs subcommand
    auto* assocs = app.add_subcommand("assocs", "Dump association objects");

    std::string nameFilter;
    assocs->add_option("-n,--name", nameFilter,
                       "Show only associations where the path contains <str>");

    std::string typeFilter;
    assocs->add_option(
        "-t,--type", typeFilter,
        "Show only associations where any type (either direction) matches");

    bool pathMode = false;
    assocs->add_flag("-p,--paths", pathMode,
                     "Print raw association paths instead of human form");

    assocs->callback([&]() { dumpAssocs(nameFilter, typeFilter, pathMode); });

    // getobject subcommand
    auto* getobj = app.add_subcommand(
        "getobject", "Show which services implement a given object path");

    std::string objPath;
    getobj->add_option("path", objPath, "D-Bus object path to look up")
        ->required();

    std::vector<std::string> ifaceFilter;
    getobj->add_option(
        "-i,--interface", ifaceFilter,
        "Restrict to services implementing this interface (repeatable)");

    getobj->callback([&]() { getObject(objPath, ifaceFilter); });

    CLI11_PARSE(app, argc, argv);
    return 0;
}
