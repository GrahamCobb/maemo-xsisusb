/*
 * $Id$
 *
 * Copyright © 2008 Graham Cobb
 * Copyright © 2004 Keith Packard
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

void
InitCard (char *name)
{
    KdCardAttr	attr;

    KdCardInfoAdd (&sisusbFuncs, &attr, 0);
}

void
InitOutput (ScreenInfo *pScreenInfo, int argc, char **argv)
{
    KdInitOutput (pScreenInfo, argc, argv);
}

void
InitInput (int argc, char **argv)
{
    KdAddKeyboardDriver(&SiSusbKeyboardDriver);
    KdAddPointerDriver(&SiSusbPointerDriver);

    KdInitInput ();
}

void
ddxUseMsg (void)
{
    KdUseMsg();
}

int
ddxProcessArgument (int argc, char **argv, int i)
{
    return KdProcessArgument (argc, argv, i);
}

void
OsVendorInit (void)
{
    KdOsInit (&SiSusbOsFuncs);
}

KdCardFuncs	sisusbFuncs = {
    sisusbCardInit,	    /* cardinit */
    sisusbScreenInit,	    /* scrinit */
    sisusbInitScreen,	    /* initScreen */
    sisusbFinishInitScreen,  /* finishInitScreen */
    sisusbCreateResources,   /* createRes */
    sisusbPreserve,	    /* preserve */
    sisusbEnable,	    /* enable */
    sisusbDPMS,		    /* dpms */
    sisusbDisable,	    /* disable */
    sisusbRestore,	    /* restore */
    sisusbScreenFini,	    /* scrfini */
    sisusbCardFini,	    /* cardfini */
    
    0,			    /* initCursor */
    0,			    /* enableCursor */
    0,			    /* disableCursor */
    0,			    /* finiCursor */
    0,			    /* recolorCursor */
    
    0,			    /* initAccel */
    0,			    /* enableAccel */
    0,			    /* disableAccel */
    0,			    /* finiAccel */
    
    sisusbGetColors,    	    /* getColors */
    sisusbPutColors,	    /* putColors */
};
