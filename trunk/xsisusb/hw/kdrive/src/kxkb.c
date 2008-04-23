/*
 * Copyright © 2006 Nokia Corporation
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
 * Author: Daniel Stone <daniel.stone@nokia.com>
 */

#ifdef HAVE_KDRIVE_CONFIG_H
#include <kdrive-config.h>
#endif

#ifdef HAVE_XKB_CONFIG_H
#include <xkb-config.h>
#endif

#include "inputstr.h"
#include "kdrive.h"

int
XkbDDXTerminateServer(DeviceIntPtr dev, KeyCode key, XkbAction *act)
{
    if (!kdDontZap)
        KdExit();

    return 0;
}

int
XkbDDXSwitchScreen(DeviceIntPtr dev, KeyCode key, XkbAction *act)
{
    int vtnum = XkbSAScreen(&act->screen);

    if (!kdOsFuncs->SwitchVT)
        return 1;

    if (act->screen.flags & XkbSA_SwitchApplication) {
        if (act->screen.flags & XkbSA_SwitchAbsolute)
            (*kdOsFuncs->SwitchVT) (KD_VT_ABSOLUTE, vtnum);
        else
            (*kdOsFuncs->SwitchVT) (KD_VT_RELATIVE, vtnum);
    }

    return 0;
}

int
XkbDDXPrivate(DeviceIntPtr dev, KeyCode key, XkbAction *act)
{
    return 0;
}
