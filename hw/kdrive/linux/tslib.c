/*
 * TSLIB based touchscreen driver for KDrive
 * Porting to new input API and event queueing by Daniel Stone.
 * Derived from ts.c by Keith Packard
 * Derived from ps2.c by Jim Gettys
 *
 * Copyright © 1999 Keith Packard
 * Copyright © 2000 Compaq Computer Corporation
 * Copyright © 2002 MontaVista Software Inc.
 * Copyright © 2006 Nokia Corporation
 * 
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the authors and/or copyright holders
 * not be used in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  The authors and/or
 * copyright holders make no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * THE AUTHORS AND/OR COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL THE AUTHORS AND/OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_KDRIVE_CONFIG_H
#include <kdrive-config.h>
#endif

#define NEED_EVENTS
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/Xpoll.h>
#include "inputstr.h"
#include "scrnintstr.h"
#include "kdrive.h"
#include <sys/ioctl.h>
#include <tslib.h>
#include <dirent.h>
#include <linux/input.h>

#define test_bit(bit, array) (array[bit / 8] & (1 << (bit % 8)))

struct TslibPrivate {
    int fd;
    int lastx, lasty;
    struct tsdev *tsDev;
    int first_pressure;
    void (*raw_event_hook)(int x, int y, int pressure, void *closure);
    void *raw_event_closure;
    int phys_screen;
};

static int
TsRead (int fd, void *closure)
{
    KdPointerInfo       *pi = closure;
    struct TslibPrivate *private = pi->driverPrivate;
    struct ts_sample    events[16];
    int                 i, num_events;
    long                x = 0, y = 0, z = 0;
    unsigned long       flags = 0;
    int                 discard = 0;

    if (!private->tsDev) {
        DebugF("[tslib] EXTREME BADNESS: TsRead called while tsDev is null!\n");
        return 0;
    }

    if (private->raw_event_hook) {
        while ((num_events = ts_read_raw(private->tsDev, events, 1)) == 1) {
            DebugF("[tslib] %d raw events stolen\n", num_events);
            for (i = 0; i < num_events; i++) {
                (*private->raw_event_hook)(events[i].x, events[i].y,
                                           events[i].pressure,
                                           private->raw_event_closure);
            }
        }
        return 0;
    }

    while ((num_events = ts_read(private->tsDev, events, 1)) == 1) {
        for (i = 0; i < num_events; i++) {
            DebugF("[tslib] originally from (%d, %d, %d)\n", events[i].x,
                   events[i].y, events[i].pressure);
            discard = 0;
            if (events[i].pressure) {
                    flags = KD_BUTTON_1;

                if (!private->first_pressure)
                    private->first_pressure = events[i].pressure;

                /* 
                 * Here we test for the touch screen driver actually being on the
                 * touch screen, if it is we send absolute coordinates. If not,
                 * then we send delta's so that we can track the entire vga screen.
                 */
                if (KdCurScreen == private->phys_screen) {
                    x = events[i].x;
                    y = events[i].y;
                } else {
                    if (private->lastx == 0 || private->lasty == 0) {
                        x = events[i].x;
                        y = events[i].y;
                    }
                    else {
                        x = private->lastx + events[i].x;
                        y = private->lasty + events[i].y;
    	    	    }
                }
                z = events[i].pressure;
            } else {
                flags = 0;
                x = private->lastx;
                y = private->lasty;
                z = private->first_pressure;
                private->first_pressure = 0;
            }

            private->lastx = x;
            private->lasty = y;
            KdEnqueuePointerEvent (pi, flags, x, y, z);
        }
    }

    return 0;
}

static Status
TslibEnable (KdPointerInfo *pi)
{
    struct TslibPrivate *private = pi->driverPrivate;
    AxisInfoPtr axis = pi->common.dixdev->valuator->axes + 2;

    /* theoretically belongs in TslibInit, but InitPointerDeviceStruct
     * will trash this.  le sigh. */
    axis->min_value = 0;
    axis->max_value = 255;
    axis->min_resolution = 1;
    axis->max_resolution = 1;

    private->raw_event_hook = NULL;
    private->raw_event_closure = NULL;
    private->tsDev = ts_open(pi->common.path, 0);
    private->fd = ts_fd(private->tsDev);
    if (!private->tsDev || ts_config(private->tsDev) || private->fd < 0) {
        ErrorF("[tslib/TslibEnable] failed to open %s\n", pi->common.path);
        if (private->fd > 0);
            close(private->fd);
        return BadAlloc;
    }
    private->first_pressure = 0;

    DebugF("[tslib/TslibEnable] successfully enabled %s\n", pi->common.path);

    KdRegisterFd(private->fd, TsRead, pi);
  
    return Success;
}


static void
TslibDisable (KdPointerInfo *pi)
{
    struct TslibPrivate *private = pi->driverPrivate;

    if (private->fd)
        KdUnregisterFd(pi, private->fd, TRUE);

    if (private->tsDev)
        ts_close(private->tsDev);

    private->fd = 0;
    private->tsDev = NULL;
}


static Status
TslibInit (KdPointerInfo *pi)
{
    int		        fd = 0;
    DIR                 *inputdir = NULL;
    struct dirent       *inputent = NULL;
    struct TslibPrivate *private = NULL;
    char devpath[PATH_MAX], devname[64];
    CARD8 bits[(EV_MAX / 8) + 1];

    if (!pi || !pi->common.dixdev)
        return !Success;
    
    if (!pi->common.path || strcmp(pi->common.path, "auto") == 0) {
        if (!(inputdir = opendir("/dev/input"))) {
            ErrorF("[tslib/TslibInit]: couldn't open /dev/input!\n");
            return BadMatch;
        }

        while ((inputent = readdir(inputdir))) {
            if (strncmp(inputent->d_name, "event", 5) != 0)
                continue;

            snprintf(devpath, PATH_MAX, "/dev/input/%s", inputent->d_name);
            fd = open(devpath, O_RDWR);

            if (!ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits)) {
                close(fd);
                continue;
            }
            if (!ioctl(fd, EVIOCGNAME(sizeof(devname)), devname)) {
                close(fd);
                continue;
            }
            close(fd);

            /* If x, y and pressure are absolute axes, then it's a
             * touchscreen. */
            if (test_bit(ABS_X, bits) && test_bit(ABS_Y, bits) &&
                test_bit(ABS_PRESSURE, bits)) {
                pi->common.path = KdSaveString(devpath);
                pi->common.name = KdSaveString(devname);
                break;
            }
        }
                
        closedir(inputdir);
    }

    if (!pi->common.path || strcmp(pi->common.path, "auto") == 0) {
        ErrorF("[tslib/TslibInit]: couldn't find device!\n");
        return BadMatch;
    }

    pi->driverPrivate = (struct TslibPrivate *)
                        xcalloc(sizeof(struct TslibPrivate), 1);
    if (!pi->driverPrivate)
        return !Success;

    private = pi->driverPrivate;
    /* hacktastic */
    private->phys_screen = 0;
    private->raw_event_hook = NULL;
    private->raw_event_closure = NULL;
    pi->nAxes = 3;
    pi->nButtons = 8;
    pi->common.type = KD_TOUCHSCREEN;

    return Success;
}


static void
TslibFini (KdPointerInfo *pi)
{
    if (pi->driverPrivate) {
        xfree(pi->driverPrivate);
        pi->driverPrivate = NULL;
    }
}

static int
TslibCtrl (KdPointerInfo *pi, int control, void *data)
{
    struct TslibPrivate *private = NULL;
    TSRawEvent *rawevent = data;

    if (!pi || !pi->driverPrivate)
        return BadImplementation;

    private = pi->driverPrivate;

    if (control == DEVICE_RAWEVENT) {
        if (rawevent) {
            private->raw_event_hook = rawevent->hook;
            private->raw_event_closure = rawevent->closure;
        }
        else {
            private->raw_event_hook = NULL;
            private->raw_event_closure = NULL;
        }
        return Success;
    }

    return BadImplementation;
}


KdPointerDriver TsDriver = {
    "tslib",
    TslibInit,
    TslibEnable,
    TslibDisable,
    TslibFini,
    TslibCtrl,
    NULL,
};
