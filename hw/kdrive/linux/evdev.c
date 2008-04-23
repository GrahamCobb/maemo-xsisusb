/*
 * Copyright © 2007 Nokia Corporation
 *
 * Permission to use, copy, modify, distribute and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of the authors and/or copyright holders
 * not be used in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  The authors and
 * copyright holders make no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without any express
 * or implied warranty.
 *
 * THE AUTHORS AND COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: Daniel Stone <daniel@fooishbar.org>
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include <errno.h>
#include <linux/input.h>

#define NEED_EVENTS
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/Xpoll.h>
#include <X11/keysym.h>

#include "inputstr.h"
#include "scrnintstr.h"
#include "kdrive.h"

static KeySym evdev_kbd_map[] = {
    /* 0x00 */  NoSymbol,       NoSymbol,
    /* 0x01 */  XK_Escape,      NoSymbol,
    /* 0x02 */  XK_1,           XK_exclam,
    /* 0x03 */  XK_2,           XK_at,
    /* 0x04 */  XK_3,           XK_numbersign,
    /* 0x05 */  XK_4,           XK_dollar,
    /* 0x06 */  XK_5,           XK_percent,
    /* 0x07 */  XK_6,           XK_asciicircum,
    /* 0x08 */  XK_7,           XK_ampersand,
    /* 0x09 */  XK_8,           XK_asterisk,
    /* 0x0a */  XK_9,           XK_parenleft,
    /* 0x0b */  XK_0,           XK_parenright,
    /* 0x0c */  XK_minus,       XK_underscore,
    /* 0x0d */  XK_equal,       XK_plus,
    /* 0x0e */  XK_BackSpace,   NoSymbol,
    /* 0x0f */  XK_Tab,         XK_ISO_Left_Tab,
    /* 0x10 */  XK_Q,           NoSymbol,
    /* 0x11 */  XK_W,           NoSymbol,
    /* 0x12 */  XK_E,           NoSymbol,
    /* 0x13 */  XK_R,           NoSymbol,
    /* 0x14 */  XK_T,           NoSymbol,
    /* 0x15 */  XK_Y,           NoSymbol,
    /* 0x16 */  XK_U,           NoSymbol,
    /* 0x17 */  XK_I,           NoSymbol,
    /* 0x18 */  XK_O,           NoSymbol,
    /* 0x19 */  XK_P,           NoSymbol,
    /* 0x1a */  XK_bracketleft, XK_braceleft,
    /* 0x1b */  XK_bracketright,XK_braceright,
    /* 0x1c */  XK_Return,      NoSymbol,
    /* 0x1d */  XK_Control_L,   NoSymbol,
    /* 0x1e */  XK_A,           NoSymbol,
    /* 0x1f */  XK_S,           NoSymbol,
    /* 0x20 */  XK_D,           NoSymbol,
    /* 0x21 */  XK_F,           NoSymbol,
    /* 0x22 */  XK_G,           NoSymbol,
    /* 0x23 */  XK_H,           NoSymbol,
    /* 0x24 */  XK_J,           NoSymbol,
    /* 0x25 */  XK_K,           NoSymbol,
    /* 0x26 */  XK_L,           NoSymbol,
    /* 0x27 */  XK_semicolon,   XK_colon,
    /* 0x28 */  XK_quoteright,  XK_quotedbl,
    /* 0x29 */  XK_quoteleft,   XK_asciitilde,
    /* 0x2a */  XK_Shift_L,     NoSymbol,
    /* 0x2b */  XK_backslash,   XK_bar,
    /* 0x2c */  XK_Z,           NoSymbol,
    /* 0x2d */  XK_X,           NoSymbol,
    /* 0x2e */  XK_C,           NoSymbol,
    /* 0x2f */  XK_V,           NoSymbol,
    /* 0x30 */  XK_B,           NoSymbol,
    /* 0x31 */  XK_N,           NoSymbol,
    /* 0x32 */  XK_M,           NoSymbol,
    /* 0x33 */  XK_comma,       XK_less,
    /* 0x34 */  XK_period,      XK_greater,
    /* 0x35 */  XK_slash,       XK_question,
    /* 0x36 */  XK_Shift_R,     NoSymbol,
    /* 0x37 */  XK_KP_Multiply, NoSymbol,
    /* 0x38 */  XK_Alt_L,       XK_Meta_L,
    /* 0x39 */  XK_space,       NoSymbol,
    /* 0x3a */  XK_Caps_Lock,   NoSymbol,
    /* 0x3b */  XK_F1,          NoSymbol,
    /* 0x3c */  XK_F2,          NoSymbol,
    /* 0x3d */  XK_F3,          NoSymbol,
    /* 0x3e */  XK_F4,          NoSymbol,
    /* 0x3f */  XK_F5,          NoSymbol,
    /* 0x40 */  XK_F6,          NoSymbol,
    /* 0x41 */  XK_F7,          NoSymbol,
    /* 0x42 */  XK_F8,          NoSymbol,
    /* 0x43 */  XK_F9,          NoSymbol,
    /* 0x44 */  XK_F10,         NoSymbol,
    /* 0x45 */  XK_Num_Lock,    NoSymbol,
    /* 0x46 */  XK_Scroll_Lock, NoSymbol,
    /* These KP keys should have the KP_7 keysyms in the numlock
     * modifer... ? */
    /* 0x47 */  XK_KP_Home,     XK_KP_7,
    /* 0x48 */  XK_KP_Up,       XK_KP_8,
    /* 0x49 */  XK_KP_Prior,    XK_KP_9,
    /* 0x4a */  XK_KP_Subtract, NoSymbol,
    /* 0x4b */  XK_KP_Left,     XK_KP_4,
    /* 0x4c */  XK_KP_Begin,    XK_KP_5,
    /* 0x4d */  XK_KP_Right,    XK_KP_6,
    /* 0x4e */  XK_KP_Add,      NoSymbol,
    /* 0x4f */  XK_KP_End,      XK_KP_1,
    /* 0x50 */  XK_KP_Down,     XK_KP_2,
    /* 0x51 */  XK_KP_Next,     XK_KP_3,
    /* 0x52 */  XK_KP_Insert,   XK_KP_0,
    /* 0x53 */  XK_KP_Delete,   XK_KP_Decimal,
    /* 0x54 */  NoSymbol,       NoSymbol,
    /* 0x55 */  XK_F13,         NoSymbol,
    /* 0x56 */  XK_less,        XK_greater,
    /* 0x57 */  XK_F11,         NoSymbol,
    /* 0x58 */  XK_F12,         NoSymbol,
    /* 0x59 */  XK_F14,         NoSymbol,
    /* 0x5a */  XK_F15,         NoSymbol,
    /* 0x5b */  XK_F16,         NoSymbol,
    /* 0x5c */  XK_F17,         NoSymbol,
    /* 0x5d */  XK_F18,         NoSymbol,
    /* 0x5e */  XK_F19,         NoSymbol,
    /* 0x5f */  XK_F20,         NoSymbol,
    /* 0x60 */  XK_KP_Enter,    NoSymbol,
    /* 0x61 */  XK_Control_R,   NoSymbol,
    /* 0x62 */  XK_KP_Divide,   NoSymbol,
    /* 0x63 */  XK_Print,       XK_Sys_Req,
    /* 0x64 */  XK_Alt_R,       XK_Meta_R,
    /* 0x65 */  NoSymbol,       NoSymbol, /* KEY_LINEFEED */
    /* 0x66 */  XK_Home,        NoSymbol,
    /* 0x67 */  XK_Up,          NoSymbol,
    /* 0x68 */  XK_Prior,       NoSymbol,
    /* 0x69 */  XK_Left,        NoSymbol,
    /* 0x6a */  XK_Right,       NoSymbol,
    /* 0x6b */  XK_End,         NoSymbol,
    /* 0x6c */  XK_Down,        NoSymbol,
    /* 0x6d */  XK_Next,        NoSymbol,
    /* 0x6e */  XK_Insert,      NoSymbol,
    /* 0x6f */  XK_Delete,      NoSymbol,
    /* 0x6f */  NoSymbol,       NoSymbol, /* KEY_MACRO */
    /* 0x70 */  NoSymbol,       NoSymbol,
    /* 0x71 */  NoSymbol,       NoSymbol,
    /* 0x72 */  NoSymbol,       NoSymbol,
    /* 0x73 */  NoSymbol,       NoSymbol,
    /* 0x74 */  NoSymbol,       NoSymbol,
    /* 0x75 */  XK_KP_Equal,    NoSymbol,
    /* 0x76 */  NoSymbol,       NoSymbol,
    /* 0x77 */  NoSymbol,       NoSymbol,
    /* 0x78 */  XK_F21,         NoSymbol,
    /* 0x79 */  XK_F22,         NoSymbol,
    /* 0x7a */  XK_F23,         NoSymbol,
    /* 0x7b */  XK_F24,         NoSymbol,
    /* 0x7c */  XK_KP_Separator, NoSymbol,
    /* 0x7d */  XK_Meta_L,      NoSymbol,
    /* 0x7e */  XK_Meta_R,      NoSymbol,
    /* 0x7f */  XK_Multi_key,   NoSymbol,
    /* 0x80 */  NoSymbol,       NoSymbol,
    /* 0x81 */  NoSymbol,       NoSymbol,
    /* 0x82 */  NoSymbol,       NoSymbol,
    /* 0x83 */  NoSymbol,       NoSymbol,
    /* 0x84 */  NoSymbol,       NoSymbol,
    /* 0x85 */  NoSymbol,       NoSymbol,
    /* 0x86 */  NoSymbol,       NoSymbol,
    /* 0x87 */  NoSymbol,       NoSymbol,
    /* 0x88 */  NoSymbol,       NoSymbol,
    /* 0x89 */  NoSymbol,       NoSymbol,
    /* 0x8a */  NoSymbol,       NoSymbol,
    /* 0x8b */  NoSymbol,       NoSymbol,
    /* 0x8c */  NoSymbol,       NoSymbol,
    /* 0x8d */  NoSymbol,       NoSymbol,
    /* 0x8e */  NoSymbol,       NoSymbol,
    /* 0x8f */  NoSymbol,       NoSymbol,
    /* 0x90 */  NoSymbol,       NoSymbol,
    /* 0x91 */  NoSymbol,       NoSymbol,
    /* 0x92 */  NoSymbol,       NoSymbol,
    /* 0x93 */  NoSymbol,       NoSymbol,
    /* 0x94 */  NoSymbol,       NoSymbol,
    /* 0x95 */  NoSymbol,       NoSymbol,
    /* 0x96 */  NoSymbol,       NoSymbol,
    /* 0x97 */  NoSymbol,       NoSymbol,
    /* 0x98 */  NoSymbol,       NoSymbol,
    /* 0x99 */  NoSymbol,       NoSymbol,
    /* 0x9a */  NoSymbol,       NoSymbol,
    /* 0x9b */  NoSymbol,       NoSymbol,
    /* 0x9c */  NoSymbol,       NoSymbol,
    /* 0x9d */  NoSymbol,       NoSymbol,
    /* 0x9e */  NoSymbol,       NoSymbol,
    /* 0x9f */  NoSymbol,       NoSymbol,
    /* 0xa0 */  NoSymbol,       NoSymbol,
    /* 0xa1 */  NoSymbol,       NoSymbol,
    /* 0xa2 */  NoSymbol,       NoSymbol,
    /* 0xa3 */  NoSymbol,       NoSymbol,
    /* 0xa4 */  NoSymbol,       NoSymbol,
    /* 0xa5 */  NoSymbol,       NoSymbol,
    /* 0xa6 */  NoSymbol,       NoSymbol,
    /* 0xa7 */  NoSymbol,       NoSymbol,
    /* 0xa8 */  NoSymbol,       NoSymbol,
    /* 0xa9 */  NoSymbol,       NoSymbol,
    /* 0xaa */  NoSymbol,       NoSymbol,
    /* 0xab */  NoSymbol,       NoSymbol,
    /* 0xac */  NoSymbol,       NoSymbol,
    /* 0xad */  NoSymbol,       NoSymbol,
    /* 0xae */  NoSymbol,       NoSymbol,
    /* 0xaf */  NoSymbol,       NoSymbol,
    /* 0xb0 */  NoSymbol,       NoSymbol,
    /* 0xb1 */  NoSymbol,       NoSymbol,
    /* 0xb2 */  NoSymbol,       NoSymbol,
    /* 0xb3 */  NoSymbol,       NoSymbol,
    /* 0xb4 */  NoSymbol,       NoSymbol,
    /* 0xb5 */  NoSymbol,       NoSymbol,
    /* 0xb6 */  NoSymbol,       NoSymbol,
    /* 0xb7 */  NoSymbol,       NoSymbol,
    /* 0xb8 */  NoSymbol,       NoSymbol,
    /* 0xb9 */  NoSymbol,       NoSymbol,
    /* 0xba */  NoSymbol,       NoSymbol,
    /* 0xbb */  NoSymbol,       NoSymbol,
    /* 0xbc */  NoSymbol,       NoSymbol,
    /* 0xbd */  NoSymbol,       NoSymbol,
    /* 0xbe */  NoSymbol,       NoSymbol,
    /* 0xbf */  NoSymbol,       NoSymbol,
    /* 0xc0 */  NoSymbol,       NoSymbol,
    /* 0xc1 */  NoSymbol,       NoSymbol,
    /* 0xc2 */  NoSymbol,       NoSymbol,
    /* 0xc3 */  NoSymbol,       NoSymbol,
    /* 0xc4 */  NoSymbol,       NoSymbol,
    /* 0xc5 */  NoSymbol,       NoSymbol,
    /* 0xc6 */  NoSymbol,       NoSymbol,
    /* 0xc7 */  NoSymbol,       NoSymbol,
    /* 0xc8 */  NoSymbol,       NoSymbol,
    /* 0xc9 */  NoSymbol,       NoSymbol,
    /* 0xca */  NoSymbol,       NoSymbol,
    /* 0xcb */  NoSymbol,       NoSymbol,
    /* 0xcc */  NoSymbol,       NoSymbol,
    /* 0xcd */  NoSymbol,       NoSymbol,
    /* 0xce */  NoSymbol,       NoSymbol,
    /* 0xcf */  NoSymbol,       NoSymbol,
    /* 0xd0 */  NoSymbol,       NoSymbol,
    /* 0xd1 */  NoSymbol,       NoSymbol,
    /* 0xd2 */  NoSymbol,       NoSymbol,
    /* 0xd3 */  NoSymbol,       NoSymbol,
    /* 0xd4 */  NoSymbol,       NoSymbol,
    /* 0xd5 */  NoSymbol,       NoSymbol,
    /* 0xd6 */  NoSymbol,       NoSymbol,
    /* 0xd7 */  NoSymbol,       NoSymbol,
    /* 0xd8 */  NoSymbol,       NoSymbol,
    /* 0xd9 */  NoSymbol,       NoSymbol,
    /* 0xda */  NoSymbol,       NoSymbol,
    /* 0xdb */  NoSymbol,       NoSymbol,
    /* 0xdc */  NoSymbol,       NoSymbol,
    /* 0xdd */  NoSymbol,       NoSymbol,
    /* 0xde */  NoSymbol,       NoSymbol,
    /* 0xdf */  NoSymbol,       NoSymbol,
    /* 0xe0 */  NoSymbol,       NoSymbol,
    /* 0xe1 */  NoSymbol,       NoSymbol,
    /* 0xe2 */  NoSymbol,       NoSymbol,
    /* 0xe3 */  NoSymbol,       NoSymbol,
    /* 0xe4 */  NoSymbol,       NoSymbol,
    /* 0xe5 */  NoSymbol,       NoSymbol,
    /* 0xe6 */  NoSymbol,       NoSymbol,
    /* 0xe7 */  NoSymbol,       NoSymbol,
    /* 0xe8 */  NoSymbol,       NoSymbol,
    /* 0xe9 */  NoSymbol,       NoSymbol,
    /* 0xea */  NoSymbol,       NoSymbol,
    /* 0xeb */  NoSymbol,       NoSymbol,
    /* 0xec */  NoSymbol,       NoSymbol,
    /* 0xed */  NoSymbol,       NoSymbol,
    /* 0xee */  NoSymbol,       NoSymbol,
    /* 0xef */  NoSymbol,       NoSymbol,
    /* 0xf0 */  NoSymbol,       NoSymbol,
    /* 0xf1 */  NoSymbol,       NoSymbol,
    /* 0xf2 */  NoSymbol,       NoSymbol,
    /* 0xf3 */  NoSymbol,       NoSymbol,
    /* 0xf4 */  NoSymbol,       NoSymbol,
    /* 0xf5 */  NoSymbol,       NoSymbol,
    /* 0xf6 */  NoSymbol,       NoSymbol,
    /* 0xf7 */  NoSymbol,       NoSymbol,
};  

enum evdev_type {
    Keyboard,
    Pointer,
};

#define NUM_EVENTS  128

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define ISBITSET(x,y) ((x)[LONG(y)] & BIT(y))
#define OFF(x)   ((x)%BITS_PER_LONG)
#define LONG(x)  ((x)/BITS_PER_LONG)
#define BIT(x)         (1 << OFF(x))
#define SETBIT(x,y) ((x)[LONG(y)] |= BIT(y))
#define CLRBIT(x,y) ((x)[LONG(y)] &= ~BIT(y))
#define ASSIGNBIT(x,y,z)    ((x)[LONG(y)] = ((x)[LONG(y)] & ~BIT(y)) | (z << OFF(y)))

struct evdev_device_info {
    int max_rel;
    int max_abs;

    unsigned long key_bits[NBITS(KEY_MAX)];
    unsigned long rel_bits[NBITS(REL_MAX)];
    unsigned long abs_bits[NBITS(EV_MAX)];

    enum evdev_type type;
    void *device;
    char *path;
    int fd;
};

#if 0
static void
evdev_pointer_check_motion(KdPointerInfo *pi)
{
    Kevdev *ke = pi->driverPrivate;
    int i;

    for (i = 0; i <= ke->max_rel; i++) {
        if (ke->rel[i]) {
            int a;
            ErrorF("rel");
            for (a = 0; a <= ke->max_rel; a++) {
                if (ISBITSET(ke->relbits, a))
                    ErrorF(" %d=%d", a, ke->rel[a]);
                ke->rel[a] = 0;
            }
            ErrorF("\n");
            break;
        }
    }

    for (i = 0; i < ke->max_abs; i++) {
        if (ke->abs[i] != ke->prevabs[i]) {
            int a;
            ErrorF("abs");
            for (a = 0; a <= ke->max_abs; a++) {
                if (ISBITSET(ke->absbits, a))
                    ErrorF(" %d=%d", a, ke->abs[a]);
                ke->prevabs[a] = ke->abs[a];
            }
            ErrorF("\n");
            break;
        }
    }
}
#endif

static int
evdev_device_read(int evdevPort, void *closure)
{
    struct evdev_device_info *info = closure;
    struct input_event events[NUM_EVENTS];
    int i, n;

    n = read(evdevPort, &events, NUM_EVENTS * sizeof(struct input_event));
    if (n <= 0) {
        if (n == 0 || errno == -EAGAIN || errno == -EINTR)
            return 0;
        else
            return errno;
    }
    n /= sizeof(struct input_event);

    for (i = 0; i < n; i++) {

        switch (events[i].type) {
        case EV_SYN:
            break;
        case EV_KEY:
            if (info->type == Keyboard)
                KdEnqueueKeyboardEvent(info->device, events[i].code,
                                       !events[i].value);
            break;
#if 0
        case EV_REL:
            ke->rel[events[i].code] += events[i].value;
            break;
        case EV_ABS:
            ke->abs[events[i].code] = events[i].value;
            break;
#endif
        }
    }

    return 0;
}

static char *
get_name_for_type(struct evdev_device_info *info)
{
    switch (info->type) {
    case Keyboard:
        return "keyboard";
    case Pointer:
        return "pointer";
    }
}

static Status
evdev_device_init(struct evdev_device_info *info)
{
    int fd;
    char devname[256];

    fd = open(info->path, O_RDWR);
    if (fd < 0) {
        ErrorF("[evdev] failed to open device %s\n", info->path);
        return BadMatch;
    }

#if 0
    if (ioctl(fd, EVIOCGNAME(sizeof(devname)), devname) < 0)
        info->name = xstrdup(devname);
    else
        info->name = xstrdup("Generic %s", get_name_for_type(info));
#endif

    close(fd);

    return Success;
}

static Status
evdev_device_enable(struct evdev_device_info *info)
{
    int fd;
    unsigned long ev_bits[NBITS(EV_MAX)];

    fd = open(info->path, O_RDWR);
    if (fd < 0) {
        ErrorF("[evdev] couldn't open %s\n", info->path);
        return BadMatch;
    }

    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0) {
        ErrorF("[evdev] couldn't get device capabilities\n");
        goto unwind;
    }

    switch (info->type) {
    case Keyboard:
        if (ISBITSET(ev_bits, EV_KEY)) {
            if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(info->key_bits)),
                      info->key_bits) < 0) {
                ErrorF("[evdev] keyboard, isn't\n");
                goto unwind;
            }
        }

        break;

    case Pointer:
        if (ISBITSET(ev_bits, EV_REL)) {
            if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(info->rel_bits)),
                      info->rel_bits) >= 0) {
                for (info->max_rel = REL_MAX; info->max_rel >= 0; info->max_rel--)
                    if (ISBITSET(info->rel_bits, info->max_rel))
                        break;
            }
        }

#if 0 /* Absolute support. */
        if (ISBITSET(ev, EV_ABS)) {
            if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(ke->absbits)),
                                    ke->absbits) >= 0) {
            for (ke->max_abs = ABS_MAX; ke->max_abs >= 0; ke->max_abs--)
                if (ISBITSET(ke->absbits, ke->max_abs))
                    break;

            for (i = 0; i <= ke->max_abs; i++) {
                if (ISBITSET(ke->absbits, i)) {
                    if (ioctl(fd, EVIOCGABS(i), &ke->absinfo[i]) < 0) {
                        perror("EVIOCGABS");
                        break;
                    }
                }

                ke->prevabs[i] = ABS_UNSET;
            }

            if (i <= ke->max_abs) {
                xfree(ke);
                close(fd);
                return BadValue;
            }
        }
#endif

        break;
    }

    if (ioctl(fd, EVIOCGRAB, 2) < 0) {
        ErrorF("[evdev] couldn't grab device\n");
        goto unwind;
    }

    if (!KdRegisterFd(fd, evdev_device_read, info))
        goto unwind;

    info->fd = fd;

    return Success;

unwind:
    close(fd);
    return BadMatch;
}

static void
evdev_device_disable(struct evdev_device_info *info)
{
    ioctl(info->fd, EVIOCGRAB, 0);
    KdUnregisterFd(info, info->fd, TRUE);
}

static void
evdev_device_fini(struct evdev_device_info *info)
{
}

static Status
evdev_pointer_init(KdPointerInfo *pi)
{
    struct evdev_device_info *info;

    info = xcalloc(sizeof(*info), 1);
    if (!info)
        return BadAlloc;
    info->type = Pointer;
    info->path = pi->common.path;
    info->device = pi;
    pi->driverPrivate = info;

    return evdev_device_init(info);
}

static Status
evdev_pointer_enable(KdPointerInfo *pi)
{        
    struct evdev_device_info *info = pi->driverPrivate;

    return evdev_device_enable(info);
}

static void
evdev_pointer_disable(KdPointerInfo *pi)
{
    struct evdev_device_info *info = pi->driverPrivate;

    evdev_device_disable(info);
}

static void
evdev_pointer_fini(KdPointerInfo *pi)
{
    struct evdev_device_info *info = pi->driverPrivate;

    evdev_device_fini(info);
    xfree(info);
    pi->driverPrivate = NULL;
}

static Status
evdev_keyboard_init(KdKeyboardInfo *ki)
{
    struct evdev_device_info *info;

    info = xcalloc(sizeof(*info), 1);
    if (!info)
        return BadAlloc;
    info->type = Keyboard;
    info->path = ki->common.path;
    info->device = ki;
    ki->driverPrivate = info;

    ki->minScanCode = 0;
    ki->maxScanCode = 255;
    ki->keySyms.minKeyCode = 8;
    ki->keySyms.maxKeyCode = 255;
    ki->keySyms.mapWidth = 2;
    memcpy(ki->keySyms.map, evdev_kbd_map, sizeof(evdev_kbd_map));

    return evdev_device_init(info);
}

static Status
evdev_keyboard_enable(KdKeyboardInfo *ki)
{
    struct evdev_device_info *info = ki->driverPrivate;

    return evdev_device_enable(info);
}

static void
evdev_keyboard_disable(KdKeyboardInfo *ki)
{
    struct evdev_device_info *info = ki->driverPrivate;

    evdev_device_disable(info);
}

static void
evdev_keyboard_fini(KdKeyboardInfo *ki)
{
    struct evdev_device_info *info = ki->driverPrivate;

    evdev_device_fini(info);
    xfree(info);
    ki->driverPrivate = NULL;
}

KdPointerDriver LinuxEvdevMouseDriver = {
    .name = "evdev",
    .Init = evdev_pointer_init,
    .Enable = evdev_pointer_enable,
    .Disable = evdev_pointer_disable,
    .Fini = evdev_pointer_fini,
};

KdKeyboardDriver LinuxEvdevKeyboardDriver = {
    .name = "evdev",
    .Init = evdev_keyboard_init,
    .Enable = evdev_keyboard_enable,
#if 0
    .Leds = evdev_keyboard_leds,
#endif
    .Disable = evdev_keyboard_disable,
    .Fini = evdev_keyboard_fini,
};
