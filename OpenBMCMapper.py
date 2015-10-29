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
