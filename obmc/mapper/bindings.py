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
import obmc.dbuslib.enums
import obmc.utils.pathtree


MAPPER_NAME = 'xyz.openbmc_project.ObjectMapper'
MAPPER_IFACE = MAPPER_NAME
MAPPER_PATH = '/xyz/openbmc_project/object_mapper'
MAPPER_NOT_FOUND = 'org.freedesktop.DBus.Error.FileNotFound'


class Mapper:
    def __init__(self, bus):
        self.bus = bus
        obj = bus.get_object(MAPPER_NAME, MAPPER_PATH, introspect=False)
        self.iface = dbus.Interface(
            obj, dbus_interface=MAPPER_IFACE)

    @staticmethod
    def retry(func, retries, interval):
        e = None
        count = 0
        while count < retries:
            try:
                return func()
            except dbus.exceptions.DBusException as e:
                if e.get_dbus_name() not in \
                    ['org.freedesktop.DBus.Error.ObjectPathInUse',
                     'org.freedesktop.DBus.Error.LimitsExceeded']:
                    raise

                count += 1
                if interval > 0:
                    from time import sleep
                    sleep(interval)
        if e:
            raise e

    def get_object(self, path, retries=5, interfaces=[], interval=0.2):
        return self.retry(
            lambda: self.iface.GetObject(
                path, interfaces, signature='sas'),
            retries, interval)

    def get_subtree_paths(
            self, path='/', depth=0, retries=5, interfaces=[], interval=0.2):
        return self.retry(
            lambda: self.iface.GetSubTreePaths(
                path, depth, interfaces, signature='sias'),
            retries, interval)

    def get_subtree(
            self, path='/', depth=0, retries=5, interfaces=[], interval=0.2):
        return self.retry(
            lambda: self.iface.GetSubTree(
                path, depth, interfaces, signature='sias'),
            retries, interval)

    def get_ancestors(self, path, retries=5, interfaces=[], interval=0.2):
        return self.retry(
            lambda: self.iface.GetAncestors(
                path, interfaces, signature='sas'),
            retries, interval)

    @staticmethod
    def __try_properties_interface(f, *a):
        try:
            return f(*a)
        except dbus.exceptions.DBusException as e:
            if obmc.dbuslib.enums.DBUS_UNKNOWN_INTERFACE in \
                    e.get_dbus_name():
                # interface doesn't have any properties
                return None
            if obmc.dbuslib.enums.DBUS_UNKNOWN_METHOD == e.get_dbus_name():
                # properties interface not implemented at all
                return None
            raise

    @staticmethod
    def __get_properties_on_iface(properties_iface, iface):
        properties = Mapper.__try_properties_interface(
            properties_iface.GetAll, iface)
        if properties is None:
            return {}
        return properties

    def __get_properties_on_bus(self, path, bus, interfaces, match):
        properties = {}
        obj = self.bus.get_object(bus, path, introspect=False)
        properties_iface = dbus.Interface(
            obj, dbus_interface=dbus.PROPERTIES_IFACE)
        for i in interfaces:
            if match and not match(i):
                continue
            properties.update(self.__get_properties_on_iface(
                properties_iface, i))

        return properties

    def enumerate_object(
            self, path,
            match=lambda x: x != dbus.BUS_DAEMON_IFACE + '.ObjectManager',
            mapper_data=None):
        if mapper_data is None:
            mapper_data = {path: self.get_object(path)}

        obj = {}

        for owner, interfaces in list(mapper_data[path].items()):
            obj.update(
                self.__get_properties_on_bus(
                    path, owner, interfaces, match))

        return obj

    def enumerate_subtree(
            self, path='/',
            match=lambda x: x != dbus.BUS_DAEMON_IFACE + '.ObjectManager',
            mapper_data=None):
        if mapper_data is None:
            mapper_data = self.get_subtree(path)
        managers = {}
        owners = []

        # look for objectmanager implementations as they result
        # in fewer dbus calls
        for path, bus_data in list(mapper_data.items()):
            for owner, interfaces in list(bus_data.items()):
                owners.append(owner)
                if dbus.BUS_DAEMON_IFACE + '.ObjectManager' in interfaces:
                    managers[owner] = path

        # also look in the parent objects
        ancestors = self.get_ancestors(path)

        # finally check the root for one too
        try:
            ancestors.update({path: self.get_object(path)})
        except dbus.exceptions.DBusException as e:
            if e.get_dbus_name() != MAPPER_NOT_FOUND:
                raise

        for path, bus_data in list(ancestors.items()):
            for owner, interfaces in list(bus_data.items()):
                if dbus.BUS_DAEMON_IFACE + '.ObjectManager' in interfaces:
                    managers[owner] = path

        # make all the manager gmo (get managed objects) calls
        results = {}
        for owner, path in list(managers.items()):
            if owner not in owners:
                continue
            obj = self.bus.get_object(owner, path, introspect=False)
            iface = dbus.Interface(
                obj, dbus.BUS_DAEMON_IFACE + '.ObjectManager')

            # flatten (remove interface names) gmo results
            for path, interfaces in list(iface.GetManagedObjects().items()):
                if path not in iter(list(mapper_data.keys())):
                    continue
                properties = {}
                for iface, props in list(interfaces.items()):
                    properties.update(props)
                results.setdefault(path, {}).setdefault(owner, properties)

        # make dbus calls for any remaining objects
        for path, bus_data in list(mapper_data.items()):
            for owner, interfaces in list(bus_data.items()):
                if results.setdefault(path, {}).setdefault(owner, {}):
                    continue
                results[path][owner].update(
                    self.__get_properties_on_bus(
                        path, owner, interfaces, match))

        objs = obmc.utils.pathtree.PathTree()
        for path, owners in list(results.items()):
            for owner, properties in list(owners.items()):
                objs.setdefault(path, {}).update(properties)

        return objs
