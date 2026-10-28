#include "associations.hpp"
#include "handler.hpp"
#include "introspect.hpp"
#include "processing.hpp"
#include "types.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/container/flat_map.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

static void updateOwners(
    sdbusplus::asio::connection* conn,
    boost::container::flat_map<std::string, std::string>& owners,
    const std::string& newObject)
{
    if (newObject.starts_with(":"))
    {
        return;
    }
    conn->async_method_call(
        [&, newObject](const boost::system::error_code ec,
                       const std::string& nameOwner) {
            if (ec)
            {
                std::cerr << "Error getting owner of " << newObject << " : "
                          << ec << "\n";
                return;
            }
            owners[nameOwner] = newObject;
        },
        "org.freedesktop.DBus", "/", "org.freedesktop.DBus", "GetNameOwner",
        newObject);
}

static void sendIntrospectionCompleteSignal(
    sdbusplus::asio::connection* systemBus, const std::string& processName)
{
    // TODO(ed) This signal doesn't get exposed properly in the
    // introspect right now.  Find out how to register signals in
    // sdbusplus
    sdbusplus::message_t m = systemBus->new_signal(
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper.Private", "IntrospectionComplete");
    m.append(processName);
    m.signal_send();
}

struct InProgressIntrospect
{
    InProgressIntrospect() = delete;
    InProgressIntrospect(const InProgressIntrospect&) = delete;
    InProgressIntrospect(InProgressIntrospect&&) = delete;
    InProgressIntrospect& operator=(const InProgressIntrospect&) = delete;
    InProgressIntrospect& operator=(InProgressIntrospect&&) = delete;
    InProgressIntrospect(
        sdbusplus::asio::connection* systemBusConnection,
        boost::asio::io_context& ioContext,
        const std::string& introspectProcessName, AssociationMaps& am,
        std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
            globalIntrospectStartTime) :
        systemBus(systemBusConnection), io(ioContext),
        processName(introspectProcessName), assocMaps(am),
        globalStartTime(std::move(globalIntrospectStartTime)),
        processStartTime(std::nullopt)
    {
        if constexpr (MAPPER_ENABLE_DEBUG)
        {
            processStartTime = std::chrono::steady_clock::now();
        }
    }
    ~InProgressIntrospect()
    {
        try
        {
            sendIntrospectionCompleteSignal(systemBus, processName);
            if constexpr (!MAPPER_ENABLE_DEBUG)
            {
                return;
            }
            if (!processStartTime.has_value())
            {
                return;
            }
            std::chrono::duration<float> diff =
                std::chrono::steady_clock::now() - processStartTime.value();
            std::cout << std::setw(50) << processName << " scan took "
                      << diff.count() << " seconds\n";

            // If we're the last outstanding caller globally, calculate the
            // time it took
            if (globalStartTime != nullptr && globalStartTime.use_count() == 1)
            {
                diff = std::chrono::steady_clock::now() - *globalStartTime;
                std::cout << "Total scan took " << diff.count()
                          << " seconds to complete\n";
            }
        }
        catch (const std::exception& e)
        {
            std::cerr
                << "Terminating, unhandled exception while introspecting: "
                << e.what() << "\n";
            std::terminate();
        }
        catch (...)
        {
            std::cerr
                << "Terminating, unhandled exception while introspecting\n";
            std::terminate();
        }
    }
    sdbusplus::asio::connection* systemBus;
    boost::asio::io_context& io;
    std::string processName;
    AssociationMaps& assocMaps;

    // only relevant for MAPPER_ENABLE_DEBUG
    std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
        globalStartTime;
    std::optional<std::chrono::time_point<std::chrono::steady_clock>>
        processStartTime;
};

static void doAssociations(
    boost::asio::io_context& io, sdbusplus::asio::connection* systemBus,
    InterfaceMapType& interfaceMap, AssociationMaps& assocMaps,
    sdbusplus::asio::object_server& objectServer,
    const std::string& processName, const std::string& path,
    int timeoutRetries = 0)
{
    constexpr int maxTimeoutRetries = 3;
    systemBus->async_method_call(
        [&io, &objectServer, path, processName, &interfaceMap, &assocMaps,
         systemBus, timeoutRetries](
            const boost::system::error_code ec,
            const std::variant<std::vector<Association>>& variantAssociations) {
            if (ec)
            {
                if (ec.value() == boost::system::errc::timed_out &&
                    timeoutRetries < maxTimeoutRetries)
                {
                    doAssociations(io, systemBus, interfaceMap, assocMaps,
                                   objectServer, processName, path,
                                   timeoutRetries + 1);
                    return;
                }
                std::cerr << "Error getting associations from " << path
                          << " of process " << processName << ": " << ec << ", "
                          << ec.message() << "\n";
                return;
            }
            std::vector<Association> associations =
                std::get<std::vector<Association>>(variantAssociations);
            associationChanged(io, objectServer, associations, path,
                               processName, interfaceMap, assocMaps);
        },
        processName, path, "org.freedesktop.DBus.Properties", "Get",
        assocDefsInterface, assocDefsProperty);
}

static void doIntrospect(
    boost::asio::io_context& io, sdbusplus::asio::connection* systemBus,
    const std::shared_ptr<InProgressIntrospect>& transaction,
    InterfaceMapType& interfaceMap,
    sdbusplus::asio::object_server& objectServer, const std::string& path,
    int timeoutRetries = 0)
{
    constexpr int maxTimeoutRetries = 3;
    systemBus->async_method_call(
        [&io, &interfaceMap, &objectServer, transaction, path, systemBus,
         timeoutRetries](const boost::system::error_code ec,
                         const std::string& introspectXml) {
            if (ec)
            {
                if (ec.value() == boost::system::errc::timed_out &&
                    timeoutRetries < maxTimeoutRetries)
                {
                    doIntrospect(io, systemBus, transaction, interfaceMap,
                                 objectServer, path, timeoutRetries + 1);
                    return;
                }
                std::cerr << "Introspect call failed with error: " << ec << ", "
                          << ec.message()
                          << " on process: " << transaction->processName
                          << " path: " << path << "\n";
                return;
            }

            std::optional<IntrospectResult> result =
                parseIntrospectXml(path, introspectXml);
            if (!result)
            {
                return;
            }

            auto& thisPathMap = interfaceMap[path];
            for (const std::string& ifaceName : result->interfaces)
            {
                thisPathMap[transaction->processName].emplace(ifaceName);

                if (ifaceName == assocDefsInterface)
                {
                    doAssociations(io, systemBus, interfaceMap,
                                   transaction->assocMaps, objectServer,
                                   transaction->processName, path);
                }
            }

            // Check if this new path has a pending association that can
            // now be completed.
            checkIfPendingAssociation(io, path, interfaceMap,
                                      transaction->assocMaps, objectServer);

            for (const std::string& childPath : result->childPaths)
            {
                doIntrospect(io, systemBus, transaction, interfaceMap,
                             objectServer, childPath);
            }
        },
        transaction->processName, path, "org.freedesktop.DBus.Introspectable",
        "Introspect");
}

static void startNewIntrospect(
    sdbusplus::asio::connection* systemBus, boost::asio::io_context& io,
    InterfaceMapType& interfaceMap, const std::string& processName,
    AssociationMaps& assocMaps,
    const std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>&
        globalStartTime,
    sdbusplus::asio::object_server& objectServer)
{
    if (needToIntrospect(processName))
    {
        std::shared_ptr<InProgressIntrospect> transaction =
            std::make_shared<InProgressIntrospect>(systemBus, io, processName,
                                                   assocMaps, globalStartTime);

        doIntrospect(io, systemBus, transaction, interfaceMap, objectServer,
                     "/");
    }
}

static void doListNames(
    boost::asio::io_context& io, InterfaceMapType& interfaceMap,
    sdbusplus::asio::connection* systemBus,
    boost::container::flat_map<std::string, std::string>& nameOwners,
    AssociationMaps& assocMaps, sdbusplus::asio::object_server& objectServer)
{
    systemBus->async_method_call(
        [&io, &interfaceMap, &nameOwners, &objectServer, systemBus,
         &assocMaps](const boost::system::error_code ec,
                     std::vector<std::string> processNames) {
            if (ec)
            {
                std::cerr << "Error getting names: " << ec << "\n";
                std::exit(EXIT_FAILURE);
                return;
            }
            // Try to make startup consistent
            std::sort(processNames.begin(), processNames.end());
            std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
                globalStartTime = nullptr;
            if constexpr (MAPPER_ENABLE_DEBUG)
            {
                globalStartTime = std::make_shared<
                    std::chrono::time_point<std::chrono::steady_clock>>(
                    std::chrono::steady_clock::now());
            }
            for (const std::string& processName : processNames)
            {
                if (needToIntrospect(processName))
                {
                    startNewIntrospect(systemBus, io, interfaceMap, processName,
                                       assocMaps, globalStartTime,
                                       objectServer);
                    updateOwners(systemBus, nameOwners, processName);
                }
            }
        },
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "ListNames");
}

// sdbusplus invokes match callbacks from a noexcept function. Since any
// process on the bus can emit a signal with an unexpected signature, an
// exception thrown while handling a signal must not reach sdbusplus because
// that would terminate the mapper. Exceptions are typically thrown while
// reading the signal arguments, before any state has been modified. If an
// exception is thrown later on, the changes made before the exception was
// thrown are not undone.
static sdbusplus::bus::match_t::callback_t catchExceptions(
    const char* signalName, sdbusplus::bus::match_t::callback_t handler)
{
    return [signalName,
            handler = std::move(handler)](sdbusplus::message_t& message) {
        try
        {
            handler(message);
        }
        catch (const std::exception& e)
        {
            const char* sender = message.get_sender();
            std::cerr << "Failed to process " << signalName << " signal from "
                      << (sender != nullptr ? sender : "(unknown)") << ": "
                      << e.what() << "\n";
        }
    };
}

int main()
{
    boost::asio::io_context io;
    std::shared_ptr<sdbusplus::asio::connection> systemBus =
        std::make_shared<sdbusplus::asio::connection>(io);

    sdbusplus::asio::object_server server(systemBus);

    // Construct a signal set registered for process termination.
    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&io](const boost::system::error_code&, int) {
        io.stop();
    });

    InterfaceMapType interfaceMap;
    boost::container::flat_map<std::string, std::string> nameOwners;
    // The association maps hold references to the D-Bus connection through
    // the association interfaces. Hence, these must be destroyed before the
    // connection and the io_context.
    AssociationMaps associationMaps;

    auto nameChangeHandler = [&interfaceMap, &io, &nameOwners, &server,
                              &associationMaps,
                              systemBus](sdbusplus::message_t& message) {
        std::string name;     // well-known
        std::string oldOwner; // unique-name
        std::string newOwner; // unique-name

        message.read(name, oldOwner, newOwner);

        if (name.starts_with(':'))
        {
            // We should do nothing with unique-name connections.
            return;
        }

        if (!oldOwner.empty())
        {
            processNameChangeDelete(io, nameOwners, name, oldOwner,
                                    interfaceMap, associationMaps, server);
        }

        if (!newOwner.empty())
        {
            std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
                transaction = nullptr;
            if constexpr (MAPPER_ENABLE_DEBUG)
            {
                transaction = std::make_shared<
                    std::chrono::time_point<std::chrono::steady_clock>>(
                    std::chrono::steady_clock::now());
            }
            // New daemon added
            if (needToIntrospect(name))
            {
                nameOwners[newOwner] = name;
                startNewIntrospect(systemBus.get(), io, interfaceMap, name,
                                   associationMaps, transaction, server);
            }
        }
    };

    sdbusplus::match nameOwnerChanged(
        static_cast<sdbusplus::bus_t&>(*systemBus),
        sdbusplus::match_rules::nameOwnerChanged(),
        catchExceptions("NameOwnerChanged", std::move(nameChangeHandler)));

    auto interfacesAddedHandler = [&io, &interfaceMap, &nameOwners, &server,
                                   &associationMaps](
                                      sdbusplus::message_t& message) {
        sdbusplus::object_path objPath;
        InterfacesAdded interfacesAdded;
        message.read(objPath, interfacesAdded);
        std::string wellKnown;
        if (!getWellKnown(nameOwners, message.get_sender(), wellKnown))
        {
            return; // only introspect well-known
        }
        if (needToIntrospect(wellKnown))
        {
            processInterfaceAdded(io, interfaceMap, objPath, interfacesAdded,
                                  wellKnown, associationMaps, server);
        }
    };

    sdbusplus::match interfacesAdded(
        static_cast<sdbusplus::bus_t&>(*systemBus),
        sdbusplus::match_rules::interfacesAdded(),
        catchExceptions("InterfacesAdded", std::move(interfacesAddedHandler)));

    auto interfacesRemovedHandler = [&io, &interfaceMap, &nameOwners, &server,
                                     &associationMaps](
                                        sdbusplus::message_t& message) {
        sdbusplus::object_path objPath;
        std::vector<std::string> interfacesRemoved;
        message.read(objPath, interfacesRemoved);
        std::string sender;
        if (!getWellKnown(nameOwners, message.get_sender(), sender))
        {
            return;
        }
        processInterfacesRemoved(io, interfaceMap, objPath, interfacesRemoved,
                                 sender, associationMaps, server);
    };

    sdbusplus::match interfacesRemoved(
        static_cast<sdbusplus::bus_t&>(*systemBus),
        sdbusplus::match_rules::interfacesRemoved(),
        catchExceptions("InterfacesRemoved",
                        std::move(interfacesRemovedHandler)));

    auto associationChangedHandler =
        [&io, &server, &nameOwners, &interfaceMap,
         &associationMaps](sdbusplus::message_t& message) {
            std::string objectName;
            boost::container::flat_map<std::string,
                                       std::variant<std::vector<Association>>>
                values;
            message.read(objectName, values);
            auto prop = values.find(assocDefsProperty);
            if (prop != values.end())
            {
                std::vector<Association> associations =
                    std::get<std::vector<Association>>(prop->second);

                std::string wellKnown;
                if (!getWellKnown(nameOwners, message.get_sender(), wellKnown))
                {
                    return;
                }
                associationChanged(io, server, associations,
                                   sdbusplus::object_path(message.get_path()),
                                   wellKnown, interfaceMap, associationMaps);
            }
        };
    sdbusplus::match assocChangedMatch(
        static_cast<sdbusplus::bus_t&>(*systemBus),
        sdbusplus::match_rules::interface("org.freedesktop.DBus.Properties") +
            sdbusplus::match_rules::member("PropertiesChanged") +
            sdbusplus::match_rules::argN(0, assocDefsInterface),
        catchExceptions("PropertiesChanged",
                        std::move(associationChangedHandler)));

    std::shared_ptr<sdbusplus::asio::dbus_interface> iface =
        server.add_interface("/xyz/openbmc_project/object_mapper",
                             "xyz.openbmc_project.ObjectMapper");

    iface->register_method(
        "GetAncestors", [&interfaceMap](std::string& reqPath,
                                        std::vector<std::string>& interfaces) {
            return getAncestors(interfaceMap, reqPath, interfaces);
        });

    iface->register_method(
        "GetObject", [&interfaceMap](const std::string& path,
                                     std::vector<std::string>& interfaces) {
            return getObject(interfaceMap, path, interfaces);
        });

    iface->register_method(
        "GetSubTree", [&interfaceMap](std::string& reqPath, int32_t depth,
                                      std::vector<std::string>& interfaces) {
            return getSubTree(interfaceMap, reqPath, depth, interfaces);
        });

    iface->register_method(
        "GetSubTreePaths",
        [&interfaceMap](std::string& reqPath, int32_t depth,
                        std::vector<std::string>& interfaces) {
            return getSubTreePaths(interfaceMap, reqPath, depth, interfaces);
        });

    iface->register_method(
        "GetAssociatedSubTree",
        [&interfaceMap,
         &associationMaps](const sdbusplus::object_path& associationPath,
                           const sdbusplus::object_path& reqPath, int32_t depth,
                           std::vector<std::string>& interfaces) {
            return getAssociatedSubTree(interfaceMap, associationMaps,
                                        associationPath, reqPath, depth,
                                        interfaces);
        });

    iface->register_method(
        "GetAssociatedSubTreePaths",
        [&interfaceMap,
         &associationMaps](const sdbusplus::object_path& associationPath,
                           const sdbusplus::object_path& reqPath, int32_t depth,
                           std::vector<std::string>& interfaces) {
            return getAssociatedSubTreePaths(interfaceMap, associationMaps,
                                             associationPath, reqPath, depth,
                                             interfaces);
        });

    iface->register_method(
        "GetAssociatedSubTreeById",
        [&interfaceMap,
         &associationMaps](const std::string& id, const std::string& objectPath,
                           std::vector<std::string>& subtreeInterfaces,
                           const std::string& association,
                           std::vector<std::string>& endpointInterfaces) {
            return getAssociatedSubTreeById(interfaceMap, associationMaps, id,
                                            objectPath, subtreeInterfaces,
                                            association, endpointInterfaces);
        });

    iface->register_method(
        "GetAssociatedSubTreePathsById",
        [&interfaceMap,
         &associationMaps](const std::string& id, const std::string& objectPath,
                           std::vector<std::string>& subtreeInterfaces,
                           const std::string& association,
                           std::vector<std::string>& endpointInterfaces) {
            return getAssociatedSubTreePathsById(
                interfaceMap, associationMaps, id, objectPath,
                subtreeInterfaces, association, endpointInterfaces);
        });

    iface->initialize();

    boost::asio::post(io, [&]() {
        doListNames(io, interfaceMap, systemBus.get(), nameOwners,
                    associationMaps, server);
    });

    systemBus->request_name("xyz.openbmc_project.ObjectMapper");

    io.run();
}
