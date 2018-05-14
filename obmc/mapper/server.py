# Contributors Listed Below - COPYRIGHT 2017
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
# TODO: openbmc/openbmc#2994 remove python 2 support
try:  # python 2
    import gobject
except ImportError:  # python 3
    from gi.repository import GObject as gobject
import xml.etree.ElementTree as ET
import obmc.utils.pathtree
import obmc.mapper
import obmc.dbuslib.bindings
import obmc.dbuslib.enums
import sys
import traceback
from collections import defaultdict
from itertools import chain


class MapperBusyException(dbus.exceptions.DBusException):
    _dbus_error_name = 'org.freedesktop.DBus.Error.ObjectPathInUse'

    def __init__(self):
        super(MapperBusyException, self).__init__(
            'busy processing bus traffic')


class MapperNotFoundException(dbus.exceptions.DBusException):
    _dbus_error_name = obmc.mapper.MAPPER_NOT_FOUND

    def __init__(self, path):
        super(MapperNotFoundException, self).__init__(
            "path or object not found: %s" % path)


def find_dbus_interfaces(conn, service, path, callback, error_callback, **kw):
    iface_match = kw.pop('iface_match', bool)
    subtree_match = kw.pop('subtree_match', bool)

    class _FindInterfaces(object):
        def __init__(self):
            self.results = {}
            self.introspect_pending = []
            self.gmo_pending = []
            self.assoc_pending = []

        @staticmethod
        def _to_path(elements):
            return '/' + '/'.join(elements)

        @staticmethod
        def _to_path_elements(path):
            return filter(bool, path.split('/'))

        def __call__(self, path):
            try:
                self._find_interfaces(path)
            except Exception as e:
                error_callback(service, path, e)

        @staticmethod
        def _match(iface):
            return iface == dbus.BUS_DAEMON_IFACE + '.ObjectManager' \
                or iface_match(iface)

        def check_done(self):
            if any([
                    self.introspect_pending,
                    self.gmo_pending,
                    self.assoc_pending]):
                return

            callback(service, self.results)

        def _assoc_callback(self, path, associations):
            try:
                iface = obmc.dbuslib.enums.OBMC_ASSOCIATIONS_IFACE
                self.assoc_pending.remove(path)
                self.results[path][iface]['associations'] = associations
            except Exception as e:
                error_callback(service, path, e)
                return None

            self.check_done()

        def _gmo_callback(self, path, objs):
            try:
                self.gmo_pending.remove(path)
                for k, v in objs.items():
                    self.results[k] = dict(x for x in v.items()
                                           if iface_match(x[0]))
            except Exception as e:
                error_callback(service, path, e)
                return None

            self.check_done()

        def _introspect_callback(self, path, data):
            self.introspect_pending.remove(path)
            if data is None:
                self.check_done()
                return

            try:
                path_elements = self._to_path_elements(path)
                root = ET.fromstring(data)
                all_ifaces = root.findall('interface')
                ifaces = filter(self._match,
                                [x.attrib.get('name') for x in all_ifaces])
                ifaces = {x: {} for x in ifaces}
                self.results[path] = ifaces

                if obmc.dbuslib.enums.OBMC_ASSOCIATIONS_IFACE in ifaces:
                    obj = conn.get_object(service, path, introspect=False)
                    iface = dbus.Interface(obj, dbus.PROPERTIES_IFACE)
                    self.assoc_pending.append(path)
                    iface.Get.call_async(
                        obmc.dbuslib.enums.OBMC_ASSOCIATIONS_IFACE,
                        'associations',
                        reply_handler=lambda x: self._assoc_callback(
                            path, x),
                        error_handler=lambda e: error_callback(
                            service, path, e))

                if dbus.BUS_DAEMON_IFACE + '.ObjectManager' in ifaces:
                    obj = conn.get_object(service, path, introspect=False)
                    iface = dbus.Interface(
                        obj, dbus.BUS_DAEMON_IFACE + '.ObjectManager')
                    self.gmo_pending.append(path)
                    iface.GetManagedObjects.call_async(
                        reply_handler=lambda x: self._gmo_callback(
                            path, x),
                        error_handler=lambda e: error_callback(
                            service, path, e))
                else:
                    children = filter(
                        bool,
                        [x.attrib.get('name') for x in root.findall('node')])
                    children = [
                        self._to_path(chain(path_elements,
                                            self._to_path_elements(x)))
                        for x in sorted(children)]
                    for child in filter(subtree_match, children):
                        if child not in self.results:
                            self._find_interfaces(child)
            except Exception as e:
                error_callback(service, path, e)
                return None

            self.check_done()

        def _find_interfaces(self, path):
            path = self._to_path(self._to_path_elements(path))
            obj = conn.get_object(service, path, introspect=False)
            iface = dbus.Interface(obj, dbus.INTROSPECTABLE_IFACE)
            self.introspect_pending.append(path)
            iface.Introspect.call_async(
                reply_handler=lambda x: self._introspect_callback(path, x),
                error_handler=lambda x: error_callback(service, path, x))

    return _FindInterfaces()(path)


@obmc.dbuslib.bindings.add_interfaces([obmc.dbuslib.enums.OBMC_ASSOC_IFACE])
class Association(obmc.dbuslib.bindings.DbusProperties):
    """Implementation of org.openbmc.Association."""

    iface = obmc.dbuslib.enums.OBMC_ASSOC_IFACE

    def __init__(self, bus, path, endpoints):
        """Construct an Association.

        Arguments:
        bus -- The python-dbus connection to host the interface
        path -- The D-Bus object path on which to implement the interface
        endpoints -- A list of the initial association endpoints
        """
        super(Association, self).__init__(conn=bus, object_path=path)
        self.properties = {self.iface: {'endpoints': endpoints}}
        self.unmask_signals()


class Manager(obmc.dbuslib.bindings.DbusObjectManager):
    def __init__(self, bus, path):
        super(Manager, self).__init__(conn=bus, object_path=path)


class ObjectMapper(dbus.service.Object):
    def __init__(
            self, bus, path, namespaces, interface_namespaces,
            blacklist, interface_blacklist):
        super(ObjectMapper, self).__init__(bus, path)
        self.cache = obmc.utils.pathtree.PathTree()
        self.bus = bus
        self.service = None
        self.index = {}
        self.manager = Manager(bus, obmc.dbuslib.bindings.OBJ_PREFIX)
        self.bus_map = {}
        self.defer_signals = {}
        self.namespaces = namespaces
        self.interface_namespaces = interface_namespaces
        self.blacklist = blacklist
        self.blacklist.append(obmc.mapper.MAPPER_PATH)
        self.interface_blacklist = interface_blacklist

        # add my object mananger instance
        self.add_new_objmgr(
            obmc.dbuslib.bindings.OBJ_PREFIX, obmc.mapper.MAPPER_NAME)

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
            arg0=obmc.dbuslib.enums.OBMC_ASSOCIATIONS_IFACE,
            path_keyword='path',
            sender_keyword='sender')

        print("ObjectMapper startup complete.  Discovery in progress...")
        self.discover()
        gobject.idle_add(self.claim_name)

    def claim_name(self):
        if len(self.defer_signals):
            return True
        print("ObjectMapper discovery complete")
        self.service = dbus.service.BusName(
            obmc.mapper.MAPPER_NAME, self.bus)
        self.manager.unmask_signals()
        return False

    def discovery_callback(self, owner, items):
        if owner in self.defer_signals:
            self.add_items(owner, items)
            pending = self.defer_signals[owner]
            del self.defer_signals[owner]

            for x in pending:
                x()
            self.IntrospectionComplete(owner)

    def discovery_error(self, owner, path, e):
        '''Log a message and remove all traces of the service
        we were attempting to introspect.'''

        if owner in self.defer_signals:

            # Safe to add a reference to the traceback here,
            # since it cannot contain the discovery_error frame.
            exctype, value, tb = sys.exc_info()
            sys.stderr.write(
                '{} discovery failure on {}\n'.format(owner, path))
            if tb:
                traceback.print_exception(exctype, value, tb, file=sys.stderr)
            else:
                sys.stderr.write('{}: {}\n'.format(e.__class__.__name__, e))

            del self.defer_signals[owner]
            del self.bus_map[owner]

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
        new = set(old).union([dbus.BUS_DAEMON_IFACE + '.ObjectManager'])
        self.update_interfaces(path, owner, old, new)

    def defer_signal(self, owner, callback):
        self.defer_signals.setdefault(owner, []).append(callback)

    def interfaces_added_handler(self, path, iprops, **kw):
        path = str(path)
        owner = self.bus_normalize(str(kw['sender']))
        if not owner:
            return
        interfaces = self.filter_signal_interfaces(iprops.keys())
        if not interfaces:
            return

        if owner not in self.defer_signals:
            self.add_new_objmgr(str(kw['sender_path']), owner)
            cache_entry = self.cache_get(path)
            old = self.interfaces_get(cache_entry, owner)
            new = set(interfaces).union(old)
            new = {x: iprops.get(x, {}) for x in new}
            self.update_interfaces(path, owner, old, new)
        else:
            self.defer_signal(
                owner,
                lambda: self.interfaces_added_handler(
                    path, iprops, **kw))

    def interfaces_removed_handler(self, path, interfaces, **kw):
        path = str(path)
        owner = self.bus_normalize(str(kw['sender']))
        if not owner:
            return
        interfaces = self.filter_signal_interfaces(interfaces)
        if not interfaces:
            return

        if owner not in self.defer_signals:
            self.add_new_objmgr(str(kw['sender_path']), owner)
            cache_entry = self.cache_get(path)
            old = self.interfaces_get(cache_entry, owner)
            new = set(old).difference(interfaces)
            self.update_interfaces(path, owner, old, new)
        else:
            self.defer_signal(
                owner,
                lambda: self.interfaces_removed_handler(
                    path, interfaces, **kw))

    def properties_changed_handler(self, interface, new, old, **kw):
        owner = self.bus_normalize(str(kw['sender']))
        path = str(kw['path'])
        if not owner:
            return
        interfaces = self.filter_signal_interfaces([interface])
        if not self.is_association(interfaces):
            return
        associations = new.get('associations', None)
        if associations is None:
            return

        if owner not in self.defer_signals:
            associations = [
                (str(x), str(y), str(z)) for x, y, z in associations]
            self.update_associations(
                path, owner,
                self.index_get_associations(path, [owner]),
                associations)
        else:
            self.defer_signal(
                owner,
                lambda: self.properties_changed_handler(
                    interface, new, old, **kw))

    def process_new_owner(self, owned_name, owner):
        # unique name
        try:
            return self.discover([(owned_name, owner)])
        except dbus.exceptions.DBusException as e:
            if obmc.dbuslib.enums.DBUS_UNKNOWN_SERVICE \
                    not in e.get_dbus_name():
                raise

    def process_old_owner(self, owned_name, owner):
        if owner in self.bus_map:
            del self.bus_map[owner]

        for path, item in self.cache.dataitems():
            old = self.interfaces_get(item, owned_name)
            if old:
                # remove all interfaces for this service
                self.update_interfaces(
                    path, owned_name, old=old, new=[])

    def bus_handler(self, owned_name, old, new):
        if obmc.dbuslib.bindings.is_unique(owned_name) or \
                owned_name == obmc.mapper.MAPPER_NAME:
            return

        if new:
            self.process_new_owner(owned_name, new)
        if old:
            # discard any unhandled signals
            # or in progress discovery
            if owned_name in self.defer_signals:
                del self.defer_signals[owned_name]

            self.process_old_owner(owned_name, old)

    def update_interfaces(self, path, owner, old, new):
        # __xx -> intf list
        # xx -> intf dict
        if isinstance(old, dict):
            __old = old.keys()
        else:
            __old = old
            old = defaultdict(dict)
        if isinstance(new, dict):
            __new = new.keys()
        else:
            __new = new
            new = defaultdict(dict)

        cache_entry = self.cache.setdefault(path, {})
        created = [] if self.has_interfaces(cache_entry) else [path]
        added = set(__new).difference(__old)
        removed = set(__old).difference(__new)
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
        for path, items in bus_items.items():
            self.update_interfaces(path, str(owner), old=[], new=items)

    def path_match(self, path):
        match = False

        if not any([x for x in self.blacklist if x in path]):
            # not blacklisted

            if any([x for x in self.namespaces if x in path]):
                # a watched namespace contains the path
                match = True
            elif any([path for x in self.namespaces if path in x]):
                # the path contains a watched namespace
                match = True

        return match

    def interface_match(self, interface):
        match = True

        if any([x for x in self.interface_blacklist if x in interface]):
            # not blacklisted
            match = False
        elif not any([x for x in self.interface_namespaces if x in interface]):
            # the interface contains a watched interface namespace
            match = False

        return match

    def discovery_error_retry(self, owner, path, e):
        sys.stderr.write(
            '{} discovery failure on {} - retry\n'.format(owner, path))
        find_dbus_interfaces(self.bus, owner, '/',
                             self.discovery_callback,
                             self.discovery_error,
                             subtree_match=self.path_match,
                             iface_match=self.interface_match)

    def discover(self, owners=None):
        if owners is None:
            owners = []

        def get_owner(name):
            try:
                return (name, self.bus.get_name_owner(name))
            except Exception:
                traceback.print_exception(*sys.exc_info())

        if not owners:
            owned_names = [x for x in self.bus.list_names()
                           if not obmc.dbuslib.bindings.is_unique(x)]
            owners = filter(bool, (get_owner(name) for name in owned_names))
        for owned_name, o in owners:
            if not self.bus_normalize(owned_name):
                continue
            self.bus_map[o] = owned_name
            self.defer_signals[owned_name] = []
            find_dbus_interfaces(
                self.bus, owned_name, '/',
                self.discovery_callback,
                self.discovery_error_retry,
                subtree_match=self.path_match,
                iface_match=self.interface_match)

    def bus_normalize(self, name):
        '''
        Normalize on well-known names and filter signals
        originating from the mapper.
        '''

        if obmc.dbuslib.bindings.is_unique(name):
            name = self.bus_map.get(name)

        if name == obmc.mapper.MAPPER_NAME:
            return None

        return name

    def filter_signal_interfaces(self, interfaces):
        return [str(x) for x in interfaces if self.interface_match(x)]

    @staticmethod
    def interfaces_get(item, owner, default=None):
        if default is None:
            default = []
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

    @staticmethod
    def filter_interfaces(item, ifaces):
        return ObjectMapper._filter_interfaces(item, set(ifaces))

    @staticmethod
    def _filter_interfaces(item, ifaces):
        if isinstance(item, dict):
            # Called with a single object.
            if not ifaces:
                return item

            filtered = dict()
            for k, v in item.items():
                isec = ifaces.intersection(v)
                if isec:
                    filtered[k] = isec

            return filtered

        # Called with a list of path/object tuples.
        if not ifaces:
            return dict(item)

        if not item:
            return dict()

        filtered = dict()
        for i in item:
            children = ObjectMapper._filter_interfaces(i[1], ifaces)
            if children:
                filtered[i[0]] = children

        return filtered

    @dbus.service.method(obmc.mapper.MAPPER_IFACE, 'sas', 'a{sas}')
    def GetObject(self, path, interfaces):
        o = self.cache_get(path)
        if not o:
            raise MapperNotFoundException(path)

        return self.filter_interfaces(o, interfaces)

    @dbus.service.method(obmc.mapper.MAPPER_IFACE, 'sias', 'as')
    def GetSubTreePaths(self, path, depth, interfaces):
        try:
            return self.filter_interfaces(
                self.cache.iteritems(path, depth),
                interfaces)
        except KeyError:
            raise MapperNotFoundException(path)

    @dbus.service.method(obmc.mapper.MAPPER_IFACE, 'sias', 'a{sa{sas}}')
    def GetSubTree(self, path, depth, interfaces):
        try:
            return self.filter_interfaces(
                self.cache.dataitems(path, depth),
                interfaces)
        except KeyError:
            raise MapperNotFoundException(path)

    @staticmethod
    def has_interfaces(item):
        return any(item.values())

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
        assoc = self.manager.get(path, None)

        old_endpoints = set(assoc.Get(iface, 'endpoints') if assoc else [])
        new_endpoints = old_endpoints.union(added).difference(removed)

        if old_endpoints == new_endpoints:
            return

        create = [] if old_endpoints else [iface]
        delete = [] if new_endpoints else [iface]

        if create:
            self.manager.add(
                path, Association(self.bus, path, list(new_endpoints)))
        elif delete:
            self.manager.remove(path)
        else:
            assoc.Set(iface, 'endpoints', list(new_endpoints))

        if create != delete:
            self.update_interfaces(
                path, obmc.mapper.MAPPER_NAME, delete, create)

    def update_associations(
            self, path, owner, old, new, created=[], destroyed=[]):
        added = set(new).difference(old)
        removed = set(old).difference(new)
        for forward, reverse, endpoint in added:
            if not endpoint:
                # skip associations without an endpoint
                continue

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

    @dbus.service.method(obmc.mapper.MAPPER_IFACE, 'sas', 'a{sa{sas}}')
    def GetAncestors(self, path, interfaces):
        if not self.cache_get(path):
            raise MapperNotFoundException(path)

        objs = {}

        def parents(path):
            yield "/"
            parent = ""
            for elem in (x for x in list(filter(bool, path.split('/')))[:-1]):
                parent += "/" + elem
                yield parent

        for parent in parents(path):
            obj = self.cache_get(parent)
            if not obj:
                continue
            objs[parent] = obj

        return self.filter_interfaces(objs.items(), interfaces)

    @dbus.service.signal(obmc.mapper.MAPPER_IFACE + '.Private', 's')
    def IntrospectionComplete(self, name):
        pass


def server_main(
        path_namespaces,
        interface_namespaces,
        blacklists,
        interface_blacklists):
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    o = ObjectMapper(
        bus,
        obmc.mapper.MAPPER_PATH,
        path_namespaces,
        interface_namespaces,
        blacklists,
        interface_blacklists)
    loop = gobject.MainLoop()

    loop.run()
