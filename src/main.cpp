#include "src/argument.hpp"

#include <tinyxml2.h>

#include <atomic>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/container/flat_map.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <src/associations.hpp>
#include <src/processing.hpp>

constexpr const char* OBJECT_MAPPER_DBUS_NAME =
    "xyz.openbmc_project.ObjectMapper";
constexpr const char* XYZ_ASSOCIATION_INTERFACE =
    "xyz.openbmc_project.Association";

using Association = std::tuple<std::string, std::string, std::string>;

AssociationInterfaces associationInterfaces;
AssociationOwnersType associationOwners;

static WhiteBlackList service_whitelist;
static WhiteBlackList service_blacklist;

/** Exception thrown when a path is not found in the object list. */
struct NotFoundException final : public sdbusplus::exception_t
{
    const char* name() const noexcept override
    {
        return "org.freedesktop.DBus.Error.FileNotFound";
    };
    const char* description() const noexcept override
    {
        return "path or object not found";
    };
    const char* what() const noexcept override
    {
        return "org.freedesktop.DBus.Error.FileNotFound: "
               "The requested object was not found";
    };
};

void update_owners(sdbusplus::asio::connection* conn,
                   boost::container::flat_map<std::string, std::string>& owners,
                   const std::string& new_object)
{
    if (boost::starts_with(new_object, ":"))
    {
        return;
    }
    conn->async_method_call(
        [&, new_object](const boost::system::error_code ec,
                        const std::string& nameOwner) {
            if (ec)
            {
                std::cerr << "Error getting owner of " << new_object << " : "
                          << ec << "\n";
                return;
            }
            owners[nameOwner] = new_object;
        },
        "org.freedesktop.DBus", "/", "org.freedesktop.DBus", "GetNameOwner",
        new_object);
}

void send_introspection_complete_signal(sdbusplus::asio::connection* system_bus,
                                        const std::string& process_name)
{
    // TODO(ed) This signal doesn't get exposed properly in the
    // introspect right now.  Find out how to register signals in
    // sdbusplus
    sdbusplus::message::message m = system_bus->new_signal(
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper.Private", "IntrospectionComplete");
    m.append(process_name);
    m.signal_send();
}

struct InProgressIntrospect
{
    InProgressIntrospect(
        sdbusplus::asio::connection* system_bus, boost::asio::io_service& io,
        const std::string& process_name
#ifdef DEBUG
        ,
        std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
            global_start_time
#endif
        ) :
        system_bus(system_bus),
        io(io), process_name(process_name)
#ifdef DEBUG
        ,
        global_start_time(global_start_time),
        process_start_time(std::chrono::steady_clock::now())
#endif
    {
    }
    ~InProgressIntrospect()
    {
        send_introspection_complete_signal(system_bus, process_name);

#ifdef DEBUG
        std::chrono::duration<float> diff =
            std::chrono::steady_clock::now() - process_start_time;
        std::cout << std::setw(50) << process_name << " scan took "
                  << diff.count() << " seconds\n";

        // If we're the last outstanding caller globally, calculate the
        // time it took
        if (global_start_time != nullptr && global_start_time.use_count() == 1)
        {
            diff = std::chrono::steady_clock::now() - *global_start_time;
            std::cout << "Total scan took " << diff.count()
                      << " seconds to complete\n";
        }
#endif
    }
    sdbusplus::asio::connection* system_bus;
    boost::asio::io_service& io;
    std::string process_name;
#ifdef DEBUG
    std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
        global_start_time;
    std::chrono::time_point<std::chrono::steady_clock> process_start_time;
#endif
};

// Based on the latest values of the org.openbmc.Associations.associations
// property, passed in via the newAssociations param, check if any of the
// paths in the xyz.openbmc_project.Association.endpoints D-Bus property
// for that association need to be removed.  If the last path is removed
// from the endpoints property, remove that whole association object from
// D-Bus.
void checkAssociationEndpointRemoves(
    const std::string& sourcePath, const std::string& owner,
    const AssociationPaths& newAssociations,
    sdbusplus::asio::object_server& objectServer)
{
    // Find the services that have associations on this path.
    auto originalOwners = associationOwners.find(sourcePath);
    if (originalOwners == associationOwners.end())
    {
        return;
    }

    // Find the associations for this service
    auto originalAssociations = originalOwners->second.find(owner);
    if (originalAssociations == originalOwners->second.end())
    {
        return;
    }

    // Compare the new endpoints versus the original endpoints, and
    // remove any of the original ones that aren't in the new list.
    for (const auto& [originalAssocPath, originalEndpoints] :
         originalAssociations->second)
    {
        // Check if this source even still has each association that
        // was there previously, and if not, remove all of its endpoints
        // from the D-Bus endpoints property which will cause the whole
        // association path to be removed if no endpoints remain.
        auto newEndpoints = newAssociations.find(originalAssocPath);
        if (newEndpoints == newAssociations.end())
        {
            removeAssociationEndpoints(objectServer, originalAssocPath,
                                       originalEndpoints,
                                       associationInterfaces);
        }
        else
        {
            // The association is still there.  Check if the endpoints
            // changed.
            boost::container::flat_set<std::string> toRemove;

            for (auto& originalEndpoint : originalEndpoints)
            {
                if (std::find(newEndpoints->second.begin(),
                              newEndpoints->second.end(),
                              originalEndpoint) == newEndpoints->second.end())
                {
                    toRemove.emplace(originalEndpoint);
                }
            }
            if (!toRemove.empty())
            {
                removeAssociationEndpoints(objectServer, originalAssocPath,
                                           toRemove, associationInterfaces);
            }
        }
    }
}

// Called when either a new org.openbmc.Associations interface was
// created, or the associations property on that interface changed.
void associationChanged(sdbusplus::asio::object_server& objectServer,
                        const std::vector<Association>& associations,
                        const std::string& path, const std::string& owner)
{
    AssociationPaths objects;

    for (const Association& association : associations)
    {
        std::string forward;
        std::string reverse;
        std::string endpoint;
        std::tie(forward, reverse, endpoint) = association;

        if (forward.size())
        {
            objects[path + "/" + forward].emplace(endpoint);
        }
        if (reverse.size())
        {
            if (endpoint.empty())
            {
                std::cerr << "Found invalid association on path " << path
                          << "\n";
                continue;
            }
            objects[endpoint + "/" + reverse].emplace(path);
        }
    }
    for (const auto& object : objects)
    {
        // the mapper exposes the new association interface but intakes
        // the old

        auto& iface = associationInterfaces[object.first];
        auto& i = std::get<ifacePos>(iface);
        auto& endpoints = std::get<endpointsPos>(iface);

        // Only add new endpoints
        for (auto& e : object.second)
        {
            if (std::find(endpoints.begin(), endpoints.end(), e) ==
                endpoints.end())
            {
                endpoints.push_back(e);
            }
        }

        // If the interface already exists, only need to update
        // the property value, otherwise create it
        if (i)
        {
            i->set_property("endpoints", endpoints);
        }
        else
        {
            i = objectServer.add_interface(object.first,
                                           XYZ_ASSOCIATION_INTERFACE);
            i->register_property("endpoints", endpoints);
            i->initialize();
        }
    }

    // Check for endpoints being removed instead of added
    checkAssociationEndpointRemoves(path, owner, objects, objectServer);

    // Update associationOwners with the latest info
    auto a = associationOwners.find(path);
    if (a != associationOwners.end())
    {
        auto o = a->second.find(owner);
        if (o != a->second.end())
        {
            o->second = std::move(objects);
        }
        else
        {
            a->second.emplace(owner, std::move(objects));
        }
    }
    else
    {
        boost::container::flat_map<std::string, AssociationPaths> owners;
        owners.emplace(owner, std::move(objects));
        associationOwners.emplace(path, owners);
    }
}

void do_associations(sdbusplus::asio::connection* system_bus,
                     sdbusplus::asio::object_server& objectServer,
                     const std::string& processName, const std::string& path)
{
    system_bus->async_method_call(
        [&objectServer, path, processName](
            const boost::system::error_code ec,
            const sdbusplus::message::variant<std::vector<Association>>&
                variantAssociations) {
            if (ec)
            {
                std::cerr << "Error getting associations from " << path << "\n";
            }
            std::vector<Association> associations =
                sdbusplus::message::variant_ns::get<std::vector<Association>>(
                    variantAssociations);
            associationChanged(objectServer, associations, path, processName);
        },
        processName, path, "org.freedesktop.DBus.Properties", "Get",
        ASSOCIATIONS_INTERFACE, "associations");
}

void do_introspect(sdbusplus::asio::connection* system_bus,
                   std::shared_ptr<InProgressIntrospect> transaction,
                   interface_map_type& interface_map,
                   sdbusplus::asio::object_server& objectServer,
                   std::string path)
{
    system_bus->async_method_call(
        [&interface_map, &objectServer, transaction, path,
         system_bus](const boost::system::error_code ec,
                     const std::string& introspect_xml) {
            if (ec)
            {
                std::cerr << "Introspect call failed with error: " << ec << ", "
                          << ec.message()
                          << " on process: " << transaction->process_name
                          << " path: " << path << "\n";
                return;
            }

            tinyxml2::XMLDocument doc;

            tinyxml2::XMLError e = doc.Parse(introspect_xml.c_str());
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
            auto& thisPathMap = interface_map[path];
            tinyxml2::XMLElement* pElement =
                pRoot->FirstChildElement("interface");
            while (pElement != nullptr)
            {
                const char* iface_name = pElement->Attribute("name");
                if (iface_name == nullptr)
                {
                    continue;
                }

                std::string iface{iface_name};

                thisPathMap[transaction->process_name].emplace(iface_name);

                if (std::strcmp(iface_name, ASSOCIATIONS_INTERFACE) == 0)
                {
                    do_associations(system_bus, objectServer,
                                    transaction->process_name, path);
                }

                pElement = pElement->NextSiblingElement("interface");
            }

            pElement = pRoot->FirstChildElement("node");
            while (pElement != nullptr)
            {
                const char* child_path = pElement->Attribute("name");
                if (child_path != nullptr)
                {
                    std::string parent_path(path);
                    if (parent_path == "/")
                    {
                        parent_path.clear();
                    }

                    do_introspect(system_bus, transaction, interface_map,
                                  objectServer, parent_path + "/" + child_path);
                }
                pElement = pElement->NextSiblingElement("node");
            }
        },
        transaction->process_name, path, "org.freedesktop.DBus.Introspectable",
        "Introspect");
}

void start_new_introspect(
    sdbusplus::asio::connection* system_bus, boost::asio::io_service& io,
    interface_map_type& interface_map, const std::string& process_name,
#ifdef DEBUG
    std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
        global_start_time,
#endif
    sdbusplus::asio::object_server& objectServer)
{
    if (needToIntrospect(process_name, service_whitelist, service_blacklist))
    {
        std::shared_ptr<InProgressIntrospect> transaction =
            std::make_shared<InProgressIntrospect>(system_bus, io, process_name
#ifdef DEBUG
                                                   ,
                                                   global_start_time
#endif
            );

        do_introspect(system_bus, transaction, interface_map, objectServer,
                      "/");
    }
}

// TODO(ed) replace with std::set_intersection once c++17 is available
template <class InputIt1, class InputIt2>
bool intersect(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2)
{
    while (first1 != last1 && first2 != last2)
    {
        if (*first1 < *first2)
        {
            ++first1;
            continue;
        }
        if (*first2 < *first1)
        {
            ++first2;
            continue;
        }
        return true;
    }
    return false;
}

void doListNames(
    boost::asio::io_service& io, interface_map_type& interface_map,
    sdbusplus::asio::connection* system_bus,
    boost::container::flat_map<std::string, std::string>& name_owners,
    sdbusplus::asio::object_server& objectServer)
{
    system_bus->async_method_call(
        [&io, &interface_map, &name_owners, &objectServer,
         system_bus](const boost::system::error_code ec,
                     std::vector<std::string> process_names) {
            if (ec)
            {
                std::cerr << "Error getting names: " << ec << "\n";
                std::exit(EXIT_FAILURE);
                return;
            }
            // Try to make startup consistent
            std::sort(process_names.begin(), process_names.end());
#ifdef DEBUG
            std::shared_ptr<std::chrono::time_point<std::chrono::steady_clock>>
                global_start_time = std::make_shared<
                    std::chrono::time_point<std::chrono::steady_clock>>(
                    std::chrono::steady_clock::now());
#endif
            for (const std::string& process_name : process_names)
            {
                if (needToIntrospect(process_name, service_whitelist,
                                     service_blacklist))
                {
                    start_new_introspect(system_bus, io, interface_map,
                                         process_name,
#ifdef DEBUG
                                         global_start_time,
#endif
                                         objectServer);
                    update_owners(system_bus, name_owners, process_name);
                }
            }
        },
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "ListNames");
}

void splitArgs(const std::string& stringArgs,
               boost::container::flat_set<std::string>& listArgs)
{
    std::istringstream args;
    std::string arg;

    args.str(stringArgs);

    while (!args.eof())
    {
        args >> arg;
        if (!arg.empty())
        {
            listArgs.insert(arg);
        }
    }
}

void addObjectMapResult(
    std::vector<interface_map_type::value_type>& objectMap,
    const std::string& objectPath,
    const std::pair<std::string, boost::container::flat_set<std::string>>&
        interfaceMap)
{
    // Adds an object path/service name/interface list entry to
    // the results of GetSubTree and GetAncestors.
    // If an entry for the object path already exists, just add the
    // service name and interfaces to that entry, otherwise create
    // a new entry.
    auto entry = std::find_if(
        objectMap.begin(), objectMap.end(),
        [&objectPath](const auto& i) { return objectPath == i.first; });

    if (entry != objectMap.end())
    {
        entry->second.emplace(interfaceMap);
    }
    else
    {
        interface_map_type::value_type object;
        object.first = objectPath;
        object.second.emplace(interfaceMap);
        objectMap.push_back(object);
    }
}

// Remove parents of the passed in path that:
// 1) Only have the 3 default interfaces on them
//    - Means D-Bus created these, not application code,
//      with the Properties, Introspectable, and Peer ifaces
// 2) Have no other child for this owner
void removeUnneededParents(const std::string& objectPath,
                           const std::string& owner,
                           interface_map_type& interface_map)
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

        auto parent_it = interface_map.find(parent);
        if (parent_it == interface_map.end())
        {
            break;
        }

        auto ifaces_it = parent_it->second.find(owner);
        if (ifaces_it == parent_it->second.end())
        {
            break;
        }

        if (ifaces_it->second.size() != 3)
        {
            break;
        }

        auto child_path = parent + '/';

        // Remove this parent if there isn't a remaining child on this owner
        auto child = std::find_if(
            interface_map.begin(), interface_map.end(),
            [&owner, &child_path](const auto& entry) {
                return boost::starts_with(entry.first, child_path) &&
                       (entry.second.find(owner) != entry.second.end());
            });

        if (child == interface_map.end())
        {
            parent_it->second.erase(ifaces_it);
            if (parent_it->second.empty())
            {
                interface_map.erase(parent_it);
            }
        }
        else
        {
            break;
        }
    }
}

int main(int argc, char** argv)
{
    auto options = ArgumentParser(argc, argv);
    boost::asio::io_service io;
    std::shared_ptr<sdbusplus::asio::connection> system_bus =
        std::make_shared<sdbusplus::asio::connection>(io);

    splitArgs(options["service-namespaces"], service_whitelist);
    splitArgs(options["service-blacklists"], service_blacklist);

    // TODO(Ed) Remove this once all service files are updated to not use this.
    // For now, simply squash the input, and ignore it.
    boost::container::flat_set<std::string> iface_whitelist;
    splitArgs(options["interface-namespaces"], iface_whitelist);

    system_bus->request_name(OBJECT_MAPPER_DBUS_NAME);
    sdbusplus::asio::object_server server(system_bus);

    // Construct a signal set registered for process termination.
    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&io](const boost::system::error_code& error,
                             int signal_number) { io.stop(); });

    interface_map_type interface_map;
    boost::container::flat_map<std::string, std::string> name_owners;

    std::function<void(sdbusplus::message::message & message)>
        nameChangeHandler = [&interface_map, &io, &name_owners, &server,
                             system_bus](sdbusplus::message::message& message) {
            std::string name;      // well-known
            std::string old_owner; // unique-name
            std::string new_owner; // unique-name

            message.read(name, old_owner, new_owner);

            if (!old_owner.empty())
            {
                processNameChangeDelete(name_owners, name, old_owner,
                                        interface_map, associationOwners,
                                        associationInterfaces, server);
            }

            if (!new_owner.empty())
            {
#ifdef DEBUG
                auto transaction = std::make_shared<
                    std::chrono::time_point<std::chrono::steady_clock>>(
                    std::chrono::steady_clock::now());
#endif
                // New daemon added
                if (needToIntrospect(name, service_whitelist,
                                     service_blacklist))
                {
                    name_owners[new_owner] = name;
                    start_new_introspect(system_bus.get(), io, interface_map,
                                         name,
#ifdef DEBUG
                                         transaction,
#endif
                                         server);
                }
            }
        };

    sdbusplus::bus::match::match nameOwnerChanged(
        static_cast<sdbusplus::bus::bus&>(*system_bus),
        sdbusplus::bus::match::rules::nameOwnerChanged(), nameChangeHandler);

    std::function<void(sdbusplus::message::message & message)>
        interfacesAddedHandler = [&interface_map, &name_owners, &server](
                                     sdbusplus::message::message& message) {
            sdbusplus::message::object_path obj_path;
            std::vector<std::pair<
                std::string, std::vector<std::pair<
                                 std::string, sdbusplus::message::variant<
                                                  std::vector<Association>>>>>>
                interfaces_added;
            message.read(obj_path, interfaces_added);
            std::string well_known;
            if (!getWellKnown(name_owners, message.get_sender(), well_known))
            {
                return; // only introspect well-known
            }
            if (needToIntrospect(well_known, service_whitelist,
                                 service_blacklist))
            {
                auto& iface_list = interface_map[obj_path.str];

                for (const auto& interface_pair : interfaces_added)
                {
                    iface_list[well_known].emplace(interface_pair.first);

                    if (interface_pair.first == ASSOCIATIONS_INTERFACE)
                    {
                        const sdbusplus::message::variant<
                            std::vector<Association>>* variantAssociations =
                            nullptr;
                        for (const auto& interface : interface_pair.second)
                        {
                            if (interface.first == "associations")
                            {
                                variantAssociations = &(interface.second);
                            }
                        }
                        if (variantAssociations == nullptr)
                        {
                            std::cerr << "Illegal association found on "
                                      << well_known << "\n";
                            continue;
                        }
                        std::vector<Association> associations =
                            sdbusplus::message::variant_ns::get<
                                std::vector<Association>>(*variantAssociations);
                        associationChanged(server, associations, obj_path.str,
                                           well_known);
                    }
                }

                // To handle the case where an object path is being created
                // with 2 or more new path segments, check if the parent paths
                // of this path are already in the interface map, and add them
                // if they aren't with just the default freedesktop interfaces.
                // This would be done via introspection if they would have
                // already existed at startup.  While we could also introspect
                // them now to do the work, we know there aren't any other
                // interfaces or we would have gotten signals for them as well,
                // so take a shortcut to speed things up.
                //
                // This is all needed so that mapper operations can be done
                // on the new parent paths.
                using iface_map_iterator = interface_map_type::iterator;
                using iface_map_value_type = boost::container::flat_map<
                    std::string, boost::container::flat_set<std::string>>;
                using name_map_iterator = iface_map_value_type::iterator;

                static const boost::container::flat_set<std::string>
                    default_ifaces{"org.freedesktop.DBus.Introspectable",
                                   "org.freedesktop.DBus.Peer",
                                   "org.freedesktop.DBus.Properties"};

                std::string parent = obj_path.str;
                auto pos = parent.find_last_of('/');

                while (pos != std::string::npos)
                {
                    parent = parent.substr(0, pos);

                    std::pair<iface_map_iterator, bool> parentEntry =
                        interface_map.insert(
                            std::make_pair(parent, iface_map_value_type{}));

                    std::pair<name_map_iterator, bool> ifaceEntry =
                        parentEntry.first->second.insert(
                            std::make_pair(well_known, default_ifaces));

                    if (!ifaceEntry.second)
                    {
                        // Entry was already there for this name so done.
                        break;
                    }

                    pos = parent.find_last_of('/');
                }
            }
        };

    sdbusplus::bus::match::match interfacesAdded(
        static_cast<sdbusplus::bus::bus&>(*system_bus),
        sdbusplus::bus::match::rules::interfacesAdded(),
        interfacesAddedHandler);

    std::function<void(sdbusplus::message::message & message)>
        interfacesRemovedHandler = [&interface_map, &name_owners, &server](
                                       sdbusplus::message::message& message) {
            sdbusplus::message::object_path obj_path;
            std::vector<std::string> interfaces_removed;
            message.read(obj_path, interfaces_removed);
            auto connection_map = interface_map.find(obj_path.str);
            if (connection_map == interface_map.end())
            {
                return;
            }

            std::string sender;
            if (!getWellKnown(name_owners, message.get_sender(), sender))
            {
                return;
            }
            for (const std::string& interface : interfaces_removed)
            {
                auto interface_set = connection_map->second.find(sender);
                if (interface_set == connection_map->second.end())
                {
                    continue;
                }

                if (interface == ASSOCIATIONS_INTERFACE)
                {
                    removeAssociation(obj_path.str, sender, server,
                                      associationOwners, associationInterfaces);
                }

                interface_set->second.erase(interface);
                // If this was the last interface on this connection,
                // erase the connection
                if (interface_set->second.empty())
                {
                    connection_map->second.erase(interface_set);
                }
            }
            // If this was the last connection on this object path,
            // erase the object path
            if (connection_map->second.empty())
            {
                interface_map.erase(connection_map);
            }

            removeUnneededParents(obj_path.str, sender, interface_map);
        };

    sdbusplus::bus::match::match interfacesRemoved(
        static_cast<sdbusplus::bus::bus&>(*system_bus),
        sdbusplus::bus::match::rules::interfacesRemoved(),
        interfacesRemovedHandler);

    std::function<void(sdbusplus::message::message & message)>
        associationChangedHandler =
            [&server, &name_owners](sdbusplus::message::message& message) {
                std::string objectName;
                boost::container::flat_map<
                    std::string,
                    sdbusplus::message::variant<std::vector<Association>>>
                    values;
                message.read(objectName, values);
                auto findAssociations = values.find("associations");
                if (findAssociations != values.end())
                {
                    std::vector<Association> associations =
                        sdbusplus::message::variant_ns::get<
                            std::vector<Association>>(findAssociations->second);

                    std::string well_known;
                    if (!getWellKnown(name_owners, message.get_sender(),
                                      well_known))
                    {
                        return;
                    }
                    associationChanged(server, associations, message.get_path(),
                                       well_known);
                }
            };
    sdbusplus::bus::match::match associationChanged(
        static_cast<sdbusplus::bus::bus&>(*system_bus),
        sdbusplus::bus::match::rules::interface(
            "org.freedesktop.DBus.Properties") +
            sdbusplus::bus::match::rules::member("PropertiesChanged") +
            sdbusplus::bus::match::rules::argN(0, ASSOCIATIONS_INTERFACE),
        associationChangedHandler);

    std::shared_ptr<sdbusplus::asio::dbus_interface> iface =
        server.add_interface("/xyz/openbmc_project/object_mapper",
                             "xyz.openbmc_project.ObjectMapper");

    iface->register_method(
        "GetAncestors", [&interface_map](std::string& req_path,
                                         std::vector<std::string>& interfaces) {
            // Interfaces need to be sorted for intersect to function
            std::sort(interfaces.begin(), interfaces.end());

            if (boost::ends_with(req_path, "/"))
            {
                req_path.pop_back();
            }
            if (req_path.size() &&
                interface_map.find(req_path) == interface_map.end())
            {
                throw NotFoundException();
            }

            std::vector<interface_map_type::value_type> ret;
            for (auto& object_path : interface_map)
            {
                auto& this_path = object_path.first;
                if (boost::starts_with(req_path, this_path) &&
                    (req_path != this_path))
                {
                    if (interfaces.empty())
                    {
                        ret.emplace_back(object_path);
                    }
                    else
                    {
                        for (auto& interface_map : object_path.second)
                        {

                            if (intersect(interfaces.begin(), interfaces.end(),
                                          interface_map.second.begin(),
                                          interface_map.second.end()))
                            {
                                addObjectMapResult(ret, this_path,
                                                   interface_map);
                            }
                        }
                    }
                }
            }

            return ret;
        });

    iface->register_method(
        "GetObject", [&interface_map](const std::string& path,
                                      std::vector<std::string>& interfaces) {
            boost::container::flat_map<std::string,
                                       boost::container::flat_set<std::string>>
                results;

            // Interfaces need to be sorted for intersect to function
            std::sort(interfaces.begin(), interfaces.end());
            auto path_ref = interface_map.find(path);
            if (path_ref == interface_map.end())
            {
                throw NotFoundException();
            }
            if (interfaces.empty())
            {
                return path_ref->second;
            }
            for (auto& interface_map : path_ref->second)
            {
                if (intersect(interfaces.begin(), interfaces.end(),
                              interface_map.second.begin(),
                              interface_map.second.end()))
                {
                    results.emplace(interface_map.first, interface_map.second);
                }
            }

            if (results.empty())
            {
                throw NotFoundException();
            }

            return results;
        });

    iface->register_method(
        "GetSubTree", [&interface_map](std::string& req_path, int32_t depth,
                                       std::vector<std::string>& interfaces) {
            if (depth <= 0)
            {
                depth = std::numeric_limits<int32_t>::max();
            }
            // Interfaces need to be sorted for intersect to function
            std::sort(interfaces.begin(), interfaces.end());
            std::vector<interface_map_type::value_type> ret;

            if (boost::ends_with(req_path, "/"))
            {
                req_path.pop_back();
            }
            if (req_path.size() &&
                interface_map.find(req_path) == interface_map.end())
            {
                throw NotFoundException();
            }

            for (auto& object_path : interface_map)
            {
                auto& this_path = object_path.first;

                if (this_path == req_path)
                {
                    continue;
                }

                if (boost::starts_with(this_path, req_path))
                {
                    // count the number of slashes past the search term
                    int32_t this_depth =
                        std::count(this_path.begin() + req_path.size(),
                                   this_path.end(), '/');
                    if (this_depth <= depth)
                    {
                        for (auto& interface_map : object_path.second)
                        {
                            if (intersect(interfaces.begin(), interfaces.end(),
                                          interface_map.second.begin(),
                                          interface_map.second.end()) ||
                                interfaces.empty())
                            {
                                addObjectMapResult(ret, this_path,
                                                   interface_map);
                            }
                        }
                    }
                }
            }

            return ret;
        });

    iface->register_method(
        "GetSubTreePaths",
        [&interface_map](std::string& req_path, int32_t depth,
                         std::vector<std::string>& interfaces) {
            if (depth <= 0)
            {
                depth = std::numeric_limits<int32_t>::max();
            }
            // Interfaces need to be sorted for intersect to function
            std::sort(interfaces.begin(), interfaces.end());
            std::vector<std::string> ret;

            if (boost::ends_with(req_path, "/"))
            {
                req_path.pop_back();
            }
            if (req_path.size() &&
                interface_map.find(req_path) == interface_map.end())
            {
                throw NotFoundException();
            }

            for (auto& object_path : interface_map)
            {
                auto& this_path = object_path.first;

                if (this_path == req_path)
                {
                    continue;
                }

                if (boost::starts_with(this_path, req_path))
                {
                    // count the number of slashes past the search term
                    int this_depth =
                        std::count(this_path.begin() + req_path.size(),
                                   this_path.end(), '/');
                    if (this_depth <= depth)
                    {
                        bool add = interfaces.empty();
                        for (auto& interface_map : object_path.second)
                        {
                            if (intersect(interfaces.begin(), interfaces.end(),
                                          interface_map.second.begin(),
                                          interface_map.second.end()))
                            {
                                add = true;
                                break;
                            }
                        }
                        if (add)
                        {
                            // TODO(ed) this is a copy
                            ret.emplace_back(this_path);
                        }
                    }
                }
            }

            return ret;
        });

    iface->initialize();

    io.post([&]() {
        doListNames(io, interface_map, system_bus.get(), name_owners, server);
    });

    io.run();
}
