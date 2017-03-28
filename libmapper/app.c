/**
 * Copyright 2016 IBM Corporation
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
#include <stdio.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include "mapper.h"

static int call_main(int argc, char *argv[])
{
	int r;
	sd_bus *conn = NULL;
	char *service = NULL;
	sd_bus_message *m = NULL, *reply = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	if(argc < 5) {
		fprintf(stderr, "Usage: %s call OBJECTPATH INTERFACE "
				"METHOD [SIGNATURE [ARGUMENT...]\n", argv[0]);
		r = -1;
		goto finish;
	}

	r = sd_bus_default_system(&conn);
	if(r < 0) {
		fprintf(stderr, "Error connecting to system bus: %s\n",
				strerror(-r));
		goto finish;
	}

	r = mapper_get_service(conn, argv[2], &service);
	if(r < 0) {
		fprintf(stderr, "Error finding '%s' service: %s\n",
				argv[2], strerror(-r));
		goto finish;
	}

	r = sd_bus_message_new_method_call(
			conn, &m, service, argv[2], argv[3], argv[4]);
	if(r < 0) {
		fprintf(stderr, "Error populating message: %s\n",
				strerror(-r));
		goto finish;
	}

	if(argc > 5) {
		char **p;
		p = argv + 6;
		r = sd_bus_message_append_cmdline(m, argv[5], &p);
		if(r < 0) {
			fprintf(stderr, "Error appending method arguments: %s\n",
					strerror(-r));
			goto finish;
		}
	}

	r = sd_bus_call(conn, m, 0, &error, &reply);
	if(r < 0) {
		fprintf(stderr, "Error invoking method: %s\n",
				strerror(-r));
		goto finish;
	}

finish:
	exit(r < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void quit(int r, void *loop)
{
	sd_event_exit((sd_event *)loop, r);
}

static int wait_main(int argc, char *argv[])
{
	int r;
	sd_bus *conn = NULL;
	sd_event *loop = NULL;
	mapper_async_wait *wait = NULL;

	if(argc < 3) {
		fprintf(stderr, "Usage: %s wait OBJECTPATH...\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	r = sd_bus_default_system(&conn);
	if(r < 0) {
		fprintf(stderr, "Error connecting to system bus: %s\n",
				strerror(-r));
		goto finish;
	}

	r = sd_event_default(&loop);
	if (r < 0) {
		fprintf(stderr, "Error obtaining event loop: %s\n",
				strerror(-r));

		goto finish;
	}

	r = sd_bus_attach_event(conn, loop, SD_EVENT_PRIORITY_NORMAL);
	if (r < 0) {
		fprintf(stderr, "Failed to attach system "
				"bus to event loop: %s\n",
				strerror(-r));
		goto finish;
	}

	if (!strcmp(argv[1], "wait"))
		r = mapper_wait_async(conn, loop, argv+2, quit, loop, &wait, true);
	else if (!strcmp(argv[1], "wait-until-removed"))
		r = mapper_wait_async(conn, loop, argv+2, quit, loop, &wait, false);
	if(r < 0) {
		fprintf(stderr, "Error configuring waitlist: %s\n",
				strerror(-r));
		goto finish;
	}

	r = sd_event_loop(loop);
	if(r < 0) {
		fprintf(stderr, "Error starting event loop: %s\n",
				strerror(-r));
		goto finish;
	}

finish:
	exit(r < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
}

/* print out the distinct dbus service name for the input dbus path */
static int get_service_main(int argc, char *argv[])
{
	int r;
	sd_bus *conn = NULL;
	char *service = NULL;

	if(argc != 3) {
		fprintf(stderr, "Usage: %s get-service OBJECTPATH\n",
				argv[0]);
		exit(EXIT_FAILURE);
	}

	r = sd_bus_default_system(&conn);
	if(r < 0) {
		fprintf(stderr, "Error connecting to system bus: %s\n",
				strerror(-r));
		goto finish;
	}

	r = mapper_get_service(conn, argv[2], &service);
	if(r < 0) {
		fprintf(stderr, "Error finding '%s' service: %s\n",
				argv[2], strerror(-r));
		goto finish;
	}

	printf("%s\n", service);


finish:
	exit(r < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	static const char *usage =
		"Usage: %s {COMMAND} ...\n"
		"\nCOMMANDS:\n"
		"  call           invoke the specified method\n"
		"  wait           wait for the specified objects to appear on the DBus\n"
		"  wait-until-removed"
		"		wait until the specified objects are not present in the DBus\n"
		"  get-service    return the service identifier for input path\n";

	if(argc < 2) {
		fprintf(stderr, usage, argv[0]);
		exit(EXIT_FAILURE);
	}

	if(!strcmp(argv[1], "call"))
		call_main(argc, argv);
	if(!strcmp(argv[1], "wait") ||
	   !strcmp(argv[1], "wait-until-removed"))
		wait_main(argc, argv);
	if(!strcmp(argv[1], "get-service"))
		get_service_main(argc, argv);

	fprintf(stderr, usage, argv[0]);
	exit(EXIT_FAILURE);
}
