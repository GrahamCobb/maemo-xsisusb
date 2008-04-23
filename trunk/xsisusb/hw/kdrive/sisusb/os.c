/*
 * $Id$
 *
 * Copyright � 2008 Graham Cobb
 * Copyright � 2004 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * This component re-uses code from the kdrive components developed by Keith Packard.
 * Changes are by Graham Cobb.
 *
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif
#include "sisusb.h"

static int
SiSusbInit (void)
{
    return 1;
}

static void
SiSusbEnable (void)
{
}

static Bool
SiSusbSpecialKey (KeySym sym)
{
    return FALSE;
}

static void
SiSusbDisable (void)
{
}

static void
SiSusbFini (void)
{
}

static void
SiSusbVT (int a, int b)
{
}

KdOsFuncs SiSusbOsFuncs = {
    SiSusbInit,
    SiSusbEnable,
    SiSusbSpecialKey,
    SiSusbDisable,
    SiSusbFini,
    NULL,
    SiSusbVT, 
};
