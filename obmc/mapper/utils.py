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
import gobject
import obmc.mapper


class Wait(object):
    def __init__(self, bus, waitlist, *a, **kw):
        self.bus = bus
        self.waitlist = dict(zip(waitlist, [None]*len(waitlist)))
        mapper = bus.get_object(
            obmc.mapper.MAPPER_NAME,
            obmc.mapper.MAPPER_PATH,
            introspect=False)
        self.mapper_iface = dbus.Interface(
            mapper, dbus_interface=obmc.mapper.MAPPER_IFACE)
        self.done = False
        self.callback = kw.pop('callback', None)
        self.callback_keyword = kw.pop('keyword', None)
        self.callback_args = a
        self.callback_kw = kw

        self.bus.add_signal_receiver(
            self.name_owner_changed_handler,
            dbus_interface=dbus.BUS_DAEMON_IFACE,
            signal_name='NameOwnerChanged')
        self.bus.add_signal_receiver(
            self.interfaces_added_handler,
            dbus_interface=dbus.BUS_DAEMON_IFACE + '.ObjectManager',
            sender_keyword='sender',
            signal_name='InterfacesAdded')
        gobject.idle_add(self.name_owner_changed_handler)

    def check_done(self):
        if not all(self.waitlist.values()) or self.done:
            return

        self.done = True
        self.bus.remove_signal_receiver(
            self.name_owner_changed_handler,
            dbus_interface=dbus.BUS_DAEMON_IFACE,
            signal_name='NameOwnerChanged')
        self.bus.remove_signal_receiver(
            self.interfaces_added_handler,
            dbus_interface=dbus.BUS_DAEMON_IFACE + '.ObjectManager',
            signal_name='InterfacesAdded')
        if self.callback:
            if self.callback_keyword:
                self.callback_kw[self.callback_keyword] = self.waitlist
            self.callback(*self.callback_args, **self.callback_kw)

    def get_object_callback(self, path, info):
        self.waitlist[path] = list(info)[0]
        self.check_done()

    def name_owner_changed_handler(self, *a, **kw):
        class Callback(object):
            def __init__(self, func, *args):
                self.func = func
                self.extra_args = args

            def __call__(self, *a):
                return self.func(*(self.extra_args + a))

        if self.done:
            return

        for path in self.waitlist.keys():
            method = getattr(self.mapper_iface, 'GetObject')
            method.call_async(
                path,
                reply_handler=Callback(self.get_object_callback, path))

    def interfaces_added_handler(self, path, *a, **kw):
        if self.done:
            return
        if path in self.waitlist.keys():
            self.waitlist[path] = kw['sender']
        self.check_done()
