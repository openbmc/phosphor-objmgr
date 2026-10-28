// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright 2016 IBM Corporation

#include "mapper.h"

#include "internal.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <unistd.h>

#define _public_ __attribute__((__visibility__("default")))
#define _unused_ __attribute__((unused))

static const char* async_wait_introspection_match =
    "type='signal',"
    "sender='xyz.openbmc_project.ObjectMapper',"
    "interface='xyz.openbmc_project.ObjectMapper.Private',"
    "member='IntrospectionComplete'";

static const char* async_wait_interfaces_added_match =
    "type='signal',"
    "interface='org.freedesktop.DBus.ObjectManager',"
    "member='InterfacesAdded'";

static const char* interfaces_removed_match =
    "type='signal',"
    "interface='org.freedesktop.DBus.ObjectManager',"
    "member='InterfacesRemoved'";

static const int mapper_busy_retries = 5;
static const uint64_t mapper_busy_delay_interval_usec = 1000000;

/* Flags in mapper_async_wait.status[]. */
enum
{
    /* The object has been found. */
    ASYNC_WAIT_FOUND = 1,
    /* A GetObject request for the object is outstanding. */
    ASYNC_WAIT_BUSY = 2,
    /* A signal has been received while a request was outstanding. */
    ASYNC_WAIT_RESEND = 4,
};

struct async_wait_callback_data;

struct mapper_async_wait
{
    char** objs;
    void (*callback)(int, void*);
    void* userdata;
    sd_event* loop;
    sd_bus* conn;
    sd_bus_slot* introspection_slot;
    sd_bus_slot* intf_slot;
    /* List of outstanding GetObject requests. */
    struct async_wait_callback_data* requests;
    int* status;
    size_t count;
    int finished;
    int r;
};

/* State of a single GetObject request. */
struct async_wait_callback_data
{
    mapper_async_wait* wait;
    const char* path;
    size_t index;
    sd_bus_slot* call_slot;
    sd_event_source* event_source;
    struct async_wait_callback_data* prev;
    struct async_wait_callback_data* next;
    int retry;
};

struct async_subtree_request;

struct mapper_async_subtree
{
    char* namespace;
    char* interface;
    void (*callback)(int, void*);
    void* userdata;
    sd_event* loop;
    sd_bus* conn;
    sd_bus_slot* slot;
    /* List of outstanding GetSubTreePaths requests. */
    struct async_subtree_request* requests;
    int finished;
    int op;
};

/* State of a single GetSubTreePaths request. */
struct async_subtree_request
{
    mapper_async_subtree* subtree;
    sd_bus_slot* call_slot;
    sd_event_source* event_source;
    struct async_subtree_request* prev;
    struct async_subtree_request* next;
    int retry;
};

static int async_wait_match_introspection_complete(sd_bus_message*, void*,
                                                   sd_bus_error*);
static int async_wait_check_done(mapper_async_wait*);
static void async_wait_done(int r, mapper_async_wait*);
static int async_wait_get_objects(mapper_async_wait*);
static int async_wait_send_request(mapper_async_wait*, size_t);
static int async_wait_getobject_callback(sd_bus_message*, void*, sd_bus_error*);

static int async_subtree_match_callback(sd_bus_message*, void*, sd_bus_error*);
static void async_subtree_done(int r, mapper_async_subtree*);
static int async_subtree_getpaths(mapper_async_subtree*);
static int async_subtree_getpaths_callback(sd_bus_message*, void*,
                                           sd_bus_error*);

size_t sarraylen(char* array[])
{
    size_t count = 0;
    char** p = array;

    while (*p != NULL)
    {
        ++count;
        ++p;
    }

    return count;
}

void sarrayfree(char* array[])
{
    char** p = array;
    while (*p != NULL)
    {
        free(*p);
        ++p;
    }
    free(array);
}

char** sarraydup(char* array[])
{
    size_t count = sarraylen(array);
    size_t i;
    char** ret = NULL;

    ret = calloc(count + 1, sizeof(*ret));
    if (!ret)
    {
        return NULL;
    }

    for (i = 0; i < count; ++i)
    {
        ret[i] = strdup(array[i]);
        if (!ret[i])
        {
            goto error;
        }
    }

    return ret;

error:
    sarrayfree(ret);
    return NULL;
}

/* Cancel a GetObject request and free the memory associated with it. */
static void async_wait_free_request(struct async_wait_callback_data* data)
{
    mapper_async_wait* wait = data->wait;

    sd_bus_slot_unref(data->call_slot);
    sd_event_source_unref(data->event_source);
    wait->status[data->index] &= ~ASYNC_WAIT_BUSY;
    if (data->prev)
    {
        data->prev->next = data->next;
    }
    else
    {
        wait->requests = data->next;
    }
    if (data->next)
    {
        data->next->prev = data->prev;
    }
    free(data);
}

static int async_wait_timeout_callback(_unused_ sd_event_source* s,
                                       _unused_ uint64_t usec, void* userdata)
{
    int r;
    struct async_wait_callback_data* data = userdata;
    mapper_async_wait* wait = data->wait;

    data->event_source = sd_event_source_unref(data->event_source);
    if (wait->finished)
    {
        async_wait_free_request(data);
        return 0;
    }

    /* The request that is sent below covers all signals received so far. */
    wait->status[data->index] &= ~ASYNC_WAIT_RESEND;

    /* The reply callback has already released data->call_slot. */
    r = sd_bus_call_method_async(
        wait->conn, &data->call_slot, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        async_wait_getobject_callback, data, "sas", data->path, 0, NULL);
    if (r < 0)
    {
        async_wait_free_request(data);
        async_wait_done(r, wait);
    }

    return 0;
}

static int async_wait_getobject_callback(sd_bus_message* m, void* userdata,
                                         _unused_ sd_bus_error* e)
{
    size_t i;
    int r;
    struct async_wait_callback_data* data = userdata;
    mapper_async_wait* wait = data->wait;
    uint64_t next_retry;

    /* The reply has been received. Release the reference to the slot. */
    data->call_slot = sd_bus_slot_unref(data->call_slot);

    if (wait->finished)
    {
        goto exit;
    }

    if (sd_bus_message_is_method_error(
            m, "xyz.openbmc_project.Common.Error.ResourceNotFound"))
    {
        i = data->index;
        async_wait_free_request(data);
        /* If a signal has been received while this request was outstanding,
         * the object may have appeared after the mapper processed this
         * request. Hence, ask again. */
        if (wait->status[i] & ASYNC_WAIT_RESEND)
        {
            r = async_wait_send_request(wait, i);
            if (r < 0)
            {
                async_wait_done(r, wait);
            }
        }
        return 0;
    }

    r = sd_bus_message_get_errno(m);

    if ((r == EBUSY || r == ENOBUFS) && data->retry < mapper_busy_retries)
    {
        r = sd_event_now(wait->loop, CLOCK_MONOTONIC, &next_retry);
        if (r < 0)
        {
            goto fail;
        }

        next_retry += mapper_busy_delay_interval_usec * (1 << data->retry);
        r = sd_event_add_time(wait->loop, &data->event_source, CLOCK_MONOTONIC,
                              next_retry, 0, async_wait_timeout_callback, data);
        ++data->retry;
        if (r < 0)
        {
            goto fail;
        }

        return 0;
    }

    if (r)
    {
        r = -r;
        goto fail;
    }

    for (i = 0; i < wait->count; ++i)
    {
        if (!strcmp(data->path, wait->objs[i]))
        {
            wait->status[i] |= ASYNC_WAIT_FOUND;
        }
    }

    /* Free data before invoking the callback since the callback may call
     * mapper_wait_async_free(). */
    async_wait_free_request(data);
    if (async_wait_check_done(wait))
    {
        async_wait_done(0, wait);
    }
    return 0;

fail:
    async_wait_free_request(data);
    async_wait_done(r, wait);
    return 0;

exit:
    async_wait_free_request(data);
    return 0;
}

static int async_wait_send_request(mapper_async_wait* wait, size_t i)
{
    int r;
    struct async_wait_callback_data* data;

    data = calloc(1, sizeof(*data));
    if (!data)
    {
        return -ENOMEM;
    }
    data->wait = wait;
    data->path = wait->objs[i];
    data->index = i;
    data->next = wait->requests;
    if (wait->requests)
    {
        wait->requests->prev = data;
    }
    wait->requests = data;
    wait->status[i] |= ASYNC_WAIT_BUSY;
    wait->status[i] &= ~ASYNC_WAIT_RESEND;
    r = sd_bus_call_method_async(
        wait->conn, &data->call_slot, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        async_wait_getobject_callback, data, "sas", wait->objs[i], 0, NULL);
    if (r < 0)
    {
        async_wait_free_request(data);
        fprintf(stderr, "Error invoking method: %s\n", strerror(-r));
        return r;
    }

    return 0;
}

static int async_wait_get_objects(mapper_async_wait* wait)
{
    size_t i;
    int r;

    for (i = 0; i < wait->count; ++i)
    {
        if (wait->status[i] & ASYNC_WAIT_FOUND)
        {
            continue;
        }
        /* Do not send another request for the same object while one is
         * outstanding. Every InterfacesAdded signal on the bus triggers this
         * function. Instead, send another request once the outstanding
         * request has completed. */
        if (wait->status[i] & ASYNC_WAIT_BUSY)
        {
            wait->status[i] |= ASYNC_WAIT_RESEND;
            continue;
        }
        r = async_wait_send_request(wait, i);
        if (r < 0)
        {
            return r;
        }
    }

    return 0;
}

static int async_wait_match_introspection_complete(
    _unused_ sd_bus_message* m, void* w, _unused_ sd_bus_error* e)
{
    int r;

    mapper_async_wait* wait = w;
    if (wait->finished)
    {
        return 0;
    }

    r = async_wait_get_objects(wait);
    if (r < 0)
    {
        async_wait_done(r, wait);
    }

    return 0;
}

static void async_wait_free_requests(mapper_async_wait* w)
{
    while (w->requests)
    {
        async_wait_free_request(w->requests);
    }
}

static void async_wait_done(int r, mapper_async_wait* w)
{
    if (w->finished)
    {
        return;
    }

    w->finished = 1;
    w->introspection_slot = sd_bus_slot_unref(w->introspection_slot);
    w->intf_slot = sd_bus_slot_unref(w->intf_slot);
    async_wait_free_requests(w);

    if (w->callback)
    {
        w->callback(r, w->userdata);
    }
}

static int async_wait_check_done(mapper_async_wait* w)
{
    size_t i;

    if (w->finished)
    {
        return 1;
    }

    for (i = 0; i < w->count; ++i)
    {
        if (!(w->status[i] & ASYNC_WAIT_FOUND))
        {
            return 0;
        }
    }

    return 1;
}

_public_ void mapper_wait_async_free(mapper_async_wait* w)
{
    if (!w)
    {
        return;
    }

    /* Cancel the match rules and all outstanding requests such that no
     * callback can access w after it has been freed. */
    w->finished = 1;
    sd_bus_slot_unref(w->introspection_slot);
    sd_bus_slot_unref(w->intf_slot);
    async_wait_free_requests(w);
    free(w->status);
    sarrayfree(w->objs);
    free(w);
}

_public_ int mapper_wait_async(sd_bus* conn, sd_event* loop, char* objs[],
                               void (*callback)(int, void*), void* userdata,
                               mapper_async_wait** w)
{
    int r;
    mapper_async_wait* wait = NULL;

    wait = malloc(sizeof(*wait));
    if (!wait)
    {
        return -ENOMEM;
    }

    memset(wait, 0, sizeof(*wait));
    wait->conn = conn;
    wait->loop = loop;
    wait->callback = callback;
    wait->userdata = userdata;
    wait->count = sarraylen(objs);
    if (!wait->count)
    {
        r = 0;
        goto free_wait;
    }

    wait->objs = sarraydup(objs);
    if (!wait->objs)
    {
        r = -ENOMEM;
        goto free_wait;
    }

    wait->status = malloc(sizeof(*wait->status) * wait->count);
    if (!wait->status)
    {
        r = -ENOMEM;
        goto free_objs;
    }
    memset(wait->status, 0, sizeof(*wait->status) * wait->count);

    r = sd_bus_add_match(conn, &wait->introspection_slot,
                         async_wait_introspection_match,
                         async_wait_match_introspection_complete, wait);
    if (r < 0)
    {
        fprintf(stderr, "Error adding match rule: %s\n", strerror(-r));
        goto free_status;
    }

    r = sd_bus_add_match(conn, &wait->intf_slot,
                         async_wait_interfaces_added_match,
                         async_wait_match_introspection_complete, wait);
    if (r < 0)
    {
        fprintf(stderr, "Error adding match rule: %s\n", strerror(-r));
        goto unref_name_slot;
    }

    r = async_wait_get_objects(wait);
    if (r < 0)
    {
        fprintf(stderr, "Error calling method: %s\n", strerror(-r));
        goto unref_intf_slot;
    }

    *w = wait;

    return 0;

unref_intf_slot:
    async_wait_free_requests(wait);
    sd_bus_slot_unref(wait->intf_slot);
unref_name_slot:
    sd_bus_slot_unref(wait->introspection_slot);
free_status:
    free(wait->status);
free_objs:
    sarrayfree(wait->objs);
free_wait:
    free(wait);

    return r;
}

/* Cancel a GetSubTreePaths request and free the memory associated with it. */
static void async_subtree_free_request(struct async_subtree_request* req)
{
    mapper_async_subtree* subtree = req->subtree;

    sd_bus_slot_unref(req->call_slot);
    sd_event_source_unref(req->event_source);
    if (req->prev)
    {
        req->prev->next = req->next;
    }
    else
    {
        subtree->requests = req->next;
    }
    if (req->next)
    {
        req->next->prev = req->prev;
    }
    free(req);
}

static int async_subtree_send_request(struct async_subtree_request* req)
{
    mapper_async_subtree* subtree = req->subtree;

    req->call_slot = sd_bus_slot_unref(req->call_slot);
    return sd_bus_call_method_async(
        subtree->conn, &req->call_slot, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        async_subtree_getpaths_callback, req, "sias", subtree->namespace, 0, 1,
        subtree->interface);
}

static int async_subtree_timeout_callback(
    _unused_ sd_event_source* s, _unused_ uint64_t usec, void* userdata)
{
    int r;
    struct async_subtree_request* req = userdata;
    mapper_async_subtree* subtree = req->subtree;

    req->event_source = sd_event_source_unref(req->event_source);
    if (subtree->finished)
    {
        async_subtree_free_request(req);
        return 0;
    }

    r = async_subtree_send_request(req);
    if (r < 0)
    {
        async_subtree_free_request(req);
        async_subtree_done(r, subtree);
    }

    return 0;
}

static int async_subtree_getpaths_callback(sd_bus_message* m, void* userdata,
                                           _unused_ sd_bus_error* e)
{
    int r;
    struct async_subtree_request* req = userdata;
    mapper_async_subtree* subtree = req->subtree;
    uint64_t next_retry;

    /* The reply has been received. Release the reference to the slot. */
    req->call_slot = sd_bus_slot_unref(req->call_slot);

    if (subtree->finished)
    {
        goto exit;
    }

    if (sd_bus_message_is_method_error(
            m, "xyz.openbmc_project.Common.Error.ResourceNotFound"))
    {
        if (subtree->op == MAPPER_OP_REMOVE)
        {
            /* The namespace does not exist (anymore). Hence, the interface
             * is not present in any of its subtrees. Report success instead
             * of trying to parse the error reply as a list of paths. */
            r = 0;
            goto done;
        }
        goto exit;
    }

    r = sd_bus_message_get_errno(m);

    if ((r == EBUSY || r == ENOBUFS) && req->retry < mapper_busy_retries)
    {
        r = sd_event_now(subtree->loop, CLOCK_MONOTONIC, &next_retry);
        if (r < 0)
        {
            goto done;
        }

        next_retry += mapper_busy_delay_interval_usec * (1 << req->retry);
        r = sd_event_add_time(subtree->loop, &req->event_source,
                              CLOCK_MONOTONIC, next_retry, 0,
                              async_subtree_timeout_callback, req);
        ++req->retry;
        if (r < 0)
        {
            goto done;
        }

        return 0;
    }

    if (r)
    {
        r = -r;
        goto done;
    }

    if (subtree->op == MAPPER_OP_REMOVE)
    {
        r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "s");
        if (r < 0)
        {
            goto done;
        }

        r = sd_bus_message_at_end(m, false);
        if (r < 0)
        {
            goto done;
        }

        /* For remove, operation is complete when the interface is not present
         * we know it is empty if the returned array is empty
         */
        if (r)
        {
            r = 0;
            goto done;
        }
    }

exit:
    async_subtree_free_request(req);
    return 0;

done:
    /* Free req before invoking the callback because the callback may free
     * subtree. */
    async_subtree_free_request(req);
    async_subtree_done(r, subtree);
    return 0;
}

static int async_subtree_getpaths(mapper_async_subtree* subtree)
{
    int r = 0;
    struct async_subtree_request* req;

    /* Each request has its own retry state such that concurrent requests,
     * e.g. triggered by multiple InterfacesRemoved signals, do not interfere
     * with each other. */
    req = calloc(1, sizeof(*req));
    if (!req)
    {
        return -ENOMEM;
    }
    req->subtree = subtree;
    req->next = subtree->requests;
    if (subtree->requests)
    {
        subtree->requests->prev = req;
    }
    subtree->requests = req;

    r = async_subtree_send_request(req);
    if (r < 0)
    {
        async_subtree_free_request(req);
        fprintf(stderr, "Error invoking method: %s\n", strerror(-r));
        return r;
    }

    return 0;
}

static int async_subtree_match_callback(_unused_ sd_bus_message* m, void* t,
                                        _unused_ sd_bus_error* e)
{
    int r;

    mapper_async_subtree* subtree = t;
    if (subtree->finished)
    {
        return 0;
    }

    r = async_subtree_getpaths(subtree);
    if (r < 0)
    {
        async_subtree_done(r, subtree);
    }

    return 0;
}

static void async_subtree_free_requests(mapper_async_subtree* t)
{
    while (t->requests)
    {
        async_subtree_free_request(t->requests);
    }
}

static void async_subtree_done(int r, mapper_async_subtree* t)
{
    if (t->finished)
    {
        return;
    }

    t->finished = 1;
    t->slot = sd_bus_slot_unref(t->slot);
    async_subtree_free_requests(t);

    if (t->callback)
    {
        t->callback(r, t->userdata);
    }
}

_public_ void mapper_subtree_async_free(mapper_async_subtree* t)
{
    if (!t)
    {
        return;
    }

    /* Cancel the match rule and all outstanding requests such that no
     * callback can access t after it has been freed. */
    t->finished = 1;
    sd_bus_slot_unref(t->slot);
    async_subtree_free_requests(t);
    free(t);
}

_public_ int mapper_subtree_async(sd_bus* conn, sd_event* loop, char* namespace,
                                  char* interface, void (*callback)(int, void*),
                                  void* userdata, mapper_async_subtree** t,
                                  int op)
{
    int r = 0;
    mapper_async_subtree* subtree = NULL;

    subtree = malloc(sizeof(*subtree));
    if (!subtree)
    {
        return -ENOMEM;
    }

    memset(subtree, 0, sizeof(*subtree));
    subtree->conn = conn;
    subtree->loop = loop;
    subtree->namespace = namespace;
    subtree->interface = interface;
    subtree->callback = callback;
    subtree->userdata = userdata;
    subtree->op = op;

    if (subtree->op == MAPPER_OP_REMOVE)
    {
        r = sd_bus_add_match(conn, &subtree->slot, interfaces_removed_match,
                             async_subtree_match_callback, subtree);
        if (r < 0)
        {
            fprintf(stderr, "Error adding match rule: %s\n", strerror(-r));
            goto unref_slot;
        }
    }
    else
    {
        /* Operation not supported */
        r = -EINVAL;
        goto free_subtree;
    }

    r = async_subtree_getpaths(subtree);
    if (r < 0)
    {
        fprintf(stderr, "Error calling method: %s\n", strerror(-r));
        goto unref_slot;
    }

    *t = subtree;

    return 0;

unref_slot:
    async_subtree_free_requests(subtree);
    sd_bus_slot_unref(subtree->slot);
free_subtree:
    free(subtree);

    return r;
}

/* Internal: issues a GetObject call with an explicit interface filter.
 * Pass n_ifaces == 0 and iface == NULL for no filter (all services returned).
 * On success the caller owns *reply and must sd_bus_message_unref() it.
 */
static int do_get_object(sd_bus* conn, const char* obj, int n_ifaces,
                         const char* iface, sd_bus_message** reply)
{
    sd_bus_message* request = NULL;
    int r, retry = 0;

    r = sd_bus_message_new_method_call(
        conn, &request, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject");
    if (r < 0)
    {
        goto exit;
    }

    r = sd_bus_message_append(request, "s", obj);
    if (r < 0)
    {
        goto exit;
    }

    r = sd_bus_message_append(request, "as", n_ifaces, iface);
    if (r < 0)
    {
        goto exit;
    }

    while (true)
    {
        r = sd_bus_call(conn, request, 0, NULL, reply);
        if (r == -EBUSY || r == -ENOBUFS)
        {
            if (retry >= mapper_busy_retries)
                break;

            usleep(mapper_busy_delay_interval_usec * (1 << retry));
            ++retry;
            continue;
        }
        break;
    }

exit:
    sd_bus_message_unref(request);

    return r;
}

_public_ int mapper_get_object(sd_bus* conn, const char* obj,
                               sd_bus_message** reply)
{
    return do_get_object(conn, obj, 0, NULL, reply);
}

static int read_first_service(sd_bus_message* reply, char** service)
{
    int r;
    const char* tmp;

    r = sd_bus_message_enter_container(reply, 0, NULL);
    if (r < 0)
    {
        return r;
    }

    r = sd_bus_message_enter_container(reply, 0, NULL);
    if (r < 0)
    {
        return r;
    }

    r = sd_bus_message_read(reply, "s", &tmp);
    if (r < 0)
    {
        return r;
    }

    *service = strdup(tmp);
    if (*service == NULL)
    {
        r = -ENOMEM;
    }

    return r;
}

_public_ int mapper_get_service(sd_bus* conn, const char* obj, char** service)
{
    sd_bus_message* reply = NULL;
    int r;

    r = mapper_get_object(conn, obj, &reply);
    if (r < 0)
    {
        goto exit;
    }

    r = read_first_service(reply, service);

exit:
    sd_bus_message_unref(reply);

    return r;
}

/*
 * Like mapper_get_service but passes a required interface to GetObject so that
 * only services implementing that interface are considered. This is useful when
 * multiple services are registered for the same path and the caller knows which
 * interface it needs.
 */
_public_ int mapper_get_service_with_iface(
    sd_bus* conn, const char* obj, const char* interface, char** service)
{
    sd_bus_message* reply = NULL;
    int r;

    r = do_get_object(conn, obj, 1, interface, &reply);
    if (r < 0)
    {
        goto exit;
    }

    r = read_first_service(reply, service);

exit:
    sd_bus_message_unref(reply);

    return r;
}
