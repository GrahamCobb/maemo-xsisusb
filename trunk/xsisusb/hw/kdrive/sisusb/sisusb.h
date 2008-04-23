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

#ifndef _FBDEV_H_
#define _FBDEV_H_
#include <stdio.h>
#include <unistd.h>
#include "kdrive.h"

#ifdef RANDR
#include "randrstr.h"
#endif

typedef struct _sisusbPriv {
    CARD8	*base;
    int		bytes_per_line;
    int         sisusbfd;
} SiSusbPriv;
    
typedef struct _sisusbScrPriv {
    Rotation	randr;
    Bool	shadow;
    PixmapPtr	pShadow;
} SiSusbScrPriv;

extern KdCardFuncs  sisusbFuncs;

Bool
sisusbInitialize (KdCardInfo *card, SiSusbPriv *priv);

Bool
sisusbCardInit (KdCardInfo *card);

Bool
sisusbScreenInit (KdScreenInfo *screen);

Bool
sisusbScreenInitialize (KdScreenInfo *screen, SiSusbScrPriv *scrpriv);
    
Bool
sisusbInitScreen (ScreenPtr pScreen);

Bool
sisusbFinishInitScreen (ScreenPtr pScreen);

Bool
sisusbCreateResources (ScreenPtr pScreen);

void
sisusbPreserve (KdCardInfo *card);

Bool
sisusbEnable (ScreenPtr pScreen);

Bool
sisusbDPMS (ScreenPtr pScreen, int mode);

void
sisusbDisable (ScreenPtr pScreen);

void
sisusbRestore (KdCardInfo *card);

void
sisusbOpenDevice (KdScreenInfo *screen);

void
sisusbCloseDevice (KdScreenInfo *screen);

void
sisusbScreenFini (KdScreenInfo *screen);

void
sisusbCardFini (KdCardInfo *card);

void
sisusbGetColors (ScreenPtr pScreen, int fb, int n, xColorItem *pdefs);

void
sisusbPutColors (ScreenPtr pScreen, int fb, int n, xColorItem *pdefs);

Bool
sisusbMapFramebuffer (KdScreenInfo *screen);

void *
sisusbWindowLinear (ScreenPtr	pScreen,
		   CARD32	row,
		   CARD32	offset,
		   int		mode,
		   CARD32	*size,
		   void		*closure);

void
sisusbSetScreenSizes (ScreenPtr pScreen);

Bool
sisusbUnmapFramebuffer (KdScreenInfo *screen);

Bool
sisusbSetShadow (ScreenPtr pScreen);

void
sisusbResendFrame (ScreenPtr pScreen);

Bool
sisusbCreateColormap (ColormapPtr pmap);
    
#ifdef RANDR
Bool
sisusbRandRGetInfo (ScreenPtr pScreen, Rotation *rotations);

Bool
sisusbRandRSetConfig (ScreenPtr		pScreen,
		     Rotation		randr,
		     int		rate,
		     RRScreenSizePtr	pSize);
Bool
sisusbRandRInit (ScreenPtr pScreen);

#endif

extern KdPointerDriver SiSusbPointerDriver;

extern KdKeyboardDriver	SiSusbKeyboardDriver;

extern KdOsFuncs   SiSusbOsFuncs;

#endif /* _FBDEV_H_ */
