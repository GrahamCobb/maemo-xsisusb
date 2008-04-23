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

extern int KdTsPhyScreen;

Bool
sisusbInitialize (KdCardInfo *card, SiSusbPriv *priv)
{
    priv->base = 0;
    priv->bytes_per_line = 0;
    priv->sisusbfd = -1;
    return TRUE;
}

Bool
sisusbCardInit (KdCardInfo *card)
{
    SiSusbPriv	*priv;

    priv = (SiSusbPriv *) xalloc (sizeof (SiSusbPriv));
    if (!priv)
	return FALSE;
    
    if (!sisusbInitialize (card, priv))
    {
	xfree (priv);
	return FALSE;
    }
    card->driver = priv;
    
    return TRUE;
}

Bool
sisusbScreenInitialize (KdScreenInfo *screen, SiSusbScrPriv *scrpriv)
{
    if (!screen->width || !screen->height)
    {
	screen->width = 1024;
	screen->height = 768;
	screen->rate = 72;
    }

    if (screen->width <= 0)
	screen->width = 1;
    if (screen->height <= 0)
	screen->height = 1;
    
    if (!screen->fb[0].depth)
	screen->fb[0].depth = 24;

    if (screen->fb[0].depth <= 8)
    {
	screen->fb[0].visuals = ((1 << StaticGray) |
				 (1 << GrayScale) |
				 (1 << StaticColor) |
				 (1 << PseudoColor) |
				 (1 << TrueColor) |
				 (1 << DirectColor));
    }
    else 
    {
	screen->fb[0].visuals = (1 << TrueColor);
#define Mask(o,l)   (((1 << l) - 1) << o)
	if (screen->fb[0].depth <= 15)
	{
	    screen->fb[0].depth = 15;
	    screen->fb[0].bitsPerPixel = 16;
	    screen->fb[0].redMask = Mask (10, 5);
	    screen->fb[0].greenMask = Mask (5, 5);
	    screen->fb[0].blueMask = Mask (0, 5);
	}
	else if (screen->fb[0].depth <= 16)
	{
	    screen->fb[0].depth = 16;
	    screen->fb[0].bitsPerPixel = 16;
	    screen->fb[0].redMask = Mask (11, 5);
	    screen->fb[0].greenMask = Mask (5, 6);
	    screen->fb[0].blueMask = Mask (0, 5);
	}
	else
	{
	    screen->fb[0].depth = 24;
	    screen->fb[0].bitsPerPixel = 32;
	    screen->fb[0].redMask = Mask (16, 8);
	    screen->fb[0].greenMask = Mask (8, 8);
	    screen->fb[0].blueMask = Mask (0, 8);
	}
    }

    scrpriv->randr = screen->randr;

    sisusbOpenDevice (screen);

    return sisusbMapFramebuffer (screen);
}

Bool
sisusbScreenInit (KdScreenInfo *screen)
{
    SiSusbScrPriv *scrpriv;

    scrpriv = xalloc (sizeof (SiSusbScrPriv));
    if (!scrpriv)
	return FALSE;
    memset (scrpriv, '\0', sizeof (SiSusbScrPriv));
    screen->driver = scrpriv;
    if (!sisusbScreenInitialize (screen, scrpriv))
    {
	screen->driver = 0;
	xfree (scrpriv);
	return FALSE;
    }
    return TRUE;
}
    
void *
sisusbWindowLinear (ScreenPtr	pScreen,
		   CARD32	row,
		   CARD32	offset,
		   int		mode,
		   CARD32	*size,
		   void		*closure)
{
    KdScreenPriv(pScreen);
    SiSusbPriv	    *priv = pScreenPriv->card->driver;

    if (!pScreenPriv->enabled)
	return 0;
    *size = priv->bytes_per_line;
    return priv->base + row * priv->bytes_per_line + offset;
}

Bool
sisusbMapFramebuffer (KdScreenInfo *screen)
{
    SiSusbScrPriv	*scrpriv = screen->driver;
    KdPointerMatrix	m;
    SiSusbPriv		*priv = screen->card->driver;

    /* We need to use shadow */
    scrpriv->shadow = TRUE;
    
    KdComputePointerMatrix (&m, scrpriv->randr, screen->width, screen->height);
    
    KdSetPointerMatrix (&m);
    
    priv->bytes_per_line = ((screen->width * screen->fb[0].bitsPerPixel + 31) >> 5) << 2;
    if (priv->base)
	free (priv->base);
    priv->base = malloc (priv->bytes_per_line * screen->height);
    screen->memory_base = (CARD8 *) (priv->base);
    screen->memory_size = 0;
    screen->off_screen_base = 0;
    
    if (scrpriv->shadow)
    {
	if (!KdShadowFbAlloc (screen, 0, 
			      scrpriv->randr & (RR_Rotate_90|RR_Rotate_270)))
	    return FALSE;
    }
    else
    {
        screen->fb[0].byteStride = priv->bytes_per_line;
        screen->fb[0].pixelStride = (priv->bytes_per_line * 8/
				     screen->fb[0].bitsPerPixel);
        screen->fb[0].frameBuffer = (CARD8 *) (priv->base);
    }
    
    return TRUE;
}

void
sisusbSetScreenSizes (ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo	*screen = pScreenPriv->screen;
    SiSusbScrPriv	*scrpriv = screen->driver;

    if (scrpriv->randr & (RR_Rotate_0|RR_Rotate_180))
    {
	pScreen->width = screen->width;
	pScreen->height = screen->height;
	pScreen->mmWidth = screen->width_mm;
	pScreen->mmHeight = screen->height_mm;
    }
    else
    {
	pScreen->width = screen->width;
	pScreen->height = screen->height;
	pScreen->mmWidth = screen->height_mm;
	pScreen->mmHeight = screen->width_mm;
    }
}

Bool
sisusbUnmapFramebuffer (KdScreenInfo *screen)
{
    SiSusbPriv		*priv = screen->card->driver;
    KdShadowFbFree (screen, 0);
    if (priv->base)
    {
	free (priv->base);
	priv->base = 0;
    }
    return TRUE;
}

static void
sisusbShadowUpdate (ScreenPtr	    pScreen,
		    shadowBufPtr    pBuf)
{
    KdScreenPriv(pScreen);
    KdScreenInfo	*screen = pScreenPriv->screen;
    SiSusbScrPriv	*scrpriv = screen->driver;
  
    if (scrpriv->randr)
      shadowUpdateRotatePacked (pScreen, pBuf);
    else
      shadowUpdatePacked (pScreen, pBuf);

    sisusbResendFrame(pScreen);
}

Bool
sisusbSetShadow (ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo	*screen = pScreenPriv->screen;
    SiSusbScrPriv	*scrpriv = screen->driver;

    return KdShadowSet (pScreen, scrpriv->randr, sisusbShadowUpdate, sisusbWindowLinear, NULL);
}


#ifdef RANDR
Bool
sisusbRandRGetInfo (ScreenPtr pScreen, Rotation *rotations)
{
    KdScreenPriv(pScreen);
    KdScreenInfo	    *screen = pScreenPriv->screen;
    SiSusbScrPriv	    *scrpriv = screen->driver;
    RRScreenSizePtr	    pSize;
    Rotation		    randr;
    int			    n;
    
    *rotations = RR_Rotate_All|RR_Reflect_All;
    
    for (n = 0; n < pScreen->numDepths; n++)
	if (pScreen->allowedDepths[n].numVids)
	    break;
    if (n == pScreen->numDepths)
	return FALSE;
    
    pSize = RRRegisterSize (pScreen,
			    screen->width,
			    screen->height,
			    screen->width_mm,
			    screen->height_mm);
    
    randr = KdSubRotation (scrpriv->randr, screen->randr);
    
    RRSetCurrentConfig (pScreen, randr, 0, pSize);
    
    return TRUE;
}

Bool
sisusbRandRSetConfig (ScreenPtr		pScreen,
		     Rotation		randr,
		     int		rate,
		     RRScreenSizePtr	pSize)
{
    KdScreenPriv(pScreen);
    KdScreenInfo	*screen = pScreenPriv->screen;
    SiSusbScrPriv	*scrpriv = screen->driver;
    Bool		wasEnabled = pScreenPriv->enabled;
    SiSusbScrPriv	oldscr;
    int			oldwidth;
    int			oldheight;
    int			oldmmwidth;
    int			oldmmheight;
    int			newwidth, newheight;

    if (screen->randr & (RR_Rotate_0|RR_Rotate_180))
    {
	newwidth = pSize->width;
	newheight = pSize->height;
    }
    else
    {
	newwidth = pSize->height;
	newheight = pSize->width;
    }

    if (wasEnabled)
	KdDisableScreen (pScreen);

    oldscr = *scrpriv;
    
    oldwidth = screen->width;
    oldheight = screen->height;
    oldmmwidth = pScreen->mmWidth;
    oldmmheight = pScreen->mmHeight;
    
    /*
     * Set new configuration
     */
    
    scrpriv->randr = KdAddRotation (screen->randr, randr);

    KdOffscreenSwapOut (screen->pScreen);

    sisusbUnmapFramebuffer (screen);
    
    if (!sisusbMapFramebuffer (screen))
	goto bail4;

    KdShadowUnset (screen->pScreen);

    if (!sisusbSetShadow (screen->pScreen))
	goto bail4;

    sisusbSetScreenSizes (screen->pScreen);

    /*
     * Set frame buffer mapping
     */
    (*pScreen->ModifyPixmapHeader) (fbGetScreenPixmap (pScreen),
				    pScreen->width,
				    pScreen->height,
				    screen->fb[0].depth,
				    screen->fb[0].bitsPerPixel,
				    screen->fb[0].byteStride,
				    screen->fb[0].frameBuffer);
    
    /* set the subpixel order */
    
    KdSetSubpixelOrder (pScreen, scrpriv->randr);
    if (wasEnabled)
	KdEnableScreen (pScreen);

    return TRUE;

bail4:
    sisusbUnmapFramebuffer (screen);
    *scrpriv = oldscr;
    (void) sisusbMapFramebuffer (screen);
    pScreen->width = oldwidth;
    pScreen->height = oldheight;
    pScreen->mmWidth = oldmmwidth;
    pScreen->mmHeight = oldmmheight;
    
    if (wasEnabled)
	KdEnableScreen (pScreen);
    return FALSE;
}

Bool
sisusbRandRInit (ScreenPtr pScreen)
{
    rrScrPrivPtr    pScrPriv;
    
    if (!RRScreenInit (pScreen))
	return FALSE;

    pScrPriv = rrGetScrPriv(pScreen);
    pScrPriv->rrGetInfo = sisusbRandRGetInfo;
    pScrPriv->rrSetConfig = sisusbRandRSetConfig;
    return TRUE;
}
#endif

Bool
sisusbCreateColormap (ColormapPtr pmap)
{
    return fbInitializeColormap (pmap);
}

Bool
sisusbInitScreen (ScreenPtr pScreen)
{
#ifdef TOUCHSCREEN
    KdTsPhyScreen = pScreen->myNum;
#endif

    pScreen->CreateColormap = sisusbCreateColormap;
    return TRUE;
}

Bool
sisusbFinishInitScreen (ScreenPtr pScreen)
{
    if (!shadowSetup (pScreen))
	return FALSE;

#ifdef RANDR
    if (!sisusbRandRInit (pScreen))
	return FALSE;
#endif
    
    return TRUE;
}


Bool
sisusbCreateResources (ScreenPtr pScreen)
{
    return sisusbSetShadow (pScreen);
}

void
sisusbPreserve (KdCardInfo *card)
{
}

Bool
sisusbEnable (ScreenPtr pScreen)
{
    return TRUE;
}

Bool
sisusbDPMS (ScreenPtr pScreen, int mode)
{
    return TRUE;
}

void
sisusbDisable (ScreenPtr pScreen)
{
}

void
sisusbRestore (KdCardInfo *card)
{
}

void
sisusbScreenFini (KdScreenInfo *screen)
{
    sisusbCloseDevice (screen);
}

void
sisusbCardFini (KdCardInfo *card)
{
    SiSusbPriv	*priv = card->driver;

    if (priv->base)
	free (priv->base);
    xfree (priv);
}

void
sisusbGetColors (ScreenPtr pScreen, int fb, int n, xColorItem *pdefs)
{
    while (n--)
    {
	pdefs->red = 0;
	pdefs->green = 0;
	pdefs->blue = 0;
	pdefs++;
    }
}

void
sisusbPutColors (ScreenPtr pScreen, int fb, int n, xColorItem *pdefs)
{
}
