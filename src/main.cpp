#include "src/argument.hpp"

#include <tinyxml2.h>

#include <atomic>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

constexpr const char* OBJECT_MAPPER_DBUS_NAME =
    "xyz.openbmc_project.ObjectMapper";
constexpr const char* ASSOCIATIONS_INTERFACE = "org.openbmc.Associations";
constexpr const char* XYZ_ASSOCIATION_INTERFACE =
    "xyz.openbmc_project.Association";

// interface_map_type is the underlying datastructure the mapper uses.
// The 3 levels of map are
// object paths
//   connection names
//      interface names
using interface_map_type = boost::container::flat_map<
    std::string, boost::container::flat_map<
                     std::string, boost::container::flat_set<std::string>>>;

using Association = std::tuple<std::string, std::string, std::string>;

//  Associations and some metadata are stored in associationInterfaces.
//  The fields are:
//   * ifacePos - holds the D-Bus interface object
//   * endpointPos - holds the endpoints array that shadows the property
//   * sourcePos - holds the owning source path of the association
static constexpr auto ifacePos = 0;
static constexpr auto endpointsPos = 1;
static constexpr auto sourcePos = 2;
using Endpoints = std::vector<std::string>;
boost::container::flat_map<
    std::string, std::tuple<std::shared_ptr<sdbusplus::asio::dbus_interface>,
                            Endpoints, std::string>>
    associationInterfaces;

static boost::container::flat_set<std::string> service_whitelist;
static boost::container::flat_set<std::string> service_blacklist;

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

bool get_well_known(
    boost::container::flat_map<std::string, std::string>& owners,
    const std::string& request, std::string& well_known)
{
    // If it's already a well known name, just return
    if (!boost::starts_with(request, ":"))
    {
        well_known = request;
        return true;
    }

    auto it = owners.find(request);
    if (it == owners.end())
    {
        return false;
    }
    well_known = it->second;
    return true;
}

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

void addAssociation(sdbusplus::asio::object_server& objectServer,
                    const std::vector<Association>& associations,
                    const std::string& path)
{
    boost::container::flat_map<std::string,
                               boost::container::flat_set<std::string>>
        objects;
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
        std::get<sourcePos>(iface) = path;

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
}

void removeAssociation(const std::string& sourcePath,
                       sdbusplus::asio::object_server& server)
{
    // The sourcePath passed in can be:
    // a) the source of the association object, in which case
    //    that whole object needs to be deleted, and/or
    // b) just an entry in the endpoints property under some
    //    other path, in which case it just needs to be removed
    //    from the property.
    for (auto& assoc : associationInterfaces)
    {
        if (sourcePath == std::get<sourcePos>(assoc.second))
        {
            server.remove_interface(std::get<ifacePos>(assoc.second));
            std::get<ifacePos>(assoc.second) = nullptr;
            std::get<endpointsPos>(assoc.second).clear();
            std::get<sourcePos>(assoc.second).clear();
        }
        else
        {
            auto& endpoints = std::get<endpointsPos>(assoc.second);
            auto toRemove =
                std::find(endpoints.begin(), endpoints.end(), sourcePath);
            if (toRemove != endpoints.end())
            {
                endpoints.erase(toRemove);

                if (endpoints.empty())
                {
                    // Remove the interface object too if no longer needed
                    server.remove_interface(std::get<ifacePos>(assoc.second));
                    std::get<ifacePos>(assoc.second) = nullptr;
                    std::get<sourcePos>(assoc.second).clear();
                }
                else
                {
                    std::get<ifacePos>(assoc.second)
                        ->set_property("endpoints", endpoints);
                }
            }
        }
    }
}

void do_associations(sdbusplus::asio::connection* system_bus,
                     sdbusplus::asio::object_server& objectServer,
                     const std::string& processName, const std::string& path)
{
    system_bus->async_method_call(
        [&objectServer,
         path](const boost::system::error_code ec,
               const sdbusplus::message::variant<std::vector<Association>>&
                   variantAssociations) {
            if (ec)
            {
                std::cerr << "Error getting associations from " << path << "\n";
            }
            std::vector<Association> associations =
                sdbusplus::message::variant_ns::get<std::vector<Association>>(
                    variantAssociations);
            addAssociation(objectServer, associations, path);
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

bool need_to_introspect(const std::string& process_name)
{
    auto inWhitelist =
        std::find_if(service_whitelist.begin(), service_whitelist.end(),
                     [&process_name](const auto& prefix) {
                         return boost::starts_with(process_name, prefix);
                     }) != service_whitelist.end();

    // This holds full service names, not prefixes
    auto inBlacklist =
        service_blacklist.find(process_name) != service_blacklist.end();

    return inWhitelist && !inBlacklist;
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
    if (need_to_introspect(process_name))
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
                if (need_to_introspect(process_name))
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
            std::string name;
            std::string old_owner;
            std::string new_owner;

            message.read(name, old_owner, new_owner);

            if (!old_owner.empty())
            {
                if (boost::starts_with(old_owner, ":"))
                {
                    auto it = name_owners.find(old_owner);
                    if (it != name_owners.end())
                    {
                        name_owners.erase(it);
                    }
                }
                // Connection removed
                interface_map_type::iterator path_it = interface_map.begin();
                while (path_it != interface_map.end())
                {
                    // If an associations interface is being removed,
                    // also need to remove the corresponding associations
                    // objects and properties.
                    auto ifaces = path_it->second.find(name);
                    if (ifaces != path_it->second.end())
                    {
                        auto assoc = std::find(ifaces->second.begin(),
                                               ifaces->second.end(),
                                               ASSOCIATIONS_INTERFACE);

                        if (assoc != ifaces->second.end())
                        {
                            removeAssociation(path_it->first, server);
                        }
                    }

                    path_it->second.erase(name);
                    if (path_it->second.empty())
                    {
                        // If the last connection to the object is gone,
                        // delete the top level object
                        path_it = interface_map.erase(path_it);
                        continue;
                    }
                    path_it++;
                }
            }

            if (!new_owner.empty())
            {
#ifdef DEBUG
                auto transaction = std::make_shared<
                    std::chrono::time_point<std::chrono::steady_clock>>(
                    std::chrono::steady_clock::now());
#endif
                // New daemon added
                if (need_to_introspect(name))
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
            if (!get_well_known(name_owners, message.get_sender(), well_known))
            {
                return; // only introspect well-known
            }
            if (need_to_introspect(well_known))
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
                        addAssociation(server, associations, obj_path.str);
                    }
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
            if (!get_well_known(name_owners, message.get_sender(), sender))
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
                    removeAssociation(obj_path.str, server);
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
        };

    sdbusplus::bus::match::match interfacesRemoved(
        static_cast<sdbusplus::bus::bus&>(*system_bus),
        sdbusplus::bus::match::rules::interfacesRemoved(),
        interfacesRemovedHandler);

    std::function<void(sdbusplus::message::message & message)>
        associationChangedHandler =
            [&server](sdbusplus::message::message& message) {
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
                    addAssociation(server, associations, message.get_path());
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

            if (req_path.size() > 1)
            {
                if (req_path.back() == '/')
                {
                    req_path.pop_back();
                }

                if (interface_map.find(req_path) == interface_map.end())
                {
                    throw NotFoundException();
                }
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
                                addObjectMapResult(ret, this_path, interface_map);
                                break;
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

            if (req_path.size() > 1)
            {
                if (req_path.back() == '/')
                {
                    req_path.pop_back();
                }

                if (interface_map.find(req_path) == interface_map.end())
                {
                    throw NotFoundException();
                }
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
                                addObjectMapResult(ret, this_path, interface_map);
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

            if (req_path.size() > 1)
            {
                if (req_path.back() == '/')
                {
                    req_path.pop_back();
                }

                if (interface_map.find(req_path) == interface_map.end())
                {
                    throw NotFoundException();
                }
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
