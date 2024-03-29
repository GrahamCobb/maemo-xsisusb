/*
 * $Id$
 *
 * Copyright � 2000 Keith Packard
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
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdlib.h>

#include    <X11/X.h>
#include    "scrnintstr.h"
#include    "windowstr.h"
#include    "dixfontstr.h"
#include    "mi.h"
#include    "regionstr.h"
#include    "globals.h"
#include    "gcstruct.h"
#include    "shadow.h"

int shadowScrPrivateIndex;
int shadowGeneration;

#define wrap(priv, real, mem) {\
    priv->mem = real->mem; \
    real->mem = shadow##mem; \
}

#define unwrap(priv, real, mem) {\
    real->mem = priv->mem; \
}

static Bool
dummyPreupdate(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    return TRUE;
}

static void
shadowRedisplay(ScreenPtr pScreen)
{
    shadowBuf(pScreen);
    RegionPtr pRegion;

    if (!pBuf || !pBuf->pDamage || !pBuf->update)
	return;
    /* allow deferral of updates, to minimize overdraw, etc. */
    if (!pBuf->ready_to_update(pScreen, pBuf))
	return;
    pRegion = DamageRegion(pBuf->pDamage);
    if (REGION_NOTEMPTY(pScreen, pRegion)) {
	(*pBuf->update)(pScreen, pBuf);
	DamageEmpty(pBuf->pDamage);
    }
}

static void
shadowBlockHandler(pointer data, OSTimePtr pTimeout, pointer pRead)
{
    ScreenPtr pScreen = (ScreenPtr) data;

    shadowRedisplay(pScreen);
}

static void
shadowWakeupHandler(pointer data, int i, pointer LastSelectMask)
{
}

static void
shadowGetImage(DrawablePtr pDrawable, int sx, int sy, int w, int h,
	       unsigned int format, unsigned long planeMask, char *pdstLine)
{
    ScreenPtr pScreen = pDrawable->pScreen;
    shadowBuf(pScreen);

    /* Many apps use GetImage to sync with the visable frame buffer */
    if (pDrawable->type == DRAWABLE_WINDOW)
	shadowRedisplay(pScreen);
    unwrap(pBuf, pScreen, GetImage);
    pScreen->GetImage(pDrawable, sx, sy, w, h, format, planeMask, pdstLine);
    wrap(pBuf, pScreen, GetImage);
}

#define BACKWARDS_COMPATIBILITY

static Bool
shadowCloseScreen(int i, ScreenPtr pScreen)
{
    shadowBuf(pScreen);

    unwrap(pBuf, pScreen, GetImage);
    unwrap(pBuf, pScreen, CloseScreen);
    shadowRemove(pScreen, pBuf->pPixmap);
    DamageDestroy(pBuf->pDamage);
#ifdef BACKWARDS_COMPATIBILITY
    REGION_UNINIT(pScreen, &pBuf->damage); /* bc */
#endif
    if (pBuf->pPixmap)
	pScreen->DestroyPixmap(pBuf->pPixmap);
    xfree(pBuf);
    return pScreen->CloseScreen(i, pScreen);
}

#ifdef BACKWARDS_COMPATIBILITY
static void
shadowReportFunc(DamagePtr pDamage, RegionPtr pRegion, void *closure)
{
    ScreenPtr pScreen = closure;
    shadowBufPtr pBuf = pScreen->devPrivates[shadowScrPrivateIndex].ptr;

    /* Register the damaged region, use DamageReportNone below when we
     * want to break BC below... */
    REGION_UNION(pScreen, &pDamage->damage, &pDamage->damage, pRegion);

    /*
     * BC hack.  In 7.0 and earlier several drivers would inspect the
     * 'damage' member directly, so we have to keep it existing.
     */
    REGION_COPY(pScreen, &pBuf->damage, pRegion);
}
#endif

Bool
shadowSetup(ScreenPtr pScreen)
{
    shadowBufPtr pBuf;

    if (!DamageSetup(pScreen))
	return FALSE;

    if (shadowGeneration != serverGeneration) {
	shadowScrPrivateIndex = AllocateScreenPrivateIndex();
	if (shadowScrPrivateIndex == -1)
	    return FALSE;
	shadowGeneration = serverGeneration;
    }

    pBuf = (shadowBufPtr) xalloc(sizeof(shadowBufRec));
    if (!pBuf)
	return FALSE;
#ifdef BACKWARDS_COMPATIBILITY
    pBuf->pDamage = DamageCreate((DamageReportFunc)shadowReportFunc, 
		    		 (DamageDestroyFunc)NULL,
				 DamageReportRawRegion,
				 TRUE, pScreen, pScreen);
#else
    pBuf->pDamage = DamageCreate((DamageReportFunc)NULL, 
		    		 (DamageDestroyFunc)NULL,
				 DamageReportNone,
				 TRUE, pScreen, pScreen);
#endif
    if (!pBuf->pDamage) {
	xfree(pBuf);
	return FALSE;
    }

    wrap(pBuf, pScreen, CloseScreen);
    wrap(pBuf, pScreen, GetImage);
    pBuf->update = NULL;
    pBuf->window = NULL;
    pBuf->pPixmap = 0;
    pBuf->closure = 0;
    pBuf->randr = 0;
    pBuf->ready_to_update = NULL;
#ifdef BACKWARDS_COMPATIBILITY
    REGION_NULL(pScreen, &pBuf->damage); /* bc */
#endif

    pScreen->devPrivates[shadowScrPrivateIndex].ptr = (pointer) pBuf;
    return TRUE;
}

Bool
shadowAdd(ScreenPtr pScreen, PixmapPtr pPixmap, ShadowUpdateProc update,
	  ShadowWindowProc window, ShadowPreupdateProc preupdate,
          int randr, void *closure)
{
    shadowBuf(pScreen);

    if (!RegisterBlockAndWakeupHandlers(shadowBlockHandler, shadowWakeupHandler,
					(pointer)pScreen))
	return FALSE;

    /*
     * Map simple rotation values to bitmasks; fortunately,
     * these are all unique
     */
    switch (randr) {
    case 0:
	randr = SHADOW_ROTATE_0;
	break;
    case 90:
	randr = SHADOW_ROTATE_90;
	break;
    case 180:
	randr = SHADOW_ROTATE_180;
	break;
    case 270:
	randr = SHADOW_ROTATE_270;
	break;
    }
    pBuf->update = update;
    pBuf->window = window;
    if (preupdate)
        pBuf->ready_to_update = preupdate;
    else
        pBuf->ready_to_update = dummyPreupdate;
    pBuf->randr = randr;
    pBuf->closure = 0;
    pBuf->pPixmap = pPixmap;
    DamageRegister(&pPixmap->drawable, pBuf->pDamage);
    return TRUE;
}

void
shadowRemove(ScreenPtr pScreen, PixmapPtr pPixmap)
{
    shadowBuf(pScreen);

    if (pBuf->pPixmap) {
	DamageUnregister(&pBuf->pPixmap->drawable, pBuf->pDamage);
	pBuf->update = 0;
	pBuf->window = 0;
	pBuf->randr = 0;
	pBuf->closure = 0;
	pBuf->pPixmap = 0;
    }

    RemoveBlockAndWakeupHandlers(shadowBlockHandler, shadowWakeupHandler,
				 (pointer) pScreen);
}

Bool
shadowInit(ScreenPtr pScreen, ShadowUpdateProc update, ShadowWindowProc window)
{
    PixmapPtr pPixmap;
    
    pPixmap = pScreen->CreatePixmap(pScreen, pScreen->width, pScreen->height,
				    pScreen->rootDepth);
    if (!pPixmap)
	return FALSE;
    
    if (!shadowSetup(pScreen)) {
	pScreen->DestroyPixmap(pPixmap);
	return FALSE;
    }

    shadowAdd(pScreen, pPixmap, update, window, NULL, SHADOW_ROTATE_0, 0);

    return TRUE;
}
