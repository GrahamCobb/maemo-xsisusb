/*
 * Copyright © 2004-2006 Nokia Corporation
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

#include "omap.h"
#include "kdrive.h"

void
InitCard (char *name)
{   
    KdCardAttr  attr;

    ENTER();
    KdCardInfoAdd(&omap_funcs, &attr, 0);
    
    LEAVE();
}   

void
InitOutput (ScreenInfo *screen_info, int argc, char **argv)
{
    ENTER();
    KdInitOutput(screen_info, argc, argv);
    LEAVE();
}   

void
InitInput (int argc, char **argv)
{
    ENTER();

    KdAddKeyboardDriver(&LinuxEvdevKeyboardDriver);
    KdAddPointerDriver(&TsDriver);

    KdInitInput();
    
    LEAVE();
}

void
ddxUseMsg (void)
{
    KdUseMsg();
}

int
ddxProcessArgument (int argc, char **argv, int i)
{
    return KdProcessArgument(argc, argv, i);
}
