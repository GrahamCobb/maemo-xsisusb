/*
 * Copyright © 2006-2007 Daniel Stone
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders and/or authors
 * not be used in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  The copyright holders
 * and/or authors make no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * THE COPYRIGHT HOLDERS AND/OR AUTHORS DISCLAIM ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS AND/OR AUTHORS BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus.h>
#include <sys/select.h>

#include "config-backends.h"
#include "dix.h"
#include "os.h"

/* How often to attempt reconnecting when we get booted off the bus. */
#define RECONNECT_DELAY (10 * 1000) /* in ms */

struct dbus_core_info {
    int fd;
    DBusConnection *connection;
    OsTimerPtr timer;
    struct config_dbus_core_hook *hooks;
};
static struct dbus_core_info bus_info;

static CARD32 reconnect_timer(OsTimerPtr timer, CARD32 time, pointer arg);

static void
wakeup_handler(pointer data, int err, pointer read_mask)
{
    struct dbus_core_info *info = data;

    if (!info->connection || info->fd == -1)
        return;

    if (FD_ISSET(info->fd, (fd_set *) read_mask)) {
        do {
            dbus_connection_read_write_dispatch(info->connection, 0);
        } while (info->connection &&
                 dbus_connection_get_dispatch_status(info->connection) ==
                  DBUS_DISPATCH_DATA_REMAINS);
    }
}

static void
block_handler(pointer data, struct timeval **tv, pointer read_mask)
{
}

/**
 * Disconnect (if we haven't already been forcefully disconnected), clean up
 * after ourselves, and call all registered disconnect hooks.
 */
static void
teardown(void)
{
    struct config_dbus_core_hook *hook;

    if (bus_info.timer) {
        TimerCancel(bus_info.timer);
        bus_info.timer = NULL;
    }
    
    /* We should really have pre-disconnect hooks and run them here, for
     * completeness.  But then it gets awkward, given that you can't
     * guarantee that they'll be called ... */
    if (bus_info.connection) {
        dbus_connection_close(bus_info.connection);
        dbus_connection_unref(bus_info.connection);
    }

    RemoveBlockAndWakeupHandlers(block_handler, wakeup_handler, &bus_info);

    if (bus_info.fd != -1)
        RemoveGeneralSocket(bus_info.fd);
    bus_info.fd = -1;
    bus_info.connection = NULL;

    for (hook = bus_info.hooks; hook; hook = hook->next) {
        if (hook->disconnect)
            hook->disconnect(hook->data);
    }
}

/**
 * This is a filter, which only handles the disconnected signal, which
 * doesn't go to the normal message handling function.  This takes
 * precedence over the message handling function, so have have to be
 * careful to ignore anything we don't want to deal with here.
 */
static DBusHandlerResult
message_filter(DBusConnection *connection, DBusMessage *message, void *data)
{
    /* If we get disconnected, then take everything down, and attempt to
     * reconnect immediately (assuming it's just a restart).  The
     * connection isn't valid at this point, so throw it out immediately. */
    if (dbus_message_is_signal(message, DBUS_INTERFACE_LOCAL,
                                    "Disconnected")) {
        DebugF("[config/dbus-core] disconnected from bus\n");
        bus_info.connection = NULL;
        teardown();

        bus_info.timer = TimerSet(NULL, 0, 1, reconnect_timer, NULL);

        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
 * Attempt to connect to the system bus, and set a filter to deal with
 * disconnection (see message_filter above).
 *
 * @return 1 on success, 0 on failure.
 */
static int
connect_to_bus(void)
{
    DBusError error;
    struct config_dbus_core_hook *hook;

    dbus_error_init(&error);
    bus_info.connection = dbus_bus_get_private(DBUS_BUS_SYSTEM, &error);
    if (!bus_info.connection || dbus_error_is_set(&error)) {
        DebugF("[config/dbus-core] error connecting to system bus: %s (%s)\n",
               error.name, error.message);
        goto err_begin;
    }

    /* Thankyou.  Really, thankyou. */
    dbus_connection_set_exit_on_disconnect(bus_info.connection, FALSE);

    if (!dbus_connection_get_unix_fd(bus_info.connection, &bus_info.fd)) {
        ErrorF("[config/dbus-core] couldn't get fd for system bus\n");
        goto err_unref;
    }

    if (!dbus_connection_add_filter(bus_info.connection, message_filter,
                                    &bus_info, NULL)) {
        ErrorF("[config/dbus-core] couldn't add filter: %s (%s)\n", error.name,
               error.message);
        goto err_fd;
    }

    dbus_error_free(&error);
    AddGeneralSocket(bus_info.fd);

    RegisterBlockAndWakeupHandlers(block_handler, wakeup_handler, &bus_info);

    for (hook = bus_info.hooks; hook; hook = hook->next) {
        if (hook->connect)
            hook->connect(bus_info.connection, hook->data);
    }

    return 1;

err_fd:
    bus_info.fd = -1;
err_unref:
    dbus_connection_close(bus_info.connection);
    dbus_connection_unref(bus_info.connection);
    bus_info.connection = NULL;
err_begin:
    dbus_error_free(&error);

    return 0;
}

static CARD32
reconnect_timer(OsTimerPtr timer, CARD32 time, pointer arg)
{
    if (connect_to_bus()) {
        bus_info.timer = NULL;
        return 0;
    }
    else {
        return RECONNECT_DELAY;
    }
}

int
config_dbus_core_add_hook(struct config_dbus_core_hook *hook)
{
    struct config_dbus_core_hook **prev;

    for (prev = &bus_info.hooks; *prev; prev = &(*prev)->next)
        ;
    *prev = hook;
    hook->next = NULL;

    /* If we're already connected, call the connect hook. */
    if (bus_info.connection)
        hook->connect(bus_info.connection, hook->data);

    return 1;
}

void
config_dbus_core_remove_hook(struct config_dbus_core_hook *hook)
{
    struct config_dbus_core_hook **prev;

    for (prev = &bus_info.hooks; *prev; prev = &(*prev)->next) {
        if (*prev == hook) {
            *prev = hook->next;
            break;
        }
    }
}

int
config_dbus_core_init(void)
{
    memset(&bus_info, 0, sizeof(bus_info));
    bus_info.fd = -1;
    bus_info.hooks = NULL;
    bus_info.connection = NULL;
    bus_info.timer = TimerSet(NULL, 0, 1, reconnect_timer, NULL);

    return 1;
}

void
config_dbus_core_fini(void)
{
    teardown();
}
