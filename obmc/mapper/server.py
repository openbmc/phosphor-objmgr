# Contributors Listed Below - COPYRIGHT 2016
# [+] International Business Machines Corp.
#
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied. See the License for the specific language governing
# permissions and limitations under the License.

import dbus
import dbus.service
import dbus.exceptions
import dbus.mainloop.glib
import gobject
import xml.etree.ElementTree as ET
import obmc.utils.pathtree
import obmc.utils.misc
import obmc.mapper
import obmc.dbuslib.bindings
import obmc.dbuslib.enums


class MapperNotFoundException(dbus.exceptions.DBusException):
    _dbus_error_name = obmc.mapper.MAPPER_NOT_FOUND

    def __init__(self, path):
        super(MapperNotFoundException, self).__init__(
            "path or object not found: %s" % path)


def find_dbus_interfaces(conn, service, path, **kw):
    iface_match = kw.pop('iface_match', bool)
    subtree_match = kw.pop('subtree_match', bool)

    class _FindInterfaces(object):
        def __init__(self):
            self.results = {}

        @staticmethod
        def _get_object(path):
            try:
                return conn.get_object(service, path, introspect=False)
            except dbus.exceptions.DBusException, e:
                if e.get_dbus_name() in [
                        obmc.dbuslib.enums.DBUS_UNKNOWN_SERVICE,
                        obmc.dbuslib.enums.DBUS_NO_REPLY]:
                    print "Warning: Introspection failure: " \
                        "service `%s` is not running" % (service)
                    return None
                raise

        @staticmethod
        def _invoke_method(path, iface, method):
            obj = _FindInterfaces._get_object(path)
            if not obj:
                return None

            iface = dbus.Interface(obj, iface)
            try:
                return method(iface)
            except dbus.exceptions.DBusException, e:
                if e.get_dbus_name() in [
                        obmc.dbuslib.enums.DBUS_UNKNOWN_SERVICE,
                        obmc.dbuslib.enums.DBUS_NO_REPLY]:
                    print "Warning: Introspection failure: " \
                        "service `%s` did not reply to "\
                        "method call on %s" % (service, path)
                    return None
                raise

        @staticmethod
        def _introspect(path):
            return _FindInterfaces._invoke_method(
                path,
                dbus.INTROSPECTABLE_IFACE,
                lambda x: x.Introspect())

        @staticmethod
        def _get(path, iface, prop):
            return _FindInterfaces._invoke_method(
                path,
                dbus.PROPERTIES_IFACE,
                lambda x: x.Get(iface, prop))

        @staticmethod
        def _get_managed_objects(om):
            return _FindInterfaces._invoke_method(
                om,
                dbus.BUS_DAEMON_IFACE + '.ObjectManager',
                lambda x: x.GetManagedObjects())

        @staticmethod
        def _to_path(elements):
            return '/' + '/'.join(elements)

        @staticmethod
        def _to_path_elements(path):
            return filter(bool, path.split('/'))

        def __call__(self, path):
            self.results = {}
            self._find_interfaces(path)
            return self.results

        @staticmethod
        def _match(iface):
            return iface == dbus.BUS_DAEMON_IFACE + '.ObjectManager' \
                or iface_match(iface)

        def _find_interfaces(self, path):
            path_elements = self._to_path_elements(path)
            path = self._to_path(path_elements)
            data = self._introspect(path)
            if data is None:
                return

            root = ET.fromstring(data)
            ifaces = filter(
                self._match,
                [x.attrib.get('name') for x in root.findall('interface')])
            ifaces = {x: {} for x in ifaces}

            iface = obmc.dbuslib.enums.OBMC_ASSOCIATIONS_IFACE
            if iface in ifaces:
                associations = self._get(
                    path, iface, 'associations')
                if associations:
                    ifaces[iface]['associations'] = associations

            self.results[path] = ifaces

            if dbus.BUS_DAEMON_IFACE + '.ObjectManager' in ifaces:
                objs = self._get_managed_objects(path)
                for k, v in objs.iteritems():
                    self.results[k] = v
            else:
                children = filter(
                    bool,
                    [x.attrib.get('name') for x in root.findall('node')])
                children = [
                    self._to_path(
                        path_elements + self._to_path_elements(x))
                    for x in sorted(children)]
                for child in filter(subtree_match, children):
                    if child not in self.results:
                        self._find_interfaces(child)

    return _FindInterfaces()(path)


class Association(dbus.service.Object):
    def __init__(self, bus, path, endpoints):
        super(Association, self).__init__(conn=bus, object_path=path)
        self.endpoints = endpoints

    def __getattr__(self, name):
        if name == 'properties':
            return {
                obmc.dbuslib.enums.OBMC_ASSOC_IFACE: {
                    'endpoints': self.endpoints}}
        return super(Association, self).__getattr__(name)

    def emit_signal(self, old):
        if old != self.endpoints:
            self.PropertiesChanged(
                obmc.dbuslib.enums.OBMC_ASSOC_IFACE,
                {'endpoints': self.endpoints}, ['endpoints'])

    def append(self, endpoints):
        old = self.endpoints
        self.endpoints = list(set(endpoints).union(self.endpoints))
        self.emit_signal(old)

    def remove(self, endpoints):
        old = self.endpoints
        self.endpoints = list(set(self.endpoints).difference(endpoints))
        self.emit_signal(old)

    @dbus.service.method(dbus.PROPERTIES_IFACE, 'ss', 'as')
    def Get(self, interface_name, property_name):
        if property_name != 'endpoints':
            raise dbus.exceptions.DBusException(name=DBUS_UNKNOWN_PROPERTY)
        return self.GetAll(interface_name)[property_name]

    @dbus.service.method(dbus.PROPERTIES_IFACE, 's', 'a{sas}')
    def GetAll(self, interface_name):
        if interface_name != obmc.dbuslib.enums.OBMC_ASSOC_IFACE:
            raise dbus.exceptions.DBusException(DBUS_UNKNOWN_INTERFACE)
        return {'endpoints': self.endpoints}

    @dbus.service.signal(
        dbus.PROPERTIES_IFACE, signature='sa{sas}as')
    def PropertiesChanged(
            self, interface_name, changed_properties, invalidated_properties):
        pass


class Manager(obmc.dbuslib.bindings.DbusObjectManager):
    def __init__(self, bus, path):
        super(Manager, self).__init__(conn=bus, object_path=path)


class ObjectMapper(dbus.service.Object):
    def __init__(self, bus, path,
                 intf_match=obmc.utils.misc.org_dot_openbmc_match):
        super(ObjectMapper, self).__init__(bus, path)
        self.cache = obmc.utils.pathtree.PathTree()
        self.bus = bus
        self.intf_match = intf_match
        self.service = None
        self.index = {}
        self.manager = Manager(bus, obmc.dbuslib.bindings.OBJ_PREFIX)
        self.unique = bus.get_unique_name()
        self.bus_map = {}
        self.bus_map[self.unique] = obmc.mapper.MAPPER_NAME

        # add my object mananger instance
        self.add_new_objmgr(obmc.dbuslib.bindings.OBJ_PREFIX, self.unique)

        self.bus.add_signal_receiver(
            self.bus_handler,
            dbus_interface=dbus.BUS_DAEMON_IFACE,
            signal_name='NameOwnerChanged')
        self.bus.add_signal_receiver(
            self.interfaces_added_handler,
            dbus_interface=dbus.BUS_DAEMON_IFACE + '.ObjectManager',
            signal_name='InterfacesAdded',
            sender_keyword='sender',
            path_keyword='sender_path')
        self.bus.add_signal_receiver(
            self.interfaces_removed_handler,
            dbus_interface=dbus.BUS_DAEMON_IFACE + '.ObjectManager',
            signal_name='InterfacesRemoved',
            sender_keyword='sender',
            path_keyword='sender_path')
        self.bus.add_signal_receiver(
            self.properties_changed_handler,
            dbus_interface=dbus.PROPERTIES_IFACE,
            signal_name='PropertiesChanged',
            path_keyword='path',
            sender_keyword='sender')

        print "ObjectMapper startup complete.  Discovery in progress..."
        self.discover()

        print "ObjectMapper discovery complete"
        self.service = dbus.service.BusName(
            obmc.mapper.MAPPER_NAME, self.bus)

    def cache_get(self, path):
        cache_entry = self.cache.get(path, {})
        if cache_entry is None:
            # hide path elements without any interfaces
            cache_entry = {}
        return cache_entry

    def add_new_objmgr(self, path, owner):
        # We don't get a signal for the ObjectManager
        # interface itself, so if we see a signal from
        # make sure its in our cache, and add it if not.
        cache_entry = self.cache_get(path)
        old = self.interfaces_get(cache_entry, owner)
        new = list(set(old).union([dbus.BUS_DAEMON_IFACE + '.ObjectManager']))
        self.update_interfaces(path, owner, old, new)

    def interfaces_added_handler(self, path, iprops, **kw):
        path = str(path)
        owner = str(kw['sender'])
        interfaces = self.get_signal_interfaces(owner, iprops.iterkeys())
        if interfaces:
            self.add_new_objmgr(str(kw['sender_path']), owner)
            cache_entry = self.cache_get(path)
            old = self.interfaces_get(cache_entry, owner)
            new = list(set(interfaces).union(old))
            new = {x: iprops[x] for x in new}
            self.update_interfaces(path, owner, old, new)

    def interfaces_removed_handler(self, path, interfaces, **kw):
        path = str(path)
        owner = str(kw['sender'])
        interfaces = self.get_signal_interfaces(owner, interfaces)
        if interfaces:
            self.add_new_objmgr(str(kw['sender_path']), owner)
            cache_entry = self.cache_get(path)
            old = self.interfaces_get(cache_entry, owner)
            new = list(set(old).difference(interfaces))
            self.update_interfaces(path, owner, old, new)

    def properties_changed_handler(self, interface, new, old, **kw):
        owner = str(kw['sender'])
        path = str(kw['path'])
        interfaces = self.get_signal_interfaces(owner, [interface])
        if not self.is_association(interfaces):
            return
        associations = new.get('associations', None)
        if associations is None:
            return

        associations = [
            (str(x), str(y), str(z)) for x, y, z in associations]
        self.update_associations(
            path, owner,
            self.index_get_associations(path, [owner]),
            associations)

    def process_new_owner(self, owned_name, owner):
        # unique name
        try:
            return self.discover([(owned_name, owner)])
        except dbus.exceptions.DBusException, e:
            if obmc.dbuslib.enums.DBUS_UNKNOWN_SERVICE \
                    not in e.get_dbus_name():
                raise

    def process_old_owner(self, owned_name, owner):
        if owner in self.bus_map:
            del self.bus_map[owner]

        for path, item in self.cache.dataitems():
            old = self.interfaces_get(item, owner)
            # remove all interfaces for this service
            self.update_interfaces(
                path, owner, old=old, new=[])

    def bus_handler(self, owned_name, old, new):
        valid = False
        if not obmc.dbuslib.bindings.is_unique(owned_name):
            valid = self.valid_signal(owned_name)

        if valid and new:
            self.process_new_owner(owned_name, new)
        if valid and old:
            self.process_old_owner(owned_name, old)

    def update_interfaces(self, path, owner, old, new):
        # __xx -> intf list
        # xx -> intf dict
        if isinstance(old, dict):
            __old = old.keys()
        else:
            __old = old
            old = {x: {} for x in old}
        if isinstance(new, dict):
            __new = new.keys()
        else:
            __new = new
            new = {x: {} for x in new}

        cache_entry = self.cache.setdefault(path, {})
        created = [] if self.has_interfaces(cache_entry) else [path]
        added = list(set(__new).difference(__old))
        removed = list(set(__old).difference(__new))
        self.interfaces_append(cache_entry, owner, added)
        self.interfaces_remove(cache_entry, owner, removed, path)
        destroyed = [] if self.has_interfaces(cache_entry) else [path]

        # react to anything that requires association updates
        new_assoc = []
        old_assoc = []
        if self.is_association(added):
            iface = obmc.dbuslib.enums.OBMC_ASSOCIATIONS_IFACE
            new_assoc = new[iface]['associations']
        if self.is_association(removed):
            old_assoc = self.index_get_associations(path, [owner])
        self.update_associations(
            path, owner, old_assoc, new_assoc, created, destroyed)

    def add_items(self, owner, bus_items):
        for path, items in bus_items.iteritems():
            self.update_interfaces(path, str(owner), old=[], new=items)

    def discover(self, owners=[]):
        def match(iface):
            return iface == dbus.BUS_DAEMON_IFACE + '.ObjectManager' or \
                self.intf_match(iface)

        subtree_match = lambda x: obmc.utils.misc.org_dot_openbmc_match(
            x, sep='/', prefix='/')

        if not owners:
            owned_names = filter(
                lambda x: not obmc.dbuslib.bindings.is_unique(x),
                self.bus.list_names())
            owners = [self.bus.get_name_owner(x) for x in owned_names]
            owners = zip(owned_names, owners)
        for owned_name, o in owners:
            self.add_items(
                o,
                find_dbus_interfaces(
                    self.bus, o, '/',
                    subtree_match=subtree_match,
                    iface_match=self.intf_match))
            self.bus_map[o] = owned_name

    def valid_signal(self, name):
        if obmc.dbuslib.bindings.is_unique(name):
            name = self.bus_map.get(name)

        return name is not None and name is not obmc.mapper.MAPPER_NAME

    def get_signal_interfaces(self, owner, interfaces):
        filtered = []
        if self.valid_signal(owner):
            filtered = [str(x) for x in interfaces if self.intf_match(x)]

        return filtered

    @staticmethod
    def interfaces_get(item, owner, default=[]):
        return item.get(owner, default)

    @staticmethod
    def interfaces_append(item, owner, append):
        interfaces = item.setdefault(owner, [])
        item[owner] = list(set(append).union(interfaces))

    def interfaces_remove(self, item, owner, remove, path):
        interfaces = item.get(owner, [])
        item[owner] = list(set(interfaces).difference(remove))

        if not item[owner]:
            # remove the owner if there aren't any interfaces left
            del item[owner]

        if item:
            # other owners remain
            return

        if self.cache.get_children(path):
            # there are still references to this path
            # from objects further down the tree.
            # mark it for removal if that changes
            self.cache.demote(path)
        else:
            # delete the entire path if everything is gone
            del self.cache[path]

    @dbus.service.method(obmc.mapper.MAPPER_IFACE, 's', 'a{sas}')
    def GetObject(self, path):
        o = self.cache_get(path)
        if not o:
            raise MapperNotFoundException(path)
        return o

    @dbus.service.method(obmc.mapper.MAPPER_IFACE, 'si', 'as')
    def GetSubTreePaths(self, path, depth):
        try:
            return self.cache.iterkeys(path, depth)
        except KeyError:
            raise MapperNotFoundException(path)

    @dbus.service.method(obmc.mapper.MAPPER_IFACE, 'si', 'a{sa{sas}}')
    def GetSubTree(self, path, depth):
        try:
            return {x: y for x, y in self.cache.dataitems(path, depth)}
        except KeyError:
            raise MapperNotFoundException(path)

    @staticmethod
    def has_interfaces(item):
        for owner in item.iterkeys():
            if ObjectMapper.interfaces_get(item, owner):
                return True
        return False

    @staticmethod
    def is_association(interfaces):
        return obmc.dbuslib.enums.OBMC_ASSOCIATIONS_IFACE in interfaces

    def index_get(self, index, path, owners):
        items = []
        item = self.index.get(index, {})
        item = item.get(path, {})
        for o in owners:
            items.extend(item.get(o, []))
        return items

    def index_append(self, index, path, owner, assoc):
        item = self.index.setdefault(index, {})
        item = item.setdefault(path, {})
        item = item.setdefault(owner, [])
        item.append(assoc)

    def index_remove(self, index, path, owner, assoc):
        index = self.index.get(index, {})
        owners = index.get(path, {})
        items = owners.get(owner, [])
        if assoc in items:
            items.remove(assoc)
        if not items:
            del owners[owner]
        if not owners:
            del index[path]

    def index_get_associations(self, path, owners=[], direction='forward'):
        forward = 'forward' if direction == 'forward' else 'reverse'
        reverse = 'reverse' if direction == 'forward' else 'forward'

        associations = []
        if not owners:
            index = self.index.get(forward, {})
            owners = index.get(path, {}).keys()

        # f: forward
        # r: reverse
        for rassoc in self.index_get(forward, path, owners):
            elements = rassoc.split('/')
            rtype = ''.join(elements[-1:])
            fendpoint = '/'.join(elements[:-1])
            for fassoc in self.index_get(reverse, fendpoint, owners):
                elements = fassoc.split('/')
                ftype = ''.join(elements[-1:])
                rendpoint = '/'.join(elements[:-1])
                if rendpoint != path:
                    continue
                associations.append((ftype, rtype, fendpoint))

        return associations

    def update_association(self, path, removed, added):
        iface = obmc.dbuslib.enums.OBMC_ASSOC_IFACE
        create = [] if self.manager.get(path, False) else [iface]

        if added and create:
            self.manager.add(
                path, Association(self.bus, path, added))
        elif added:
            self.manager.get(path).append(added)

        obj = self.manager.get(path, None)
        if obj and removed:
            obj.remove(removed)

        if obj and not obj.endpoints:
            self.manager.remove(path)

        delete = [] if self.manager.get(path, False) else [iface]

        if create != delete:
            self.update_interfaces(
                path, self.unique, delete, create)

    def update_associations(
            self, path, owner, old, new, created=[], destroyed=[]):
        added = list(set(new).difference(old))
        removed = list(set(old).difference(new))
        for forward, reverse, endpoint in added:
            # update the index
            forward_path = str(path + '/' + forward)
            reverse_path = str(endpoint + '/' + reverse)
            self.index_append(
                'forward', path, owner, reverse_path)
            self.index_append(
                'reverse', endpoint, owner, forward_path)

            # create the association if the endpoint exists
            if not self.cache_get(endpoint):
                continue

            self.update_association(forward_path, [], [endpoint])
            self.update_association(reverse_path, [], [path])

        for forward, reverse, endpoint in removed:
            # update the index
            forward_path = str(path + '/' + forward)
            reverse_path = str(endpoint + '/' + reverse)
            self.index_remove(
                'forward', path, owner, reverse_path)
            self.index_remove(
                'reverse', endpoint, owner, forward_path)

            # destroy the association if it exists
            self.update_association(forward_path, [endpoint], [])
            self.update_association(reverse_path, [path], [])

        # If the associations interface endpoint comes
        # or goes create or destroy the appropriate
        # associations
        for path in created:
            for forward, reverse, endpoint in \
                    self.index_get_associations(path, direction='reverse'):
                forward_path = str(path + '/' + forward)
                reverse_path = str(endpoint + '/' + reverse)
                self.update_association(forward_path, [], [endpoint])
                self.update_association(reverse_path, [], [path])

        for path in destroyed:
            for forward, reverse, endpoint in \
                    self.index_get_associations(path, direction='reverse'):
                forward_path = str(path + '/' + forward)
                reverse_path = str(endpoint + '/' + reverse)
                self.update_association(forward_path, [endpoint], [])
                self.update_association(reverse_path, [path], [])

    @dbus.service.method(obmc.mapper.MAPPER_IFACE, 's', 'a{sa{sas}}')
    def GetAncestors(self, path):
        elements = filter(bool, path.split('/'))
        paths = []
        objs = {}
        while elements:
            elements.pop()
            paths.append('/' + '/'.join(elements))
        if path != '/':
            paths.append('/')

        for path in paths:
            obj = self.cache_get(path)
            if not obj:
                continue
            objs[path] = obj

        return objs


def server_main():
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    o = ObjectMapper(bus, obmc.mapper.MAPPER_PATH)
    loop = gobject.MainLoop()

    loop.run()
