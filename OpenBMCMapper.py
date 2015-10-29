#!/usr/bin/env python

# Contributors Listed Below - COPYRIGHT 2015
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

from xml.etree import ElementTree
import dbus

MAPPER_NAME = 'org.openbmc.objectmapper'
MAPPER_IFACE = MAPPER_NAME + '.ObjectMapper'
MAPPER_PATH = '/org/openbmc/objectmapper/objectmapper'

class Path:
	def __init__(self, path):
		self.parts = filter(bool, path.split('/'))

	def rel(self, first = None, last = None):
		# relative
		return self.get('', first, last)

	def fq(self, first = None, last = None):
		# fully qualified
		return self.get('/', first, last)

	def depth(self):
		return len(self.parts)

	def get(self, prefix = '/', first = None, last = None):
		if not first:
			first = 0
		if not last:
			last = self.depth()
		return prefix + '/'.join(self.parts[first:last])

class IntrospectionNodeParser:
	def __init__(self, data):
		self.data = data
		self.cache = {}

	def parse_args(self):
		return [ x.attrib for x in self.data.findall('arg') ]

	def parse_children(self):
		return [ x.attrib['name'] for x in self.data.findall('node') ]

	def parse_method_or_signal(self):
		name = self.data.attrib['name']
		return name, self.parse_args()

	def parse_interface(self):
		iface = {}
		iface['method'] = {}
		iface['signal'] = {}
		name = self.data.attrib['name']

		for node in self.data:
			p = IntrospectionNodeParser(node)
			if node.tag not in ['method', 'signal']:
				continue
			n, element = p.parse_method_or_signal()
			iface[node.tag][n] = element

		return name, iface

	def parse_node(self):
		if self.cache:
			return self.cache

		self.cache['interfaces'] = {}
		self.cache['children'] = []

		for node in self.data:
			p = IntrospectionNodeParser(node)
			if node.tag == 'interface':
				name, ifaces = p.parse_interface()
				self.cache['interfaces'][name] = ifaces
			elif node.tag == 'node':
				self.cache['children'] = self.parse_children()

		return self.cache

	def get_interfaces(self):
		return self.parse_node()['interfaces']

	def get_children(self):
		return self.parse_node()['children']

	def recursive_binding(self):
		return any('/' in s for s in self.get_children())

class IntrospectionParser:
	def __init__(self, name, bus):
		self.name = name
		self.bus = bus

	def _introspect(self, path):
		try:
			obj = self.bus.get_object(self.name, path)
			iface = dbus.Interface(obj, dbus.BUS_DAEMON_IFACE + '.Introspectable')
			data = iface.Introspect()
		except dbus.DBusException:
			return None

		return IntrospectionNodeParser(ElementTree.fromstring(data))

	def _discover_flat(self, path, parser):
		items = {}
		interfaces = parser.get_interfaces().keys()
		if interfaces:
			items[path] = {}
			items[path]['interfaces'] = interfaces

		return items

	def introspect(self, path = '/', parser = None):
		items = {}
		if not parser:
			parser = self._introspect(path)
		if not parser:
			return {}
		items.update(self._discover_flat(path, parser))

		if path != '/':
			path += '/'

		if parser.recursive_binding():
			callback = self._discover_flat
		else:
			callback = self.introspect

		for k in parser.get_children():
			parser = self._introspect(path + k)
			if not parser:
				continue
			items.update(callback(path + k, parser))

		return items
