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
import obmc.dbuslib.enums
import obmc.mapper
import obmc.mapper.utils
import optparse


def add_systemd_path_option(parser):
    parser.add_option(
        '-s', '--systemd', action='store_true', default=False,
        help='interpret-dash-delimited-path-arguments-as-paths')


def systemd_to_dbus(item):
    if not item.startswith('/'):
        item = '/%s' % item.replace('-', '/')
    return item


class CallApp(object):
    usage = 'OBJECTPATH INTERFACE METHOD ARGUMENTS...'
    description = 'Invoke a DBus method on the named DBus object.'

    def setup(self, parser, command):
        add_systemd_path_option(parser)

    def main(self, parser):
        args = parser.largs
        try:
            path, interface, method, parameters = \
                args[0], args[1], args[2], args[3:]
        except IndexError:
            parser.error('Not enough arguments')

        bus = dbus.SystemBus()
        mapper = obmc.mapper.Mapper(bus)
        if parser.values.systemd:
            path = systemd_to_dbus(path)

        try:
            service_info = mapper.get_object(path)
        except dbus.exceptions.DBusException, e:
            if e.get_dbus_name() != obmc.mapper.MAPPER_NOT_FOUND:
                raise
            parser.error('\'%s\' was not found' % path)

        obj = bus.get_object(list(service_info)[0], path, introspect=False)
        iface = dbus.Interface(obj, interface)
        func = getattr(iface, method)
        try:
            return func(*parameters)
        except dbus.exceptions.DBusException, e:
            if e.get_dbus_name() != obmc.dbuslib.enums.DBUS_UNKNOWN_METHOD:
                raise
            parser.error(
                '\'%s.%s\' is not a valid method for \'%s\''
                % (interface, method, path))


class WaitApp(object):
    usage = 'OBJECTPATH...'
    description = 'Wait for one or more DBus ' \
        'object(s) to appear on the system bus.'

    def setup(self, parser, command):
        add_systemd_path_option(parser)

    def main(self, parser):
        if not parser.largs:
            parser.error('Specify one or more object paths')

        dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
        loop = gobject.MainLoop()
        bus = dbus.SystemBus()
        if parser.values.systemd:
            waitlist = [systemd_to_dbus(x) for x in parser.largs]
        else:
            waitlist = parser.largs

        waiter = obmc.mapper.utils.Wait(bus, waitlist, callback=loop.quit)
        loop.run()


def mapper_main():
    all_commands = []
    usage = '''%prog [options] SUBCOMMAND\n\nSUBCOMMANDS:\n'''
    for k, v in sys.modules[__name__].__dict__.iteritems():
        if k.endswith('App'):
            all_commands.append(k.replace('App', '').lower())
            usage += '    %s - %s\n' % (all_commands[-1], v.description)

    parser = optparse.OptionParser(usage=usage)
    commands = list(set(sys.argv[1:]).intersection(all_commands))
    if len(commands) != 1:
        parser.error('Specify a single sub-command')

    classname = '%sApp' % commands[0].capitalize()
    cls = getattr(sys.modules[__name__], classname)
    usage = getattr(cls, 'usage')

    parser.set_usage('%%prog %s [options] %s' % (commands[0], usage))
    inst = cls()
    inst.setup(parser, commands[0])
    opts, args = parser.parse_args()
    parser.largs = parser.largs[1:]
    return inst.main(parser)
