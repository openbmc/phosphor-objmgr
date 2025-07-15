#include "associations.hpp"
#include "handler.hpp"
#include "processing.hpp"
#include "types.hpp"

#include <tinyxml2.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/container/flat_map.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

static AssociationMaps associationMaps;

void updateOwners(sdbusplus::asio::connection* conn,
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

void sendIntrospectionCompleteSignal(sdbusplus::asio::connection* systemBus,
                                     const std::string& processName)
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
        const std::string& introspectProcessName, AssociationMaps& am
#ifdef MAPPER_ENABLE_DEBUG
        ,
        std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
            globalIntrospectStartTime
#endif
        ) :
        systemBus(systemBusConnection), io(ioContext),
        processName(introspectProcessName), assocMaps(am)
#ifdef MAPPER_ENABLE_DEBUG
        ,
        globalStartTime(std::move(globalIntrospectStartTime)),
        processStartTime(std::chrono::steady_clock::now())
#endif
    {}
    ~InProgressIntrospect()
    {
        try
        {
            sendIntrospectionCompleteSignal(systemBus, processName);
#ifdef MAPPER_ENABLE_DEBUG
            std::chrono::duration<float> diff =
                std::chrono::steady_clock::now() - processStartTime;
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
#endif
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
#ifdef MAPPER_ENABLE_DEBUG
    std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
        globalStartTime;
    std::chrono::time_point<std::chrono::steady_clock> processStartTime;
#endif
};

void doAssociations(boost::asio::io_context& io,
                    sdbusplus::asio::connection* systemBus,
                    InterfaceMapType& interfaceMap,
                    sdbusplus::asio::object_server& objectServer,
                    const std::string& processName, const std::string& path,
                    int timeoutRetries = 0)
{
    constexpr int maxTimeoutRetries = 3;
    systemBus->async_method_call(
        [&io, &objectServer, path, processName, &interfaceMap, systemBus,
         timeoutRetries](
            const boost::system::error_code ec,
            const std::variant<std::vector<Association>>& variantAssociations) {
            if (ec)
            {
                if (ec.value() == boost::system::errc::timed_out &&
                    timeoutRetries < maxTimeoutRetries)
                {
                    doAssociations(io, systemBus, interfaceMap, objectServer,
                                   processName, path, timeoutRetries + 1);
                    return;
                }
                std::cerr << "Error getting associations from " << path << "\n";
            }
            std::vector<Association> associations =
                std::get<std::vector<Association>>(variantAssociations);
            associationChanged(io, objectServer, associations, path,
                               processName, interfaceMap, associationMaps);
        },
        processName, path, "org.freedesktop.DBus.Properties", "Get",
        assocDefsInterface, assocDefsProperty);
}

void doIntrospect(boost::asio::io_context& io,
                  sdbusplus::asio::connection* systemBus,
                  const std::shared_ptr<InProgressIntrospect>& transaction,
                  InterfaceMapType& interfaceMap,
                  sdbusplus::asio::object_server& objectServer,
                  const std::string& path, int timeoutRetries = 0)
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

            tinyxml2::XMLDocument doc;

            tinyxml2::XMLError e = doc.Parse(introspectXml.c_str());
            if (e != tinyxml2::XMLError::XML_SUCCESS)
            {
                std::cerr << "XML parsing failed\n";
                return;
            }

            tinyxml2::XMLNode* pRoot = doc.FirstChildElement("node");
            if (pRoot == nullptr)
            {
                std::cerr << "XML document did not contain any data\n";
                return;
            }
            auto& thisPathMap = interfaceMap[path];
            tinyxml2::XMLElement* pElement =
                pRoot->FirstChildElement("interface");
            while (pElement != nullptr)
            {
                const char* ifaceName = pElement->Attribute("name");
                if (ifaceName == nullptr)
                {
                    continue;
                }

                thisPathMap[transaction->processName].emplace(ifaceName);

                static constexpr auto assocDefsInterfaceView =
                    std::string_view(assocDefsInterface);
                if (ifaceName == assocDefsInterfaceView)
                {
                    doAssociations(io, systemBus, interfaceMap, objectServer,
                                   transaction->processName, path);
                }

                pElement = pElement->NextSiblingElement("interface");
            }

            // Check if this new path has a pending association that can
            // now be completed.
            checkIfPendingAssociation(io, path, interfaceMap,
                                      transaction->assocMaps, objectServer);

            pElement = pRoot->FirstChildElement("node");
            while (pElement != nullptr)
            {
                const char* childPath = pElement->Attribute("name");
                if (childPath != nullptr)
                {
                    std::string parentPath(path);
                    if (parentPath == "/")
                    {
                        parentPath.clear();
                    }

                    doIntrospect(io, systemBus, transaction, interfaceMap,
                                 objectServer, parentPath + "/" + childPath);
                }
                pElement = pElement->NextSiblingElement("node");
            }
        },
        transaction->processName, path, "org.freedesktop.DBus.Introspectable",
        "Introspect");
}

void startNewIntrospect(
    sdbusplus::asio::connection* systemBus, boost::asio::io_context& io,
    InterfaceMapType& interfaceMap, const std::string& processName,
    AssociationMaps& assocMaps,
#ifdef MAPPER_ENABLE_DEBUG
    std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
        globalStartTime,
#endif
    sdbusplus::asio::object_server& objectServer)
{
    if (needToIntrospect(processName))
    {
        std::shared_ptr<InProgressIntrospect> transaction =
            std::make_shared<InProgressIntrospect>(
                systemBus, io, processName, assocMaps
#ifdef MAPPER_ENABLE_DEBUG
                ,
                globalStartTime
#endif
            );

        doIntrospect(io, systemBus, transaction, interfaceMap, objectServer,
                     "/");
    }
}

void doListNames(
    bool& error, boost::asio::io_context& io, InterfaceMapType& interfaceMap,
    sdbusplus::asio::connection* systemBus,
    boost::container::flat_map<std::string, std::string>& nameOwners,
    AssociationMaps& assocMaps, sdbusplus::asio::object_server& objectServer)
{
    systemBus->async_method_call(
        [&error, &io, &interfaceMap, &nameOwners, &objectServer, systemBus,
         &assocMaps](const boost::system::error_code ec,
                     std::vector<std::string> processNames) {
            if (ec)
            {
                std::cerr << "Error getting names: " << ec << "\n";
                // we can't be useful without connection names
                error = true;
                io.stop();
                return;
            }
            // Try to make startup consistent
            std::sort(processNames.begin(), processNames.end());
#ifdef MAPPER_ENABLE_DEBUG
            std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
                globalStartTime = std::make_shared<
                    std::chrono::time_point<std::chrono::steady_clock>>(
                    std::chrono::steady_clock::now());
#endif
            for (const std::string& processName : processNames)
            {
                if (needToIntrospect(processName))
                {
                    startNewIntrospect(systemBus, io, interfaceMap, processName,
                                       assocMaps,
#ifdef MAPPER_ENABLE_DEBUG
                                       globalStartTime,
#endif
                                       objectServer);
                    updateOwners(systemBus, nameOwners, processName);
                }
            }
        },
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "ListNames");
}

// Remove parents of the passed in path that:
// 1) Only have the 3 default interfaces on them
//    - Means D-Bus created these, not application code,
//      with the Properties, Introspectable, and Peer ifaces
// 2) Have no other child for this owner
void removeUnneededParents(const std::string& objectPath,
                           const std::string& owner,
                           InterfaceMapType& interfaceMap)
{
    auto parent = objectPath;

    while (true)
    {
        auto pos = parent.find_last_of('/');
        if ((pos == std::string::npos) || (pos == 0))
        {
            break;
        }
        parent = parent.substr(0, pos);

        auto parentIt = interfaceMap.find(parent);
        if (parentIt == interfaceMap.end())
        {
            break;
        }

        auto ifacesIt = parentIt->second.find(owner);
        if (ifacesIt == parentIt->second.end())
        {
            break;
        }

        if (ifacesIt->second.size() != 3)
        {
            break;
        }

        auto childPath = parent + '/';

        // Remove this parent if there isn't a remaining child on this owner
        auto child = std::find_if(
            interfaceMap.begin(), interfaceMap.end(),
            [&owner, &childPath](const auto& entry) {
                return entry.first.starts_with(childPath) &&
                       (entry.second.find(owner) != entry.second.end());
            });

        if (child == interfaceMap.end())
        {
            parentIt->second.erase(ifacesIt);
            if (parentIt->second.empty())
            {
                interfaceMap.erase(parentIt);
            }
        }
        else
        {
            break;
        }
    }
}

int main()
{
    boost::asio::io_context io;
    bool errorExit = false;
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

    auto nameChangeHandler = [&interfaceMap, &io, &nameOwners, &server,
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
#ifdef MAPPER_ENABLE_DEBUG
            auto transaction = std::make_shared<
                std::chrono::time_point<std::chrono::steady_clock>>(
                std::chrono::steady_clock::now());
#endif
            // New daemon added
            if (needToIntrospect(name))
            {
                nameOwners[newOwner] = name;
                startNewIntrospect(systemBus.get(), io, interfaceMap, name,
                                   associationMaps,
#ifdef MAPPER_ENABLE_DEBUG
                                   transaction,
#endif
                                   server);
            }
        }
    };

    sdbusplus::bus::match_t nameOwnerChanged(
        static_cast<sdbusplus::bus_t&>(*systemBus),
        sdbusplus::bus::match::rules::nameOwnerChanged(),
        std::move(nameChangeHandler));

    auto interfacesAddedHandler = [&io, &interfaceMap, &nameOwners,
                                   &server](sdbusplus::message_t& message) {
        sdbusplus::message::object_path objPath;
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

    sdbusplus::bus::match_t interfacesAdded(
        static_cast<sdbusplus::bus_t&>(*systemBus),
        sdbusplus::bus::match::rules::interfacesAdded(),
        std::move(interfacesAddedHandler));

    auto interfacesRemovedHandler = [&io, &interfaceMap, &nameOwners,
                                     &server](sdbusplus::message_t& message) {
        sdbusplus::message::object_path objPath;
        std::vector<std::string> interfacesRemoved;
        message.read(objPath, interfacesRemoved);
        auto connectionMap = interfaceMap.find(objPath.str);
        if (connectionMap == interfaceMap.end())
        {
            return;
        }

        std::string sender;
        if (!getWellKnown(nameOwners, message.get_sender(), sender))
        {
            return;
        }
        for (const std::string& interface : interfacesRemoved)
        {
            auto interfaceSet = connectionMap->second.find(sender);
            if (interfaceSet == connectionMap->second.end())
            {
                continue;
            }

            if (interface == assocDefsInterface)
            {
                removeAssociation(io, objPath.str, sender, server,
                                  associationMaps);
            }

            interfaceSet->second.erase(interface);

            if (interfaceSet->second.empty())
            {
                // If this was the last interface on this connection,
                // erase the connection
                connectionMap->second.erase(interfaceSet);

                // Instead of checking if every single path is the endpoint
                // of an association that needs to be moved to pending,
                // only check when the only remaining owner of this path is
                // ourself, which would be because we still own the
                // association path.
                if ((connectionMap->second.size() == 1) &&
                    (connectionMap->second.begin()->first ==
                     "xyz.openbmc_project.ObjectMapper"))
                {
                    // Remove the 2 association D-Bus paths and move the
                    // association to pending.
                    moveAssociationToPending(io, objPath.str, associationMaps,
                                             server);
                }
            }
        }
        // If this was the last connection on this object path,
        // erase the object path
        if (connectionMap->second.empty())
        {
            interfaceMap.erase(connectionMap);
        }

        removeUnneededParents(objPath.str, sender, interfaceMap);
    };

    sdbusplus::bus::match_t interfacesRemoved(
        static_cast<sdbusplus::bus_t&>(*systemBus),
        sdbusplus::bus::match::rules::interfacesRemoved(),
        std::move(interfacesRemovedHandler));

    auto associationChangedHandler = [&io, &server, &nameOwners, &interfaceMap](
                                         sdbusplus::message_t& message) {
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
            associationChanged(io, server, associations, message.get_path(),
                               wellKnown, interfaceMap, associationMaps);
        }
    };
    sdbusplus::bus::match_t assocChangedMatch(
        static_cast<sdbusplus::bus_t&>(*systemBus),
        sdbusplus::bus::match::rules::interface(
            "org.freedesktop.DBus.Properties") +
            sdbusplus::bus::match::rules::member("PropertiesChanged") +
            sdbusplus::bus::match::rules::argN(0, assocDefsInterface),
        std::move(associationChangedHandler));

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
        [&interfaceMap](const sdbusplus::message::object_path& associationPath,
                        const sdbusplus::message::object_path& reqPath,
                        int32_t depth, std::vector<std::string>& interfaces) {
            return getAssociatedSubTree(interfaceMap, associationMaps,
                                        associationPath, reqPath, depth,
                                        interfaces);
        });

    iface->register_method(
        "GetAssociatedSubTreePaths",
        [&interfaceMap](const sdbusplus::message::object_path& associationPath,
                        const sdbusplus::message::object_path& reqPath,
                        int32_t depth, std::vector<std::string>& interfaces) {
            return getAssociatedSubTreePaths(interfaceMap, associationMaps,
                                             associationPath, reqPath, depth,
                                             interfaces);
        });

    iface->register_method(
        "GetAssociatedSubTreeById",
        [&interfaceMap](const std::string& id, const std::string& objectPath,
                        std::vector<std::string>& subtreeInterfaces,
                        const std::string& association,
                        std::vector<std::string>& endpointInterfaces) {
            return getAssociatedSubTreeById(interfaceMap, associationMaps, id,
                                            objectPath, subtreeInterfaces,
                                            association, endpointInterfaces);
        });

    iface->register_method(
        "GetAssociatedSubTreePathsById",
        [&interfaceMap](const std::string& id, const std::string& objectPath,
                        std::vector<std::string>& subtreeInterfaces,
                        const std::string& association,
                        std::vector<std::string>& endpointInterfaces) {
            return getAssociatedSubTreePathsById(
                interfaceMap, associationMaps, id, objectPath,
                subtreeInterfaces, association, endpointInterfaces);
        });

    iface->initialize();

    boost::asio::post(io, [&]() {
        doListNames(errorExit, io, interfaceMap, systemBus.get(), nameOwners,
                    associationMaps, server);
    });

    systemBus->request_name("xyz.openbmc_project.ObjectMapper");

    io.run();

    return errorExit ? EXIT_FAILURE : EXIT_SUCCESS;
}
