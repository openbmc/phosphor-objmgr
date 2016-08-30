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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <systemd/sd-bus.h>
#include "mapper.h"

static const char *async_wait_name_owner_match =
	"type='signal',"
	"sender='org.freedesktop.DBus',"
	"interface='org.freedesktop.DBus',"
	"member='NameOwnerChanged',"
	"path='/org/freedesktop/DBus'";

static const char *async_wait_interfaces_added_match =
	"type='signal',"
	"interface='org.freedesktop.DBus.ObjectManager',"
	"member='InterfacesAdded'";

struct mapper_async_wait
{
	char **objs;
	void (*callback)(int, void *);
	void *userdata;
	sd_bus *conn;
	sd_bus_slot *name_owner_slot;
	sd_bus_slot *intf_slot;
	int *status;
	int count;
	int finished;
	int r;
};

struct async_wait_callback_data
{
	mapper_async_wait *wait;
	const char *path;
};

static int async_wait_match_name_owner_changed(sd_bus_message *, void *,
		sd_bus_error *);
static int async_wait_match_interfaces_added(sd_bus_message *, void *,
		sd_bus_error *);
static int async_wait_check_done(mapper_async_wait *);
static void async_wait_done(int r, mapper_async_wait *);
static int async_wait_get_objects(mapper_async_wait *);

static int sarraylen(char *array[])
{
	int count = 0;
	char **p = array;

	while(*p != NULL) {
		++count;
		++p;
	}

	return count;
}

static void sarrayfree(char *array[])
{
	char **p = array;
	while(*p != NULL) {
		free(*p);
		++p;
	}
	free(array);
}

static char **sarraydup(char *array[])
{
	int count = sarraylen(array);
	int i;
	char **ret = NULL;

	ret = malloc(sizeof(*ret) * count);
	if(!ret)
		return NULL;

	for(i=0; i<count; ++i) {
		ret[i] = strdup(array[i]);
		if(!ret[i])
			goto error;
	}

	return ret;

error:
	sarrayfree(ret);
	return NULL;
}

static int async_wait_getobject_callback(sd_bus_message *m,
		void *userdata,
		sd_bus_error *e)
{
	int i;
	struct async_wait_callback_data *data = userdata;
	mapper_async_wait *wait = data->wait;

	if(wait->finished)
		return 0;
	if(sd_bus_message_get_errno(m))
		return 0;

	for(i=0; i<wait->count; ++i) {
		if(!strcmp(data->path, wait->objs[i])) {
			wait->status[i] = 1;
		}
	}

	free(data);
	if(async_wait_check_done(wait))
		async_wait_done(0, wait);

	return 0;
}

static int async_wait_get_objects(mapper_async_wait *wait)
{
	int i, r;
	struct async_wait_callback_data *data = NULL;

	for(i=0; i<wait->count; ++i) {
		if(wait->status[i])
			continue;
		data = malloc(sizeof(*data));
		data->wait = wait;
		data->path = wait->objs[i];
		r = sd_bus_call_method_async(
				wait->conn,
				NULL,
				"org.openbmc.ObjectMapper",
				"/org/openbmc/ObjectMapper",
				"org.openbmc.ObjectMapper",
				"GetObject",
				async_wait_getobject_callback,
				data,
				"s",
				wait->objs[i]);
		if(r < 0) {
			free(data);
			fprintf(stderr, "Error invoking method: %s\n",
					strerror(-r));
			return r;
		}
	}

	return 0;
}

static int async_wait_match_name_owner_changed(sd_bus_message *m, void *w,
		sd_bus_error *e)
{
	int r;

	mapper_async_wait *wait = w;
	if(wait->finished)
		return 0;

	r = async_wait_get_objects(wait);
	if(r < 0)
		async_wait_done(r, wait);

	return 0;
}

static int async_wait_match_interfaces_added(sd_bus_message *m, void *w,
		sd_bus_error *e)
{
	int i, r;
	mapper_async_wait *wait = w;
	const char *path;

	if(wait->finished)
		return 0;

	r = sd_bus_message_read(m, "o", &path);
	if (r < 0) {
		fprintf(stderr, "Error reading message: %s\n",
				strerror(-r));
		goto finished;
	}

	for(i=0; i<wait->count; ++i) {
		if(!strcmp(path, wait->objs[i]))
			wait->status[i] = 1;
	}

finished:
	if(r < 0 || async_wait_check_done(wait))
		async_wait_done(r < 0 ? r : 0, wait);

	return 0;
}

static void async_wait_done(int r, mapper_async_wait *w)
{
	if(w->finished)
		return;

	w->finished = 1;
	sd_bus_slot_unref(w->name_owner_slot);
	sd_bus_slot_unref(w->intf_slot);

	if(w->callback)
		w->callback(r, w->userdata);
}

static int async_wait_check_done(mapper_async_wait *w)
{
	int i;

	if(w->finished)
		return 1;

	for(i=0; i<w->count; ++i)
		if(!w->status[i])
			return 0;

	return 1;
}

void mapper_wait_async_free(mapper_async_wait *w)
{
	free(w->status);
	sarrayfree(w->objs);
	free(w);
}

int mapper_wait_async(sd_bus *conn,
		char *objs[],
		void (*callback)(int, void *),
		void *userdata,
		mapper_async_wait **w)
{
	int r;
	mapper_async_wait *wait = NULL;

	wait = malloc(sizeof(*wait));
	if(!wait)
		return -ENOMEM;

	memset(wait, 0, sizeof(*wait));
	wait->conn = conn;
	wait->callback = callback;
	wait->userdata = userdata;
	wait->count = sarraylen(objs);
	if(!wait->count)
		return 0;

	wait->objs = sarraydup(objs);
	if(!wait->objs) {
		r = -ENOMEM;
		goto free_wait;
	}

	wait->status = malloc(sizeof(*wait->status) * wait->count);
	if(!wait->status) {
		r = -ENOMEM;
		goto free_objs;
	}
	memset(wait->status, 0, sizeof(*wait->status) * wait->count);

	r = sd_bus_add_match(conn,
                        &wait->name_owner_slot,
			async_wait_name_owner_match,
                        async_wait_match_name_owner_changed,
                        wait);
	if(r < 0) {
		fprintf(stderr, "Error adding match rule: %s\n",
				strerror(-r));
		goto free_status;
	}

	r = sd_bus_add_match(conn,
                        &wait->intf_slot,
			async_wait_interfaces_added_match,
                        async_wait_match_interfaces_added,
                        wait);
	if(r < 0) {
		fprintf(stderr, "Error adding match rule: %s\n",
				strerror(-r));
		goto unref_name_slot;
	}

	r = async_wait_get_objects(wait);
	if(r < 0) {
		fprintf(stderr, "Error calling method: %s\n",
				strerror(-r));
		goto unref_intf_slot;
	}

	*w = wait;

	return 0;

unref_intf_slot:
	sd_bus_slot_unref(wait->intf_slot);
unref_name_slot:
	sd_bus_slot_unref(wait->name_owner_slot);
free_status:
	free(wait->status);
free_objs:
	sarrayfree(wait->objs);
free_wait:
	free(wait);

	return r;
}

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
