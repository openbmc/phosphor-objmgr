/**
 * Copyright © 2016 IBM Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <string.h>
#include <systemd/sd-bus.h>
#include "mapper.h"

int mapper_get_service(sd_bus *conn, const char *obj, char **service)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *request = NULL, *reply = NULL;
	const char *tmp;
	int r;

	r = sd_bus_message_new_method_call(
			conn,
			&request,
			"org.openbmc.ObjectMapper",
			"/org/openbmc/ObjectMapper",
			"org.openbmc.ObjectMapper",
			"GetObject");
	if (r < 0)
		goto exit;

	r = sd_bus_message_append(request, "s", obj);
	if (r < 0)
		goto exit;

	r = sd_bus_call(conn, request, 0, &error, &reply);
	if (r < 0)
		goto exit;

	r = sd_bus_message_enter_container(reply, 0, NULL);
	if (r < 0)
		goto exit;

	r = sd_bus_message_enter_container(reply, 0, NULL);
	if (r < 0)
		goto exit;

	r = sd_bus_message_read(reply, "s", &tmp);
	if (r < 0)
		goto exit;

	*service = strdup(tmp);

exit:
	sd_bus_error_free(&error);
	sd_bus_message_unref(request);
	sd_bus_message_unref(reply);

	return r;
}
