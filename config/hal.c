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

#include <dbus/dbus.h>
#include <hal/libhal.h>
#include <string.h>
#include <sys/select.h>

#include "input.h"
#include "inputstr.h"
#include "hotplug.h"
#include "config-backends.h"
#include "os.h"

#define TYPE_NONE 0
#define TYPE_KEYS 1
#define TYPE_POINTER 2

struct config_hal_info {
    DBusConnection *system_bus;
    LibHalContext *hal_ctx;
};

static void
remove_device(DeviceIntPtr dev)
{
    DebugF("[config/hal] removing device %s\n", dev->name);

    /* Call PIE here so we don't try to dereference a device that's
     * already been removed. */
    OsBlockSignals();
    ProcessInputEvents();
    DeleteInputDeviceRequest(dev);
    OsReleaseSignals();
}

static void
device_removed(LibHalContext *ctx, const char *udi)
{
    DeviceIntPtr dev, next;
    struct xserver_option *option;

    for (dev = inputInfo.devices; dev; dev = next) {
        next = dev->next;
        for (option = dev->options; option; option = option->next) {
            if (strcmp(option->key, "hal_udi") == 0 &&
                strcmp(option->value, udi) == 0)
                remove_device(dev);
        }
    }
    for (dev = inputInfo.off_devices; dev; dev = next) {
        next = dev->next;
        for (option = dev->options; option; option = option->next) {
            if (strcmp(option->key, "hal_udi") == 0 &&
                strcmp(option->value, udi) == 0)
                remove_device(dev);
        }
    }
}

static void
add_option(struct xserver_option **options, const char *key, const char *value)
{
    if (!value || *value == '\0')
        return;

    for (; *options; options = &(*options)->next)
        ;
    *options = xcalloc(sizeof(**options), 1);
    (*options)->key = xstrdup(key);
    (*options)->value = xstrdup(value);
    (*options)->next = NULL;
}

static char *
get_prop_string(LibHalContext *hal_ctx, const char *udi, const char *name)
{
    char *prop, *ret;

    prop = libhal_device_get_property_string(hal_ctx, udi, name, NULL);
    DebugF(" [config/hal] getting %s on %s returned %s\n", name, udi, prop);
    if (prop) {
        ret = xstrdup(prop);
        libhal_free_string(prop);
    }
    else {
        return NULL;
    }

    return ret;
}

static char **
get_prop_string_array(LibHalContext *hal_ctx, const char *udi, const char *prop)
{
    char **props, **ret;
    int i;

    props = libhal_device_get_property_strlist(hal_ctx, udi, prop, NULL);
    if (props) {
        for (i = 0; props[i]; i++)
            ;

        ret = xcalloc(sizeof(*ret), i + 1);
        if (!ret) {
            libhal_free_string_array(props);
            return NULL;
        }

        for (i = 0; props[i]; i++)
            ret[i] = xstrdup(props[i]);

        libhal_free_string_array(props);
    }
    else {
        return NULL;
    }

    return ret;
}

static void
device_added(LibHalContext *hal_ctx, const char *udi)
{
    char **props;
    /* Cleverly, these are currently all leaked. */
    char *path = NULL, *driver = NULL, *name = NULL, *xkb_rules = NULL;
    char *xkb_model = NULL, *xkb_layout = NULL, *xkb_variant = NULL;
    char **xkb_options = NULL;
    DBusError error;
    struct xserver_option *options = NULL;
    int type = TYPE_NONE;
    int i;
    DeviceIntPtr dev = NULL;

    dbus_error_init(&error);

    props = libhal_device_get_property_strlist(hal_ctx, udi,
                                               "info.capabilities", &error);
    if (!props) {
        DebugF("[config/hal] couldn't get capabilities for %s: %s (%s)\n",
               udi, error.name, error.message);
        goto out_error;
    }
    for (i = 0; props[i]; i++) {
        /* input.keys is the new, of which input.keyboard is a subset, but
         * input.keyboard is the old 'we have keys', so we have to keep it
         * around. */
        if (strcmp(props[i], "input.keys") == 0 ||
            strcmp(props[i], "input.keyboard") == 0)
            type |= TYPE_KEYS;
        if (strcmp(props[i], "input.mouse") == 0)
            type |= TYPE_POINTER;
    }
    libhal_free_string_array(props);

    if (!type)
        goto out_error;

    driver = get_prop_string(hal_ctx, udi, "input.x11_driver");
    path = get_prop_string(hal_ctx, udi, "input.device");
    if (!driver || !path) {
        DebugF("[config/hal] no driver or path specified for %s\n", udi);
        goto unwind;
    }
    name = get_prop_string(hal_ctx, udi, "info.product");
    if (!name)
        name = xstrdup("(unnamed)");

    if (type & TYPE_KEYS) {
        xkb_rules = get_prop_string(hal_ctx, udi, "input.xkb.rules");
        xkb_model = get_prop_string(hal_ctx, udi, "input.xkb.model");
        xkb_layout = get_prop_string(hal_ctx, udi, "input.xkb.layout");
        xkb_variant = get_prop_string(hal_ctx, udi, "input.xkb.variant");
        xkb_options = get_prop_string_array(hal_ctx, udi, "input.xkb.options");
    }

    options = xcalloc(sizeof(*options), 1);
    options->key = xstrdup("_source");
    options->value = xstrdup("server/hal");
    if (!options->key || !options->value) {
        ErrorF("[config] couldn't allocate first key/value pair\n");
        goto unwind;
    }

    add_option(&options, "path", path);
    add_option(&options, "driver", driver);
    add_option(&options, "name", name);
    add_option(&options, "hal_udi", udi);

    if (xkb_model)
        add_option(&options, "xkb_model", xkb_model);
    if (xkb_layout)
        add_option(&options, "xkb_layout", xkb_layout);
    if (xkb_variant)
        add_option(&options, "xkb_variant", xkb_variant);
#if 0
    if (xkb_options)
        add_option(&options, "xkb_options", xkb_options);
#endif

    /* Maemo-specific hack.  Ugh. */
    if (type & TYPE_KEYS)
        add_option(&options, "type", "keyboard");
    else
        add_option(&options, "type", "pointer");

    if (NewInputDeviceRequest(options, &dev) != Success) {
        DebugF("[config/hal] NewInputDeviceRequest failed\n");
        goto unwind;
    }

    dbus_error_free(&error);

    return;

unwind:
    if (path)
        xfree(path);
    if (driver)
        xfree(driver);
    if (name)
        xfree(name);
    if (xkb_rules)
        xfree(xkb_rules);
    if (xkb_model)
        xfree(xkb_model);
    if (xkb_layout)
        xfree(xkb_layout);
    if (xkb_options) {
        for (i = 0; xkb_options[i]; i++)
            xfree(xkb_options[i]);
        xfree(xkb_options);
    }

out_error:
    dbus_error_free(&error);

    return;
}

static void
disconnect_hook(void *data)
{
    DBusError error;
    struct config_hal_info *info = data;

    if (info->hal_ctx) {
        dbus_error_init(&error);
        if (!libhal_ctx_shutdown(info->hal_ctx, &error))
            DebugF("[config/hal] couldn't shut down context: %s (%s)\n",
                   error.name, error.message);
        libhal_ctx_free(info->hal_ctx);
        dbus_error_free(&error);
    }

    info->hal_ctx = NULL;
    info->system_bus = NULL;
}

static void
connect_hook(DBusConnection *connection, void *data)
{
    DBusError error;
    struct config_hal_info *info = data;
    char **devices;
    int num_devices, i;

    info->system_bus = connection;

    dbus_error_init(&error);

    if (!info->hal_ctx)
        info->hal_ctx = libhal_ctx_new();
    if (!info->hal_ctx) {
        ErrorF("[config/hal] couldn't create HAL context\n");
        goto out_err;
    }

    if (!libhal_ctx_set_dbus_connection(info->hal_ctx, info->system_bus)) {
        ErrorF("[config/hal] couldn't associate HAL context with bus\n");
        goto out_ctx;
    }
    if (!libhal_ctx_init(info->hal_ctx, &error)) {
        ErrorF("[config/hal] couldn't initialise context: %s (%s)\n",
               error.name, error.message);
        goto out_ctx;
    }

    libhal_ctx_set_device_added(info->hal_ctx, device_added);
    libhal_ctx_set_device_removed(info->hal_ctx, device_removed);
    if (!libhal_device_property_watch_all(info->hal_ctx, &error)) {
        ErrorF("[config/hal] couldn't watch all properties: %s (%s)\n",
               error.name, error.message);
        goto out_ctx2;
    }

    devices = libhal_find_device_by_capability(info->hal_ctx, "input",
                                               &num_devices, &error);
    /* FIXME: Get default devices if error is set. */
    for (i = 0; i < num_devices; i++)
        device_added(info->hal_ctx, devices[i]);
    libhal_free_string_array(devices);

    dbus_error_free(&error);

    return;
    
out_ctx2:
    if (!libhal_ctx_shutdown(info->hal_ctx, &error))
        DebugF("[config/hal] couldn't shut down context: %s (%s)\n",
               error.name, error.message);
out_ctx:
    libhal_ctx_free(info->hal_ctx);
out_err:
    dbus_error_free(&error);

    info->hal_ctx = NULL;
    info->system_bus = NULL;

    return;
}

static struct config_hal_info hal_info;
static struct config_dbus_core_hook hook = {
    .connect = connect_hook,
    .disconnect = disconnect_hook,
    .data = &hal_info,
};

int
config_hal_init(void)
{
    memset(&hal_info, 0, sizeof(hal_info));
    hal_info.system_bus = NULL;
    hal_info.hal_ctx = NULL;

    if (!config_dbus_core_add_hook(&hook)) {
        ErrorF("[config/hal] failed to add D-Bus hook\n");
        return 0;
    }
    
    return 1;
}

void
config_hal_fini(void)
{
    config_dbus_core_remove_hook(&hook);
}
