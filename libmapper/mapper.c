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
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include "mapper.h"

static const char *async_wait_introspection_match =
	"type='signal',"
	"sender='xyz.openbmc_project.ObjectMapper',"
	"interface='xyz.openbmc_project.ObjectMapper.Private',"
	"member='IntrospectionComplete'";

static const char *async_wait_interfaces_added_match =
	"type='signal',"
	"interface='org.freedesktop.DBus.ObjectManager',"
	"member='InterfacesAdded'";

static const char *async_wait_interfaces_removed_match =
	"type='signal',"
	"interface='org.freedesktop.DBus.ObjectManager',"
	"member='InterfacesRemoved'";

static const int mapper_busy_retries = 5;
static const uint64_t mapper_busy_delay_interval_usec = 1000000;

struct mapper_async_wait
{
	char **objs;
	char **intfs;
	void (*callback)(int, void *);
	void *userdata;
	sd_event *loop;
	sd_bus *conn;
	sd_bus_slot *introspection_slot;
	sd_bus_slot *intf_slot;
	int *status;
	int count;
	int finished;
	int r;
	bool added;
};

struct async_wait_callback_data
{
	mapper_async_wait *wait;
	const char *path;
	const char *intf;
	sd_event_source *event_source;
	int retry;
};

static int async_wait_match_introspection_complete(sd_bus_message *, void *,
		sd_bus_error *);
static int async_wait_check_done(mapper_async_wait *);
static void async_wait_done(int r, mapper_async_wait *);
static int async_wait_get_objects(mapper_async_wait *);
static int async_wait_getobject_callback(sd_bus_message *,
		void *, sd_bus_error *);
static int async_wait_get_intfs(mapper_async_wait *);
static int async_wait_getintf_callback(sd_bus_message *,
		void *, sd_bus_error *);

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

static int async_wait_timeout_callback(sd_event_source *s,
		uint64_t usec, void *userdata)
{
	int r;
	struct async_wait_callback_data *data = userdata;
	mapper_async_wait *wait = data->wait;

	sd_event_source_unref(data->event_source);
	if (wait->added)
		r = sd_bus_call_method_async(
				wait->conn,
				NULL,
				MAPPER_BUSNAME,
				MAPPER_PATH,
				MAPPER_INTERFACE,
				"GetObject",
				async_wait_getobject_callback,
				data,
				"sas",
				data->path,
				0,
				NULL);
	else
		r = sd_bus_call_method_async(
				wait->conn,
				NULL,
				MAPPER_BUSNAME,
				MAPPER_PATH,
				MAPPER_INTERFACE,
				"GetSubTreePaths",
				async_wait_getintf_callback,
				data,
				"sias",
				data->path,
				0, 1,
				data->intf);
	if(r < 0) {
		async_wait_done(r, wait);
		free(data);
	}

	return 0;
}

/* Check that the specified interfaces do not exist. */
static int async_wait_getintf_callback(sd_bus_message *m,
		void *userdata,
		sd_bus_error *e)
{
	int i, r;
	char *intf = NULL;
	struct async_wait_callback_data *data = userdata;
	mapper_async_wait *wait = data->wait;
	uint64_t now;

	if(wait->finished)
		goto exit;

	r = sd_bus_message_get_errno(m);
fprintf(stderr, "r=%d %s\n", r, strerror(r));

	if(r == ENOENT)
		r = 0;

	if(r == EBUSY && data->retry < mapper_busy_retries) {
		r = sd_event_now(wait->loop,
				CLOCK_MONOTONIC,
				&now);
		if(r < 0) {
			async_wait_done(r, wait);
			goto exit;
		}

		++data->retry;
		r = sd_event_add_time(wait->loop,
				&data->event_source,
				CLOCK_MONOTONIC,
				now + mapper_busy_delay_interval_usec,
				0,
				async_wait_timeout_callback,
				data);
		if(r < 0) {
			async_wait_done(r, wait);
			goto exit;
		}

		return 0;
	}

	if(r) {
		async_wait_done(-r, wait);
		goto exit;
	}

	sd_bus_message_read(m, "as", 1, &intf);
	if (intf == NULL) {
		for(i=0; i<wait->count; ++i) {
			if(!strcmp(data->intf, wait->intfs[i])) {
				wait->status[i] = 1;
			}
		}
	}

	if(async_wait_check_done(wait))
		async_wait_done(0, wait);

exit:
	free(data);
	return 0;
}

static int async_wait_get_intfs(mapper_async_wait *wait)
{
	int i, r;
	struct async_wait_callback_data *data = NULL;

	for(i=0; i<wait->count; ++i) {
		if(wait->status[i])
			continue;
		data = malloc(sizeof(*data));
		data->wait = wait; 
		data->path = wait->objs[i]; 
		data->intf = wait->intfs[i]; 
		data->retry = 0;
		data->event_source = NULL;
		r = sd_bus_call_method_async(
				wait->conn,
				NULL,
				MAPPER_BUSNAME,
				MAPPER_PATH,
				MAPPER_INTERFACE,
				"GetSubTreePaths",
				async_wait_getintf_callback,
				data,
				"sias",
				wait->objs[i],
				0, 1,
				wait->intfs[i]);
		if (r < 0) {
			free(data);
			fprintf(stderr, "Error invoking method: %s\n",
					strerror(-r));
			return r;
		}

	}
	return 0;
}

static int async_wait_getobject_callback(sd_bus_message *m,
		void *userdata,
		sd_bus_error *e)
{
	int i, r;
	struct async_wait_callback_data *data = userdata;
	mapper_async_wait *wait = data->wait;
	uint64_t now;

	if(wait->finished)
		goto exit;

	r = sd_bus_message_get_errno(m);
	if(r == ENOENT)
		goto exit;

	if(r == EBUSY && data->retry < mapper_busy_retries) {
		r = sd_event_now(wait->loop,
				CLOCK_MONOTONIC,
				&now);
		if(r < 0) {
			async_wait_done(r, wait);
			goto exit;
		}

		++data->retry;
		r = sd_event_add_time(wait->loop,
				&data->event_source,
				CLOCK_MONOTONIC,
				now + mapper_busy_delay_interval_usec,
				0,
				async_wait_timeout_callback,
				data);
		if(r < 0) {
			async_wait_done(r, wait);
			goto exit;
		}

		return 0;
	}

	if(r) {
		async_wait_done(-r, wait);
		goto exit;
	}

	for(i=0; i<wait->count; ++i) {
		if(!strcmp(data->path, wait->objs[i])) {
			wait->status[i] = 1;
		}
	}

	if(async_wait_check_done(wait))
		async_wait_done(0, wait);

exit:
	free(data);
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
		data->retry = 0;
		data->event_source = NULL;
		r = sd_bus_call_method_async(
				wait->conn,
				NULL,
				MAPPER_BUSNAME,
				MAPPER_PATH,
				MAPPER_INTERFACE,
				"GetObject",
				async_wait_getobject_callback,
				data,
				"sas",
				wait->objs[i],
				0,
				NULL);
		if(r < 0) {
			free(data);
			fprintf(stderr, "Error invoking method: %s\n",
					strerror(-r));
			return r;
		}
	}

	return 0;
}

static int async_wait_match_introspection_complete(sd_bus_message *m, void *w,
		sd_bus_error *e)
{
	int r;

	mapper_async_wait *wait = w;
	if(wait->finished)
		return 0;

	if (wait->added)
		r = async_wait_get_objects(wait);
	else
		r = async_wait_get_intfs(wait);
	if(r < 0)
		async_wait_done(r, wait);
	return 0;
}

static void async_wait_done(int r, mapper_async_wait *w)
{
	if(w->finished)
		return;

	w->finished = 1;
	sd_bus_slot_unref(w->introspection_slot);
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
		sd_event *loop,
		char *objs[],
		char *intfs[],
		void (*callback)(int, void *),
		void *userdata,
		mapper_async_wait **w,
		bool added)
{
	int r;
	mapper_async_wait *wait = NULL;

	wait = malloc(sizeof(*wait));
	if(!wait)
		return -ENOMEM;

	memset(wait, 0, sizeof(*wait));
	wait->conn = conn;
	wait->loop = loop;
	wait->callback = callback;
	wait->userdata = userdata;
	wait->count = sarraylen(objs);
	if(!wait->count)
		return 0;
	wait->added = added;
	if (intfs != NULL) {
		wait->intfs = sarraydup(intfs);
		if(!wait->intfs) {
			r = -ENOMEM;
			goto free_wait;
		}
	} else
		wait->intfs = NULL;

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

	if (wait->added) {
		r = sd_bus_add_match(conn,
				&wait->introspection_slot,
				async_wait_introspection_match,
				async_wait_match_introspection_complete,
				wait);
		if(r < 0) {
			fprintf(stderr, "Error adding match rule: %s\n",
					strerror(-r));
			goto free_status;
		}

		r = sd_bus_add_match(conn,
				&wait->intf_slot,
				async_wait_interfaces_added_match,
				async_wait_match_introspection_complete,
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
	} else {
		r = sd_bus_add_match(conn,
				&wait->intf_slot,
				async_wait_interfaces_removed_match,
				async_wait_match_introspection_complete,
				wait);
		if(r < 0) {
			fprintf(stderr, "Error adding match rule: %s\n",
					strerror(-r));
			goto unref_name_slot;
		}

		r = async_wait_get_intfs(wait);
		if(r < 0) {
			fprintf(stderr, "Error calling method: %s\n",
					strerror(-r));
			goto unref_intf_slot;
		}
	}

	*w = wait;

	return 0;

unref_intf_slot:
	sd_bus_slot_unref(wait->intf_slot);
unref_name_slot:
	sd_bus_slot_unref(wait->introspection_slot);
free_status:
	free(wait->status);
free_objs:
	sarrayfree(wait->objs);
	if (wait->intfs != NULL)
		sarrayfree(wait->intfs);
free_wait:
	free(wait);

	return r;
}

int mapper_get_object(sd_bus *conn, const char *obj, sd_bus_message **reply)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *request = NULL;
	int r, retry = 0;

	r = sd_bus_message_new_method_call(
			conn,
			&request,
			MAPPER_BUSNAME,
			MAPPER_PATH,
			MAPPER_INTERFACE,
			"GetObject");
	if (r < 0)
		goto exit;

	r = sd_bus_message_append(request, "s", obj);
	if (r < 0)
		goto exit;
	r = sd_bus_message_append(request, "as", 0, NULL);
	if (r < 0)
		goto exit;

	while(retry < mapper_busy_retries) {
		sd_bus_error_free(&error);
		r = sd_bus_call(conn, request, 0, &error, reply);
		if (r < 0 && sd_bus_error_get_errno(&error) == EBUSY) {
			++retry;

			if(retry != mapper_busy_retries)
				usleep(mapper_busy_delay_interval_usec);
			continue;
		}
		break;
	}

	if (r < 0)
		goto exit;

exit:
	sd_bus_error_free(&error);
	sd_bus_message_unref(request);

	return r;
}

int mapper_get_service(sd_bus *conn, const char *obj, char **service)
{
	sd_bus_message *reply = NULL;
	const char *tmp;
	int r;

	r = mapper_get_object(conn, obj, &reply);
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
	sd_bus_message_unref(reply);

	return r;
}
