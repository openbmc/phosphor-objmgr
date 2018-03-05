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

import sys
import dbus
import dbus.mainloop.glib
# TODO: remove support for python 2 once we're at yocto 2.4
try: # python 2
    import gobject
except ImportError: # python 3
    from gi.repository import GObject as gobject
import obmc.mapper


class Wait(object):
    def __init__(self, bus, waitlist, *a, **kw):
        self.bus = bus
        self.waitlist = dict(list(zip(waitlist, [None] * len(waitlist))))
        mapper = bus.get_object(
            obmc.mapper.MAPPER_NAME,
            obmc.mapper.MAPPER_PATH,
            introspect=False)
        self.iface = dbus.Interface(
            mapper, dbus_interface=obmc.mapper.MAPPER_IFACE)
        self.done = False
        self.callback = kw.pop('callback', None)
        self.error_callback = kw.pop('error_callback', self.default_error)
        self.busy_retries = kw.pop('busy_retries', 20)
        self.busy_retry_delay_milliseconds = kw.pop(
            'busy_retry_delay_milliseconds', 500)
        self.waitlist_keyword = kw.pop('waitlist_keyword', None)

        self.bus.add_signal_receiver(
            self.introspection_handler,
            dbus_interface=obmc.mapper.MAPPER_IFACE + '.Private',
            signal_name='IntrospectionComplete')
        self.bus.add_signal_receiver(
            self.introspection_handler,
            dbus_interface=dbus.BUS_DAEMON_IFACE + '.ObjectManager')

        self.introspection_handler()

    @staticmethod
    def default_error(e):
        raise e

    def force_done(self):
        if self.done:
            return

        self.done = True
        self.bus.remove_signal_receiver(
            self.introspection_handler,
            dbus_interface=obmc.mapper.MAPPER_IFACE + '.Private',
            signal_name='IntrospectionComplete')
        self.bus.remove_signal_receiver(
            self.introspection_handler,
            dbus_interface=dbus.BUS_DAEMON_IFACE + '.ObjectManager',
            signal_name='InterfacesAdded')

    def check_done(self):
        if not all(self.waitlist.values()) or self.done:
            return

        self.force_done()

        if self.callback:
            kwargs = {}
            if self.waitlist_keyword:
                kwargs[waitlist_keyword] = self.waitlist
            self.callback(**kwargs)

    def get_object_async(self, path, retry):
        method = getattr(self.iface, 'GetObject')
        method.call_async(
            path,
            [],
            signature='sas',
            reply_handler=lambda x: self.get_object_callback(
                path, x),
            error_handler=lambda x: self.get_object_error(
                path, retry, x))
        return False

    def get_object_error(self, path, retry, e):
        if self.done:
            return

        if e.get_dbus_name() == 'org.freedesktop.DBus.Error.FileNotFound':
            pass
        elif e.get_dbus_name() in \
            ['org.freedesktop.DBus.Error.ObjectPathInUse',
             'org.freedesktop.DBus.Error.LimitsExceeded']:
            if retry > self.busy_retries:
                self.force_done()
                self.error_callback(e)
            else:
                gobject.timeout_add(
                    self.busy_retry_delay_milliseconds,
                    self.get_object_async,
                    path,
                    retry + 1)
        else:
            self.force_done()
            self.error_callback(e)

    def get_object_callback(self, path, info):
        self.waitlist[path] = list(info)[0]
        self.check_done()

    def introspection_handler(self, *a, **kw):
        if self.done:
            return

        for path in [
                x for x in list(self.waitlist.keys()) if not self.waitlist[x]]:
            self.get_object_async(path, 0)
