/*
 *
 * Copyright © 2000 SuSE, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of SuSE not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  SuSE makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * SuSE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL SuSE
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN 
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author:  Keith Packard, SuSE, Inc.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "misc.h"
#include "scrnintstr.h"
#include "os.h"
#include "regionstr.h"
#include "validate.h"
#include "windowstr.h"
#include "input.h"
#include "resource.h"
#include "colormapst.h"
#include "cursorstr.h"
#include "dixstruct.h"
#include "gcstruct.h"
#include "servermd.h"
#include "picturestr.h"

_X_EXPORT int	PictureScreenPrivateIndex = -1;
int		PictureWindowPrivateIndex;
int		PictureGeneration;
RESTYPE		PictureType;
RESTYPE		PictFormatType;
RESTYPE		GlyphSetType;
int		PictureCmapPolicy = PictureCmapPolicyDefault;

/* Picture Private machinery */

static int picturePrivateCount;

void
ResetPicturePrivateIndex (void)
{
    picturePrivateCount = 0;
}

int
AllocatePicturePrivateIndex (void)
{
    return picturePrivateCount++;
}

Bool
AllocatePicturePrivate (ScreenPtr pScreen, int index2, unsigned int amount)
{
    PictureScreenPtr	ps = GetPictureScreen(pScreen);
    unsigned int	oldamount;

    /* Round up sizes for proper alignment */
    amount = ((amount + (sizeof(long) - 1)) / sizeof(long)) * sizeof(long);

    if (index2 >= ps->PicturePrivateLen)
    {
	unsigned int *nsizes;

	nsizes = (unsigned int *)xrealloc(ps->PicturePrivateSizes,
					  (index2 + 1) * sizeof(unsigned int));
	if (!nsizes)
	    return FALSE;
	while (ps->PicturePrivateLen <= index2)
	{
	    nsizes[ps->PicturePrivateLen++] = 0;
	    ps->totalPictureSize += sizeof(DevUnion);
	}
	ps->PicturePrivateSizes = nsizes;
    }
    oldamount = ps->PicturePrivateSizes[index2];
    if (amount > oldamount)
    {
	ps->PicturePrivateSizes[index2] = amount;
	ps->totalPictureSize += (amount - oldamount);
    }

    return TRUE;
}


Bool
PictureDestroyWindow (WindowPtr pWindow)
{
    ScreenPtr		pScreen = pWindow->drawable.pScreen;
    PicturePtr		pPicture;
    PictureScreenPtr    ps = GetPictureScreen(pScreen);
    Bool		ret;

    while ((pPicture = GetPictureWindow(pWindow)))
    {
	SetPictureWindow(pWindow, pPicture->pNext);
	if (pPicture->id)
	    FreeResource (pPicture->id, PictureType);
	FreePicture ((pointer) pPicture, pPicture->id);
    }
    pScreen->DestroyWindow = ps->DestroyWindow;
    ret = (*pScreen->DestroyWindow) (pWindow);
    ps->DestroyWindow = pScreen->DestroyWindow;
    pScreen->DestroyWindow = PictureDestroyWindow;
    return ret;
}

Bool
PictureCloseScreen (int index, ScreenPtr pScreen)
{
    PictureScreenPtr    ps = GetPictureScreen(pScreen);
    Bool                ret;
    int			n;

    pScreen->CloseScreen = ps->CloseScreen;
    ret = (*pScreen->CloseScreen) (index, pScreen);
    PictureResetFilters (pScreen);
    for (n = 0; n < ps->nformats; n++)
	if (ps->formats[n].type == PictTypeIndexed)
	    (*ps->CloseIndexed) (pScreen, &ps->formats[n]);
    GlyphUninit (pScreen);
    SetPictureScreen(pScreen, 0);
    if (ps->PicturePrivateSizes)
	xfree (ps->PicturePrivateSizes);
    xfree (ps->formats);
    xfree (ps);
    return ret;
}

void
PictureStoreColors (ColormapPtr pColormap, int ndef, xColorItem *pdef)
{
    ScreenPtr		pScreen = pColormap->pScreen;
    PictureScreenPtr    ps = GetPictureScreen(pScreen);

    pScreen->StoreColors = ps->StoreColors;
    (*pScreen->StoreColors) (pColormap, ndef, pdef);
    ps->StoreColors = pScreen->StoreColors;
    pScreen->StoreColors = PictureStoreColors;

    if (pColormap->class == PseudoColor || pColormap->class == GrayScale)
    {
	PictFormatPtr	format = ps->formats;
	int		nformats = ps->nformats;

	while (nformats--)
	{
	    if (format->type == PictTypeIndexed &&
		format->index.pColormap == pColormap)
	    {
		(*ps->UpdateIndexed) (pScreen, format, ndef, pdef);
		break;
	    }
	    format++;
	}
    }
}

static int
visualDepth (ScreenPtr pScreen, VisualPtr pVisual)
{
    int		d, v;
    DepthPtr	pDepth;

    for (d = 0; d < pScreen->numDepths; d++)
    {
	pDepth = &pScreen->allowedDepths[d];
	for (v = 0; v < pDepth->numVids; v++)
	    if (pDepth->vids[v] == pVisual->vid)
		return pDepth->depth;
    }
    return 0;
}

typedef struct _formatInit {
    CARD32  format;
    CARD8   depth;
} FormatInitRec, *FormatInitPtr;

static int
addFormat (FormatInitRec    formats[256],
	   int		    nformat,
	   CARD32	    format,
	   CARD8	    depth)
{
    int	n;

    for (n = 0; n < nformat; n++)
	if (formats[n].format == format && formats[n].depth == depth)
	    return nformat;
    formats[nformat].format = format;
    formats[nformat].depth = depth;
    return ++nformat;
}

#define Mask(n)	((n) == 32 ? 0xffffffff : ((1 << (n))-1))

PictFormatPtr
PictureCreateDefaultFormats (ScreenPtr pScreen, int *nformatp)
{
    int		    nformats, f;
    PictFormatPtr   pFormats;
    FormatInitRec   formats[1024];
    CARD32	    format;
    CARD8	    depth;
    VisualPtr	    pVisual;
    int		    v;
    int		    bpp;
    int		    type;
    int		    r, g, b;
    int		    d;
    DepthPtr	    pDepth;

    nformats = 0;
    /* formats required by protocol */
    formats[nformats].format = PICT_a1;
    formats[nformats].depth = 1;
    nformats++;
    formats[nformats].format = PICT_FORMAT(BitsPerPixel(8),
					   PICT_TYPE_A,
					   8, 0, 0, 0);
    formats[nformats].depth = 8;
    nformats++;
    formats[nformats].format = PICT_FORMAT(BitsPerPixel(4),
					   PICT_TYPE_A,
					   4, 0, 0, 0);
    formats[nformats].depth = 4;
    nformats++;
    formats[nformats].format = PICT_a8r8g8b8;
    formats[nformats].depth = 32;
    nformats++;
    formats[nformats].format = PICT_x8r8g8b8;
    formats[nformats].depth = 32;
    nformats++;

    /* now look through the depths and visuals adding other formats */
    for (v = 0; v < pScreen->numVisuals; v++)
    {
	pVisual = &pScreen->visuals[v];
	depth = visualDepth (pScreen, pVisual);
	if (!depth)
	    continue;
    	bpp = BitsPerPixel (depth);
	switch (pVisual->class) {
	case DirectColor:
	case TrueColor:
	    r = Ones (pVisual->redMask);
	    g = Ones (pVisual->greenMask);
	    b = Ones (pVisual->blueMask);
	    type = PICT_TYPE_OTHER;
	    /*
	     * Current rendering code supports only two direct formats,
	     * fields must be packed together at the bottom of the pixel
	     * and must be either RGB or BGR
	     */
	    if (pVisual->offsetBlue == 0 &&
		pVisual->offsetGreen == b &&
		pVisual->offsetRed == b + g)
	    {
		type = PICT_TYPE_ARGB;
	    }
	    else if (pVisual->offsetRed == 0 &&
		     pVisual->offsetGreen == r && 
		     pVisual->offsetBlue == r + g)
	    {
		type = PICT_TYPE_ABGR;
	    }
	    if (type != PICT_TYPE_OTHER)
	    {
		format = PICT_FORMAT(bpp, type, 0, r, g, b);
		nformats = addFormat (formats, nformats, format, depth);
	    }
	    break;
	case StaticColor:
	case PseudoColor:
	    format = PICT_VISFORMAT (bpp, PICT_TYPE_COLOR, v);
	    nformats = addFormat (formats, nformats, format, depth);
	    break;
	case StaticGray:
	case GrayScale:
	    format = PICT_VISFORMAT (bpp, PICT_TYPE_GRAY, v);
	    nformats = addFormat (formats, nformats, format, depth);
	    break;
	}
    }
    /*
     * Walk supported depths and add useful Direct formats
     */
    for (d = 0; d < pScreen->numDepths; d++)
    {
	pDepth = &pScreen->allowedDepths[d];
	bpp = BitsPerPixel (pDepth->depth);
	format = 0;
	switch (bpp) {
	case 16:
	    /* depth 12 formats */
	    if (pDepth->depth >= 12)
	    {
		nformats = addFormat (formats, nformats,
				      PICT_x4r4g4b4, pDepth->depth);
		nformats = addFormat (formats, nformats,
				      PICT_x4b4g4r4, pDepth->depth);
	    }
	    /* depth 15 formats */
	    if (pDepth->depth >= 15)
	    {
		nformats = addFormat (formats, nformats,
				      PICT_x1r5g5b5, pDepth->depth);
		nformats = addFormat (formats, nformats,
				      PICT_x1b5g5r5, pDepth->depth);
	    }
	    /* depth 16 formats */
	    if (pDepth->depth >= 16) 
	    {
		nformats = addFormat (formats, nformats,
				      PICT_a1r5g5b5, pDepth->depth);
		nformats = addFormat (formats, nformats,
				      PICT_a1b5g5r5, pDepth->depth);
		nformats = addFormat (formats, nformats,
				      PICT_r5g6b5, pDepth->depth);
		nformats = addFormat (formats, nformats,
				      PICT_b5g6r5, pDepth->depth);
		nformats = addFormat (formats, nformats,
				      PICT_a4r4g4b4, pDepth->depth);
		nformats = addFormat (formats, nformats,
				      PICT_a4b4g4r4, pDepth->depth);
	    }
	    break;
	case 24:
	    if (pDepth->depth >= 24)
	    {
		nformats = addFormat (formats, nformats,
				      PICT_r8g8b8, pDepth->depth);
		nformats = addFormat (formats, nformats,
				      PICT_b8g8r8, pDepth->depth);
	    }
	    break;
	case 32:
	    if (pDepth->depth >= 24)
	    {
		nformats = addFormat (formats, nformats,
				      PICT_x8r8g8b8, pDepth->depth);
		nformats = addFormat (formats, nformats,
				      PICT_x8b8g8r8, pDepth->depth);
	    }
	    break;
	}
    }
    

    pFormats = (PictFormatPtr) xalloc (nformats * sizeof (PictFormatRec));
    if (!pFormats)
	return 0;
    memset (pFormats, '\0', nformats * sizeof (PictFormatRec));
    for (f = 0; f < nformats; f++)
    {
        pFormats[f].id = FakeClientID (0);
	pFormats[f].depth = formats[f].depth;
	format = formats[f].format;
	pFormats[f].format = format;
	switch (PICT_FORMAT_TYPE(format)) {
	case PICT_TYPE_ARGB:
	    pFormats[f].type = PictTypeDirect;
	    
	    pFormats[f].direct.alphaMask = Mask(PICT_FORMAT_A(format));
	    if (pFormats[f].direct.alphaMask)
		pFormats[f].direct.alpha = (PICT_FORMAT_R(format) +
					    PICT_FORMAT_G(format) +
					    PICT_FORMAT_B(format));
	    
	    pFormats[f].direct.redMask = Mask(PICT_FORMAT_R(format));
	    pFormats[f].direct.red = (PICT_FORMAT_G(format) + 
				      PICT_FORMAT_B(format));
	    
	    pFormats[f].direct.greenMask = Mask(PICT_FORMAT_G(format));
	    pFormats[f].direct.green = PICT_FORMAT_B(format);
	    
	    pFormats[f].direct.blueMask = Mask(PICT_FORMAT_B(format));
	    pFormats[f].direct.blue = 0;
	    break;

	case PICT_TYPE_ABGR:
	    pFormats[f].type = PictTypeDirect;
	    
	    pFormats[f].direct.alphaMask = Mask(PICT_FORMAT_A(format));
	    if (pFormats[f].direct.alphaMask)
		pFormats[f].direct.alpha = (PICT_FORMAT_B(format) +
					    PICT_FORMAT_G(format) +
					    PICT_FORMAT_R(format));
	    
	    pFormats[f].direct.blueMask = Mask(PICT_FORMAT_B(format));
	    pFormats[f].direct.blue = (PICT_FORMAT_G(format) + 
				       PICT_FORMAT_R(format));
	    
	    pFormats[f].direct.greenMask = Mask(PICT_FORMAT_G(format));
	    pFormats[f].direct.green = PICT_FORMAT_R(format);
	    
	    pFormats[f].direct.redMask = Mask(PICT_FORMAT_R(format));
	    pFormats[f].direct.red = 0;
	    break;

	case PICT_TYPE_A:
	    pFormats[f].type = PictTypeDirect;

	    pFormats[f].direct.alpha = 0;
	    pFormats[f].direct.alphaMask = Mask(PICT_FORMAT_A(format));

	    /* remaining fields already set to zero */
	    break;
	    
	case PICT_TYPE_COLOR:
	case PICT_TYPE_GRAY:
	    pFormats[f].type = PictTypeIndexed;
	    pFormats[f].index.vid = pScreen->visuals[PICT_FORMAT_VIS(format)].vid;
	    break;
	}
    }
    *nformatp = nformats;
    return pFormats;
}

static VisualPtr
PictureFindVisual (ScreenPtr pScreen, VisualID visual)
{
    int         i;
    VisualPtr   pVisual;
    for (i = 0, pVisual = pScreen->visuals;
         i < pScreen->numVisuals;
         i++, pVisual++)
    {
        if (pVisual->vid == visual)
            return pVisual;
    }
    return 0;
}

Bool
PictureInitIndexedFormats (ScreenPtr pScreen)
{
    PictureScreenPtr    ps = GetPictureScreenIfSet(pScreen);
    PictFormatPtr	format;
    int			nformat;

    if (!ps)
	return FALSE;
    format = ps->formats;
    nformat = ps->nformats;
    while (nformat--)
    {
	if (format->type == PictTypeIndexed && !format->index.pColormap)
	{
	    if (format->index.vid == pScreen->rootVisual)
		format->index.pColormap = (ColormapPtr) LookupIDByType(pScreen->defColormap,
								       RT_COLORMAP);
	    else
	    {
                VisualPtr   pVisual;

                pVisual = PictureFindVisual (pScreen, format->index.vid);
		if (CreateColormap (FakeClientID (0), pScreen,
				    pVisual,
				    &format->index.pColormap, AllocNone,
				    0) != Success)
		{
		    return FALSE;
		}
	    }
	    if (!(*ps->InitIndexed) (pScreen, format))
		return FALSE;
	}
	format++;
    }
    return TRUE;
}

Bool
PictureFinishInit (void)
{
    int	    s;

    for (s = 0; s < screenInfo.numScreens; s++)
    {
	if (!GlyphFinishInit (screenInfo.screens[s]))
	    return FALSE;
	if (!PictureInitIndexedFormats (screenInfo.screens[s]))
	    return FALSE;
	(void) AnimCurInit (screenInfo.screens[s]);
    }

    return TRUE;
}

_X_EXPORT Bool
PictureSetSubpixelOrder (ScreenPtr pScreen, int subpixel)
{
    PictureScreenPtr    ps = GetPictureScreenIfSet(pScreen);

    if (!ps)
	return FALSE;
    ps->subpixel = subpixel;
    return TRUE;
    
}

_X_EXPORT int
PictureGetSubpixelOrder (ScreenPtr pScreen)
{
    PictureScreenPtr    ps = GetPictureScreenIfSet(pScreen);

    if (!ps)
	return SubPixelUnknown;
    return ps->subpixel;
}
    
PictFormatPtr
PictureMatchVisual (ScreenPtr pScreen, int depth, VisualPtr pVisual)
{
    PictureScreenPtr    ps = GetPictureScreenIfSet(pScreen);
    PictFormatPtr	format;
    int			nformat;
    int			type;

    if (!ps)
	return 0;
    format = ps->formats;
    nformat = ps->nformats;
    switch (pVisual->class) {
    case StaticGray:
    case GrayScale:
    case StaticColor:
    case PseudoColor:
	type = PictTypeIndexed;
	break;
    case TrueColor:
    case DirectColor:
	type = PictTypeDirect;
	break;
    default:
	return 0;
    }
    while (nformat--)
    {
	if (format->depth == depth && format->type == type)
	{
	    if (type == PictTypeIndexed)
	    {
		if (format->index.vid == pVisual->vid)
		    return format;
	    }
	    else
	    {
		if (format->direct.redMask << format->direct.red == 
		    pVisual->redMask &&
		    format->direct.greenMask << format->direct.green == 
		    pVisual->greenMask &&
		    format->direct.blueMask << format->direct.blue == 
		    pVisual->blueMask)
		{
		    return format;
		}
	    }
	}
	format++;
    }
    return 0;
}

PictFormatPtr
PictureMatchFormat (ScreenPtr pScreen, int depth, CARD32 f)
{
    PictureScreenPtr    ps = GetPictureScreenIfSet(pScreen);
    PictFormatPtr	format;
    int			nformat;

    if (!ps)
	return 0;
    format = ps->formats;
    nformat = ps->nformats;
    while (nformat--)
    {
	if (format->depth == depth && format->format == (f & 0xffffff))
	    return format;
	format++;
    }
    return 0;
}

int
PictureParseCmapPolicy (const char *name)
{
    if ( strcmp (name, "default" ) == 0)
	return PictureCmapPolicyDefault;
    else if ( strcmp (name, "mono" ) == 0)
	return PictureCmapPolicyMono;
    else if ( strcmp (name, "gray" ) == 0)
	return PictureCmapPolicyGray;
    else if ( strcmp (name, "color" ) == 0)
	return PictureCmapPolicyColor;
    else if ( strcmp (name, "all" ) == 0)
	return PictureCmapPolicyAll;
    else
	return PictureCmapPolicyInvalid;
}

_X_EXPORT Bool
PictureInit (ScreenPtr pScreen, PictFormatPtr formats, int nformats)
{
    PictureScreenPtr	ps;
    int			n;
    CARD32		type, a, r, g, b;
    
    if (PictureGeneration != serverGeneration)
    {
	PictureType = CreateNewResourceType (FreePicture);
	if (!PictureType)
	    return FALSE;
	PictFormatType = CreateNewResourceType (FreePictFormat);
	if (!PictFormatType)
	    return FALSE;
	GlyphSetType = CreateNewResourceType (FreeGlyphSet);
	if (!GlyphSetType)
	    return FALSE;
	PictureScreenPrivateIndex = AllocateScreenPrivateIndex();
	if (PictureScreenPrivateIndex < 0)
	    return FALSE;
	PictureWindowPrivateIndex = AllocateWindowPrivateIndex();
	PictureGeneration = serverGeneration;
#ifdef XResExtension
	RegisterResourceName (PictureType, "PICTURE");
	RegisterResourceName (PictFormatType, "PICTFORMAT");
	RegisterResourceName (GlyphSetType, "GLYPHSET");
#endif
    }
    if (!AllocateWindowPrivate (pScreen, PictureWindowPrivateIndex, 0))
	return FALSE;
    
    if (!formats)
    {
	formats = PictureCreateDefaultFormats (pScreen, &nformats);
	if (!formats)
	    return FALSE;
    }
    for (n = 0; n < nformats; n++)
    {
	if (!AddResource (formats[n].id, PictFormatType, (pointer) (formats+n)))
	{
	    xfree (formats);
	    return FALSE;
	}
	if (formats[n].type == PictTypeIndexed)
	{
            VisualPtr   pVisual = PictureFindVisual (pScreen, formats[n].index.vid);
	    if ((pVisual->class | DynamicClass) == PseudoColor)
		type = PICT_TYPE_COLOR;
	    else
		type = PICT_TYPE_GRAY;
	    a = r = g = b = 0;
	}
	else
	{
	    if ((formats[n].direct.redMask|
		 formats[n].direct.blueMask|
		 formats[n].direct.greenMask) == 0)
		type = PICT_TYPE_A;
	    else if (formats[n].direct.red > formats[n].direct.blue)
		type = PICT_TYPE_ARGB;
	    else
		type = PICT_TYPE_ABGR;
	    a = Ones (formats[n].direct.alphaMask);
	    r = Ones (formats[n].direct.redMask);
	    g = Ones (formats[n].direct.greenMask);
	    b = Ones (formats[n].direct.blueMask);
	}
	formats[n].format = PICT_FORMAT(0,type,a,r,g,b);
    }
    ps = (PictureScreenPtr) xalloc (sizeof (PictureScreenRec));
    if (!ps)
    {
	xfree (formats);
	return FALSE;
    }
    SetPictureScreen(pScreen, ps);
    if (!GlyphInit (pScreen))
    {
	SetPictureScreen(pScreen, 0);
	xfree (formats);
	xfree (ps);
	return FALSE;
    }

    ps->totalPictureSize = sizeof (PictureRec);
    ps->PicturePrivateSizes = 0;
    ps->PicturePrivateLen = 0;
    
    ps->formats = formats;
    ps->fallback = formats;
    ps->nformats = nformats;
    
    ps->filters = 0;
    ps->nfilters = 0;
    ps->filterAliases = 0;
    ps->nfilterAliases = 0;

    ps->subpixel = SubPixelUnknown;

    ps->CloseScreen = pScreen->CloseScreen;
    ps->DestroyWindow = pScreen->DestroyWindow;
    ps->StoreColors = pScreen->StoreColors;
    pScreen->DestroyWindow = PictureDestroyWindow;
    pScreen->CloseScreen = PictureCloseScreen;
    pScreen->StoreColors = PictureStoreColors;

    if (!PictureSetDefaultFilters (pScreen))
    {
	PictureResetFilters (pScreen);
	SetPictureScreen(pScreen, 0);
	xfree (formats);
	xfree (ps);
	return FALSE;
    }

    return TRUE;
}

void
SetPictureToDefaults (PicturePtr    pPicture)
{
    pPicture->refcnt = 1;
    pPicture->repeat = 0;
    pPicture->graphicsExposures = FALSE;
    pPicture->subWindowMode = ClipByChildren;
    pPicture->polyEdge = PolyEdgeSharp;
    pPicture->polyMode = PolyModePrecise;
    pPicture->freeCompClip = FALSE;
    pPicture->clientClipType = CT_NONE;
    pPicture->componentAlpha = FALSE;
    pPicture->repeatType = RepeatNone;

    pPicture->alphaMap = 0;
    pPicture->alphaOrigin.x = 0;
    pPicture->alphaOrigin.y = 0;

    pPicture->clipOrigin.x = 0;
    pPicture->clipOrigin.y = 0;
    pPicture->clientClip = 0;

    pPicture->transform = 0;

    pPicture->dither = None;
    pPicture->filter = PictureGetFilterId (FilterNearest, -1, TRUE);
    pPicture->filter_params = 0;
    pPicture->filter_nparams = 0;

    pPicture->serialNumber = GC_CHANGE_SERIAL_BIT;
    pPicture->stateChanges = (1 << (CPLastBit+1)) - 1;
    pPicture->pSourcePict = 0;
}

PicturePtr
AllocatePicture (ScreenPtr  pScreen)
{
    PictureScreenPtr	ps = GetPictureScreen(pScreen);
    PicturePtr		pPicture;
    char		*ptr;
    DevUnion		*ppriv;
    unsigned int    	*sizes;
    unsigned int    	size;
    int			i;

    pPicture = (PicturePtr) xalloc (ps->totalPictureSize);
    if (!pPicture)
	return 0;
    ppriv = (DevUnion *)(pPicture + 1);
    pPicture->devPrivates = ppriv;
    sizes = ps->PicturePrivateSizes;
    ptr = (char *)(ppriv + ps->PicturePrivateLen);
    for (i = ps->PicturePrivateLen; --i >= 0; ppriv++, sizes++)
    {
	if ( (size = *sizes) )
	{
	    ppriv->ptr = (pointer)ptr;
	    ptr += size;
	}
	else
	    ppriv->ptr = (pointer)NULL;
    }
    return pPicture;
}

PicturePtr
CreatePicture (Picture		pid,
	       DrawablePtr	pDrawable,
	       PictFormatPtr	pFormat,
	       Mask		vmask,
	       XID		*vlist,
	       ClientPtr	client,
	       int		*error)
{
    PicturePtr		pPicture;
    PictureScreenPtr	ps = GetPictureScreen(pDrawable->pScreen);

    pPicture = AllocatePicture (pDrawable->pScreen);
    if (!pPicture)
    {
	*error = BadAlloc;
	return 0;
    }

    pPicture->id = pid;
    pPicture->pDrawable = pDrawable;
    pPicture->pFormat = pFormat;
    pPicture->format = pFormat->format | (pDrawable->bitsPerPixel << 24);
    if (pDrawable->type == DRAWABLE_PIXMAP)
    {
	++((PixmapPtr)pDrawable)->refcnt;
	pPicture->pNext = 0;
    }
    else
    {
	pPicture->pNext = GetPictureWindow(((WindowPtr) pDrawable));
	SetPictureWindow(((WindowPtr) pDrawable), pPicture);
    }

    SetPictureToDefaults (pPicture);
    
    if (vmask)
	*error = ChangePicture (pPicture, vmask, vlist, 0, client);
    else
	*error = Success;
    if (*error == Success)
	*error = (*ps->CreatePicture) (pPicture);
    if (*error != Success)
    {
	FreePicture (pPicture, (XID) 0);
	pPicture = 0;
    }
    return pPicture;
}

static CARD32 xRenderColorToCard32(xRenderColor c)
{
    return
        (c.alpha >> 8 << 24) |
        (c.red >> 8 << 16) |
        (c.green & 0xff00) |
        (c.blue >> 8);
}

static unsigned int premultiply(unsigned int x)
{
    unsigned int a = x >> 24;
    unsigned int t = (x & 0xff00ff) * a + 0x800080;
    t = (t + ((t >> 8) & 0xff00ff)) >> 8;
    t &= 0xff00ff;

    x = ((x >> 8) & 0xff) * a + 0x80;
    x = (x + ((x >> 8) & 0xff));
    x &= 0xff00;
    x |= t | (a << 24);
    return x;
}

static unsigned int INTERPOLATE_PIXEL_256(unsigned int x, unsigned int a,
                                          unsigned int y, unsigned int b)
{
    CARD32 t = (x & 0xff00ff) * a + (y & 0xff00ff) * b;
    t >>= 8;
    t &= 0xff00ff;

    x = ((x >> 8) & 0xff00ff) * a + ((y >> 8) & 0xff00ff) * b;
    x &= 0xff00ff00;
    x |= t;
    return x;
}

static void initGradientColorTable(SourcePictPtr pGradient, int *error)
{
    int begin_pos, end_pos;
    xFixed incr, dpos;
    int pos, current_stop;
    PictGradientStopPtr stops = pGradient->linear.stops;
    int nstops = pGradient->linear.nstops;

    /* The position where the gradient begins and ends */
    begin_pos = (stops[0].x * PICT_GRADIENT_STOPTABLE_SIZE) >> 16;
    end_pos = (stops[nstops - 1].x * PICT_GRADIENT_STOPTABLE_SIZE) >> 16;

    pos = 0; /* The position in the color table. */

    /* Up to first point */
    while (pos <= begin_pos) {
        pGradient->linear.colorTable[pos] = xRenderColorToCard32(stops[0].color);
        ++pos;
    }

    incr =  (1<<16)/ PICT_GRADIENT_STOPTABLE_SIZE; /* the double increment. */
    dpos = incr * pos; /* The position in terms of 0-1. */

    current_stop = 0; /* We always interpolate between current and current + 1. */

    /* Gradient area */
    while (pos < end_pos) {
        unsigned int current_color = xRenderColorToCard32(stops[current_stop].color);
        unsigned int next_color = xRenderColorToCard32(stops[current_stop + 1].color);

        int dist = (int)(256*(dpos - stops[current_stop].x)
                         / (stops[current_stop+1].x - stops[current_stop].x));
        int idist = 256 - dist;

        pGradient->linear.colorTable[pos] = premultiply(INTERPOLATE_PIXEL_256(current_color, idist, next_color, dist));

        ++pos;
        dpos += incr;

        if (dpos > stops[current_stop + 1].x)
            ++current_stop;
    }

    /* After last point */
    while (pos < PICT_GRADIENT_STOPTABLE_SIZE) {
        pGradient->linear.colorTable[pos] = xRenderColorToCard32(stops[nstops - 1].color);
        ++pos;
    }
}

static void initGradient(SourcePictPtr pGradient, int stopCount,
                         xFixed *stopPoints, xRenderColor *stopColors, int *error)
{
    int i;
    xFixed dpos;

    if (stopCount <= 0) {
        *error = BadValue;
        return;
    }

    dpos = -1;
    for (i = 0; i < stopCount; ++i) {
        if (stopPoints[i] <= dpos || stopPoints[i] > (1<<16)) {
            *error = BadValue;
            return;
        }
        dpos = stopPoints[i];
    }

    pGradient->linear.stops = xalloc(stopCount*sizeof(PictGradientStop));
    if (!pGradient->linear.stops) {
        *error = BadAlloc;
        return;
    }

    pGradient->linear.nstops = stopCount;

    for (i = 0; i < stopCount; ++i) {
        pGradient->linear.stops[i].x = stopPoints[i];
        pGradient->linear.stops[i].color = stopColors[i];
    }
    initGradientColorTable(pGradient, error);
}

static PicturePtr createSourcePicture(void)
{
    PicturePtr pPicture;
    pPicture = (PicturePtr) xalloc(sizeof(PictureRec));
    pPicture->pDrawable = 0;
    pPicture->format = PICT_a8r8g8b8;
    pPicture->pFormat = 0;
    pPicture->pNext = 0;
    pPicture->devPrivates = 0;

    SetPictureToDefaults(pPicture);
    return pPicture;
}

PicturePtr
CreateSolidPicture (Picture pid, xRenderColor *color, int *error)
{
    PicturePtr pPicture;
    pPicture = createSourcePicture();
    if (!pPicture) {
        *error = BadAlloc;
        return 0;
    }

    pPicture->id = pid;
    pPicture->pSourcePict = (SourcePictPtr) xalloc(sizeof(PictSolidFill));
    if (!pPicture->pSourcePict) {
        *error = BadAlloc;
        xfree(pPicture);
        return 0;
    }
    pPicture->pSourcePict->type = SourcePictTypeSolidFill;
    pPicture->pSourcePict->solidFill.color = xRenderColorToCard32(*color);
    return pPicture;
}

PicturePtr
CreateLinearGradientPicture (Picture pid, xPointFixed *p1, xPointFixed *p2,
                             int nStops, xFixed *stops, xRenderColor *colors, int *error)
{
    PicturePtr pPicture;

    if (nStops < 2) {
        *error = BadValue;
        return 0;
    }

    pPicture = createSourcePicture();
    if (!pPicture) {
        *error = BadAlloc;
        return 0;
    }
    if (p1->x == p2->x && p1->y == p2->y) {
        *error = BadValue;
        return 0;
    }

    pPicture->id = pid;
    pPicture->pSourcePict = (SourcePictPtr) xalloc(sizeof(PictLinearGradient));
    if (!pPicture->pSourcePict) {
        *error = BadAlloc;
        xfree(pPicture);
        return 0;
    }

    pPicture->pSourcePict->linear.type = SourcePictTypeLinear;
    pPicture->pSourcePict->linear.p1 = *p1;
    pPicture->pSourcePict->linear.p2 = *p2;

    initGradient(pPicture->pSourcePict, nStops, stops, colors, error);
    if (*error) {
        xfree(pPicture);
        return 0;
    }
    return pPicture;
}

#define FixedToDouble(x) ((x)/65536.)

PicturePtr
CreateRadialGradientPicture (Picture pid, xPointFixed *inner, xPointFixed *outer,
                             xFixed innerRadius, xFixed outerRadius,
                             int nStops, xFixed *stops, xRenderColor *colors, int *error)
{
    PicturePtr pPicture;
    PictRadialGradient *radial;

    if (nStops < 2) {
        *error = BadValue;
        return 0;
    }

    pPicture = createSourcePicture();
    if (!pPicture) {
        *error = BadAlloc;
        return 0;
    }
    {
        double dx = (double)(inner->x - outer->x);
        double dy = (double)(inner->y - outer->y);
        if (sqrt(dx*dx + dy*dy) + (double)(innerRadius) > (double)(outerRadius)) {
            *error = BadValue;
            return 0;
        }
    }

    pPicture->id = pid;
    pPicture->pSourcePict = (SourcePictPtr) xalloc(sizeof(PictRadialGradient));
    if (!pPicture->pSourcePict) {
        *error = BadAlloc;
        xfree(pPicture);
        return 0;
    }
    radial = &pPicture->pSourcePict->radial;

    radial->type = SourcePictTypeRadial;
    {
        double x = (double)innerRadius / (double)outerRadius;
        radial->dx = (outer->x - inner->x);
        radial->dy = (outer->y - inner->y);
        radial->fx = (inner->x) - x*radial->dx;
        radial->fy = (inner->y) - x*radial->dy;
        radial->m = 1./(1+x);
        radial->b = -x*radial->m;
        radial->dx /= 65536.;
        radial->dy /= 65536.;
        radial->fx /= 65536.;
        radial->fy /= 65536.;
        x = outerRadius/65536.;
        radial->a = x*x - radial->dx*radial->dx - radial->dy*radial->dy;
    }

    initGradient(pPicture->pSourcePict, nStops, stops, colors, error);
    if (*error) {
        xfree(pPicture);
        return 0;
    }
    return pPicture;
}

PicturePtr
CreateConicalGradientPicture (Picture pid, xPointFixed *center, xFixed angle,
                              int nStops, xFixed *stops, xRenderColor *colors, int *error)
{
    PicturePtr pPicture;

    if (nStops < 2) {
        *error = BadValue;
        return 0;
    }

    pPicture = createSourcePicture();
    if (!pPicture) {
        *error = BadAlloc;
        return 0;
    }

    pPicture->id = pid;
    pPicture->pSourcePict = (SourcePictPtr) xalloc(sizeof(PictConicalGradient));
    if (!pPicture->pSourcePict) {
        *error = BadAlloc;
        xfree(pPicture);
        return 0;
    }

    pPicture->pSourcePict->conical.type = SourcePictTypeConical;
    pPicture->pSourcePict->conical.center = *center;
    pPicture->pSourcePict->conical.angle = angle;

    initGradient(pPicture->pSourcePict, nStops, stops, colors, error);
    if (*error) {
        xfree(pPicture);
        return 0;
    }
    return pPicture;
}

#define NEXT_VAL(_type) (vlist ? (_type) *vlist++ : (_type) ulist++->val)

#define NEXT_PTR(_type) ((_type) ulist++->ptr)

int
ChangePicture (PicturePtr	pPicture,
	       Mask		vmask,
	       XID		*vlist,
	       DevUnion		*ulist,
	       ClientPtr	client)
{
    ScreenPtr pScreen = pPicture->pDrawable ? pPicture->pDrawable->pScreen : 0;
    PictureScreenPtr ps = pScreen ? GetPictureScreen(pScreen) : 0;
    BITS32		index2;
    int			error = 0;
    BITS32		maskQ;
    
    pPicture->serialNumber |= GC_CHANGE_SERIAL_BIT;
    maskQ = vmask;
    while (vmask && !error)
    {
	index2 = (BITS32) lowbit (vmask);
	vmask &= ~index2;
	pPicture->stateChanges |= index2;
	switch (index2)
	{
	case CPRepeat:
	    {
		unsigned int	newr;
		newr = NEXT_VAL(unsigned int);
		if (newr <= RepeatReflect)
		{
		    pPicture->repeat = (newr != RepeatNone);
		    pPicture->repeatType = newr;
		}
		else
		{
		    client->errorValue = newr;
		    error = BadValue;
		}
	    }
	    break;
	case CPAlphaMap:
	    {
		PicturePtr  pAlpha;
		
		if (vlist)
		{
		    Picture	pid = NEXT_VAL(Picture);

		    if (pid == None)
			pAlpha = 0;
		    else
		    {
			pAlpha = (PicturePtr) SecurityLookupIDByType(client,
								     pid, 
								     PictureType, 
								     DixWriteAccess|DixReadAccess);
			if (!pAlpha)
			{
			    client->errorValue = pid;
			    error = BadPixmap;
			    break;
			}
			if (pAlpha->pDrawable == NULL ||
			    pAlpha->pDrawable->type != DRAWABLE_PIXMAP)
			{
			    client->errorValue = pid;
			    error = BadMatch;
			    break;
			}
		    }
		}
		else
		    pAlpha = NEXT_PTR(PicturePtr);
		if (!error)
		{
		    if (pAlpha && pAlpha->pDrawable->type == DRAWABLE_PIXMAP)
			pAlpha->refcnt++;
		    if (pPicture->alphaMap)
			FreePicture ((pointer) pPicture->alphaMap, (XID) 0);
		    pPicture->alphaMap = pAlpha;
		}
	    }
	    break;
	case CPAlphaXOrigin:
	    pPicture->alphaOrigin.x = NEXT_VAL(INT16);
	    break;
	case CPAlphaYOrigin:
	    pPicture->alphaOrigin.y = NEXT_VAL(INT16);
	    break;
	case CPClipXOrigin:
	    pPicture->clipOrigin.x = NEXT_VAL(INT16);
	    break;
	case CPClipYOrigin:
	    pPicture->clipOrigin.y = NEXT_VAL(INT16);
	    break;
	case CPClipMask:
	    {
		Pixmap	    pid;
		PixmapPtr   pPixmap;
		int	    clipType;
                if (!pScreen)
                    return BadDrawable;

		if (vlist)
		{
		    pid = NEXT_VAL(Pixmap);
		    if (pid == None)
		    {
			clipType = CT_NONE;
			pPixmap = NullPixmap;
		    }
		    else
		    {
			clipType = CT_PIXMAP;
			pPixmap = (PixmapPtr)SecurityLookupIDByType(client,
								    pid, 
								    RT_PIXMAP,
								    DixReadAccess);
			if (!pPixmap)
			{
			    client->errorValue = pid;
			    error = BadPixmap;
			    break;
			}
		    }
		}
		else
		{
		    pPixmap = NEXT_PTR(PixmapPtr);
		    if (pPixmap)
			clipType = CT_PIXMAP;
		    else
			clipType = CT_NONE;
		}

		if (pPixmap)
		{
		    if ((pPixmap->drawable.depth != 1) ||
			(pPixmap->drawable.pScreen != pScreen))
		    {
			error = BadMatch;
			break;
		    }
		    else
		    {
			clipType = CT_PIXMAP;
			pPixmap->refcnt++;
		    }
		}
		error = (*ps->ChangePictureClip)(pPicture, clipType,
						 (pointer)pPixmap, 0);
		break;
	    }
	case CPGraphicsExposure:
	    {
		unsigned int	newe;
		newe = NEXT_VAL(unsigned int);
		if (newe <= xTrue)
		    pPicture->graphicsExposures = newe;
		else
		{
		    client->errorValue = newe;
		    error = BadValue;
		}
	    }
	    break;
	case CPSubwindowMode:
	    {
		unsigned int	news;
		news = NEXT_VAL(unsigned int);
		if (news == ClipByChildren || news == IncludeInferiors)
		    pPicture->subWindowMode = news;
		else
		{
		    client->errorValue = news;
		    error = BadValue;
		}
	    }
	    break;
	case CPPolyEdge:
	    {
		unsigned int	newe;
		newe = NEXT_VAL(unsigned int);
		if (newe == PolyEdgeSharp || newe == PolyEdgeSmooth)
		    pPicture->polyEdge = newe;
		else
		{
		    client->errorValue = newe;
		    error = BadValue;
		}
	    }
	    break;
	case CPPolyMode:
	    {
		unsigned int	newm;
		newm = NEXT_VAL(unsigned int);
		if (newm == PolyModePrecise || newm == PolyModeImprecise)
		    pPicture->polyMode = newm;
		else
		{
		    client->errorValue = newm;
		    error = BadValue;
		}
	    }
	    break;
	case CPDither:
	    pPicture->dither = NEXT_VAL(Atom);
	    break;
	case CPComponentAlpha:
	    {
		unsigned int	newca;

		newca = NEXT_VAL (unsigned int);
		if (newca <= xTrue)
		    pPicture->componentAlpha = newca;
		else
		{
		    client->errorValue = newca;
		    error = BadValue;
		}
	    }
	    break;
	default:
	    client->errorValue = maskQ;
	    error = BadValue;
	    break;
	}
    }
    if (ps)
        (*ps->ChangePicture) (pPicture, maskQ);
    return error;
}

int
SetPictureClipRects (PicturePtr	pPicture,
		     int	xOrigin,
		     int	yOrigin,
		     int	nRect,
		     xRectangle	*rects)
{
    ScreenPtr		pScreen = pPicture->pDrawable->pScreen;
    PictureScreenPtr	ps = GetPictureScreen(pScreen);
    RegionPtr		clientClip;
    int			result;

    clientClip = RECTS_TO_REGION(pScreen,
				 nRect, rects, CT_UNSORTED);
    if (!clientClip)
	return BadAlloc;
    result =(*ps->ChangePictureClip) (pPicture, CT_REGION, 
				      (pointer) clientClip, 0);
    if (result == Success)
    {
	pPicture->clipOrigin.x = xOrigin;
	pPicture->clipOrigin.y = yOrigin;
	pPicture->stateChanges |= CPClipXOrigin|CPClipYOrigin|CPClipMask;
	pPicture->serialNumber |= GC_CHANGE_SERIAL_BIT;
    }
    return result;
}

int
SetPictureClipRegion (PicturePtr    pPicture,
                      int           xOrigin,
                      int           yOrigin,
                      RegionPtr     pRegion)
{
    ScreenPtr           pScreen = pPicture->pDrawable->pScreen;
    PictureScreenPtr    ps = GetPictureScreen(pScreen);
    RegionPtr           clientClip;
    int                 result;
    int                 type;

    if (pRegion)
    {
        type = CT_REGION;
        clientClip = REGION_CREATE (pScreen,
                                    REGION_EXTENTS(pScreen, pRegion),
                                    REGION_NUM_RECTS(pRegion));
        if (!clientClip)
            return BadAlloc;
        if (!REGION_COPY (pSCreen, clientClip, pRegion))
        {
            REGION_DESTROY (pScreen, clientClip);
            return BadAlloc;
        }
    }
    else
    {
        type = CT_NONE;
        clientClip = 0;
    }

    result =(*ps->ChangePictureClip) (pPicture, type,
                                      (pointer) clientClip, 0);
    if (result == Success)
    {
        pPicture->clipOrigin.x = xOrigin;
        pPicture->clipOrigin.y = yOrigin;
        pPicture->stateChanges |= CPClipXOrigin|CPClipYOrigin|CPClipMask;
        pPicture->serialNumber |= GC_CHANGE_SERIAL_BIT;
    }
    return result;
}

static Bool
transformIsIdentity(PictTransform *t)
{
    return ((t->matrix[0][0] == t->matrix[1][1]) &&
            (t->matrix[0][0] == t->matrix[2][2]) &&
            (t->matrix[0][0] != 0) &&
            (t->matrix[0][1] == 0) &&
            (t->matrix[0][2] == 0) &&
            (t->matrix[1][0] == 0) &&
            (t->matrix[1][2] == 0) &&
            (t->matrix[2][0] == 0) &&
            (t->matrix[2][1] == 0));
}

int
SetPictureTransform (PicturePtr	    pPicture,
		     PictTransform  *transform)
{
    if (transform && transformIsIdentity (transform))
	transform = 0;
    
    if (transform)
    {
	if (!pPicture->transform)
	{
	    pPicture->transform = (PictTransform *) xalloc (sizeof (PictTransform));
	    if (!pPicture->transform)
		return BadAlloc;
	}
	*pPicture->transform = *transform;
    }
    else
    {
	if (pPicture->transform)
	{
	    xfree (pPicture->transform);
	    pPicture->transform = 0;
	}
    }
    pPicture->serialNumber |= GC_CHANGE_SERIAL_BIT;

    if (pPicture->pDrawable != NULL) {
	int result;
	PictureScreenPtr ps = GetPictureScreen(pPicture->pDrawable->pScreen);

	result = (*ps->ChangePictureTransform) (pPicture, transform);

	return result;
    }

    return Success;
}

void
CopyPicture (PicturePtr	pSrc,
	     Mask	mask,
	     PicturePtr	pDst)
{
    PictureScreenPtr ps = GetPictureScreen(pSrc->pDrawable->pScreen);
    Mask origMask = mask;

    pDst->serialNumber |= GC_CHANGE_SERIAL_BIT;
    pDst->stateChanges |= mask;

    while (mask) {
	Mask bit = lowbit(mask);

	switch (bit)
	{
	case CPRepeat:
	    pDst->repeat = pSrc->repeat;
	    pDst->repeatType = pSrc->repeatType;
	    break;
	case CPAlphaMap:
	    if (pSrc->alphaMap && pSrc->alphaMap->pDrawable->type == DRAWABLE_PIXMAP)
		pSrc->alphaMap->refcnt++;
	    if (pDst->alphaMap)
		FreePicture ((pointer) pDst->alphaMap, (XID) 0);
	    pDst->alphaMap = pSrc->alphaMap;
	    break;
	case CPAlphaXOrigin:
	    pDst->alphaOrigin.x = pSrc->alphaOrigin.x;
	    break;
	case CPAlphaYOrigin:
	    pDst->alphaOrigin.y = pSrc->alphaOrigin.y;
	    break;
	case CPClipXOrigin:
	    pDst->clipOrigin.x = pSrc->clipOrigin.x;
	    break;
	case CPClipYOrigin:
	    pDst->clipOrigin.y = pSrc->clipOrigin.y;
	    break;
	case CPClipMask:
	    switch (pSrc->clientClipType) {
	    case CT_NONE:
		(*ps->ChangePictureClip)(pDst, CT_NONE, NULL, 0);
		break;
	    case CT_REGION:
		if (!pSrc->clientClip) {
		    (*ps->ChangePictureClip)(pDst, CT_NONE, NULL, 0);
		} else {
		    RegionPtr clientClip;
		    RegionPtr srcClientClip = (RegionPtr)pSrc->clientClip;

		    clientClip = REGION_CREATE(pSrc->pDrawable->pScreen,
			REGION_EXTENTS(pSrc->pDrawable->pScreen, srcClientClip),
			REGION_NUM_RECTS(srcClientClip));
		    (*ps->ChangePictureClip)(pDst, CT_REGION, clientClip, 0);
		}
		break;
	    default:
		/* XXX: CT_PIXMAP unimplemented */
		break;
	    }
	    break;
	case CPGraphicsExposure:
	    pDst->graphicsExposures = pSrc->graphicsExposures;
	    break;
	case CPPolyEdge:
	    pDst->polyEdge = pSrc->polyEdge;
	    break;
	case CPPolyMode:
	    pDst->polyMode = pSrc->polyMode;
	    break;
	case CPDither:
	    pDst->dither = pSrc->dither;
	    break;
	case CPComponentAlpha:
	    pDst->componentAlpha = pSrc->componentAlpha;
	    break;
	}
	mask &= ~bit;
    }

    (*ps->ChangePicture)(pDst, origMask);
}

static void
ValidateOnePicture (PicturePtr pPicture)
{
    if (pPicture->pDrawable && pPicture->serialNumber != pPicture->pDrawable->serialNumber)
    {
	PictureScreenPtr    ps = GetPictureScreen(pPicture->pDrawable->pScreen);

	(*ps->ValidatePicture) (pPicture, pPicture->stateChanges);
	pPicture->stateChanges = 0;
	pPicture->serialNumber = pPicture->pDrawable->serialNumber;
    }
}

void
ValidatePicture(PicturePtr pPicture)
{
    ValidateOnePicture (pPicture);
    if (pPicture->alphaMap)
	ValidateOnePicture (pPicture->alphaMap);
}

int
FreePicture (pointer	value,
	     XID	pid)
{
    PicturePtr	pPicture = (PicturePtr) value;

    if (--pPicture->refcnt == 0)
    {
	if (pPicture->transform)
	    xfree (pPicture->transform);
        if (!pPicture->pDrawable) {
            if (pPicture->pSourcePict) {
                if (pPicture->pSourcePict->type != SourcePictTypeSolidFill)
                    xfree(pPicture->pSourcePict->linear.stops);
                xfree(pPicture->pSourcePict);
            }
        } else {
            ScreenPtr	    pScreen = pPicture->pDrawable->pScreen;
            PictureScreenPtr    ps = GetPictureScreen(pScreen);
	
            if (pPicture->alphaMap)
                FreePicture ((pointer) pPicture->alphaMap, (XID) 0);
            (*ps->DestroyPicture) (pPicture);
            (*ps->DestroyPictureClip) (pPicture);
            if (pPicture->pDrawable->type == DRAWABLE_WINDOW)
            {
                WindowPtr	pWindow = (WindowPtr) pPicture->pDrawable;
                PicturePtr	*pPrev;

                for (pPrev = (PicturePtr *) &((pWindow)->devPrivates[PictureWindowPrivateIndex].ptr);
                     *pPrev;
                     pPrev = &(*pPrev)->pNext)
                {
                    if (*pPrev == pPicture)
                    {
                        *pPrev = pPicture->pNext;
                        break;
                    }
                }
            }
            else if (pPicture->pDrawable->type == DRAWABLE_PIXMAP)
            {
                (*pScreen->DestroyPixmap) ((PixmapPtr)pPicture->pDrawable);
            }
        }
	xfree (pPicture);
    }
    return Success;
}

int
FreePictFormat (pointer	pPictFormat,
		XID     pid)
{
    return Success;
}

/**
 * ReduceCompositeOp is used to choose simpler ops for cases where alpha
 * channels are always one and so math on the alpha channel per pixel becomes
 * unnecessary.  It may also avoid destination reads sometimes if apps aren't
 * being careful to avoid these cases.
 */
static Bool
ReduceCompositeOp (CARD8 op, PicturePtr pSrc, PicturePtr pMask, PicturePtr pDst)
{
    Bool no_src_alpha, no_dst_alpha;

    no_src_alpha = PICT_FORMAT_COLOR(pSrc->format) &&
                   PICT_FORMAT_A(pSrc->format) == 0 &&
                   pSrc->alphaMap == NULL &&
                   pMask == NULL;
    no_dst_alpha = PICT_FORMAT_COLOR(pDst->format) &&
                   PICT_FORMAT_A(pDst->format) == 0 &&
                   pDst->alphaMap == NULL;

    /* TODO, maybe: Conjoint and Disjoint op reductions? */
 
    /* Deal with simplifications where the source alpha is always 1. */
    if (no_src_alpha)
    {
	switch (op) {
	case PictOpOver:
	    op = PictOpSrc;
	    break;
	case PictOpInReverse:
	    op = PictOpDst;
	    break;
	case PictOpOutReverse:
	    op = PictOpClear;
	    break;
	case PictOpAtop:
	    op = PictOpIn;
	    break;
	case PictOpAtopReverse:
	    op = PictOpOverReverse;
	    break;
	case PictOpXor:
	    op = PictOpOut;
	    break;
	default:
	    break;
	}
    }

    /* Deal with simplifications when the destination alpha is always 1 */
    if (no_dst_alpha)
    {
	switch (op) {
	case PictOpOverReverse:
	    op = PictOpDst;
	    break;
	case PictOpIn:
	    op = PictOpSrc;
	    break;
	case PictOpOut:
	    op = PictOpClear;
	    break;
	case PictOpAtop:
	    op = PictOpOver;
	    break;
	case PictOpXor:
	    op = PictOpOutReverse;
	    break;
	default:
	    break;
	}
    }

    /* Reduce some con/disjoint ops to the basic names. */
    switch (op) {
    case PictOpDisjointClear:
    case PictOpConjointClear:
	op = PictOpClear;
	break;
    case PictOpDisjointSrc:
    case PictOpConjointSrc:
	op = PictOpSrc;
	break;
    case PictOpDisjointDst:
    case PictOpConjointDst:
	op = PictOpDst;
	break;
    default:
	break;
    }

    return op;
}

void
CompositePicture (CARD8		op,
		  PicturePtr	pSrc,
		  PicturePtr	pMask,
		  PicturePtr	pDst,
		  INT16		xSrc,
		  INT16		ySrc,
		  INT16		xMask,
		  INT16		yMask,
		  INT16		xDst,
		  INT16		yDst,
		  CARD16	width,
		  CARD16	height)
{
    PictureScreenPtr	ps = GetPictureScreen(pDst->pDrawable->pScreen);
    
    ValidatePicture (pSrc);
    if (pMask)
	ValidatePicture (pMask);
    ValidatePicture (pDst);

    op = ReduceCompositeOp (op, pSrc, pMask, pDst);
    if (op == PictOpDst)
	return;

    (*ps->Composite) (op,
		       pSrc,
		       pMask,
		       pDst,
		       xSrc,
		       ySrc,
		       xMask,
		       yMask,
		       xDst,
		       yDst,
		       width,
		       height);
}

void
CompositeGlyphs (CARD8		op,
		 PicturePtr	pSrc,
		 PicturePtr	pDst,
		 PictFormatPtr	maskFormat,
		 INT16		xSrc,
		 INT16		ySrc,
		 int		nlist,
		 GlyphListPtr	lists,
		 GlyphPtr	*glyphs)
{
    PictureScreenPtr	ps = GetPictureScreen(pDst->pDrawable->pScreen);
    
    ValidatePicture (pSrc);
    ValidatePicture (pDst);
    (*ps->Glyphs) (op, pSrc, pDst, maskFormat, xSrc, ySrc, nlist, lists, glyphs);
}

void
CompositeRects (CARD8		op,
		PicturePtr	pDst,
		xRenderColor	*color,
		int		nRect,
		xRectangle      *rects)
{
    PictureScreenPtr	ps = GetPictureScreen(pDst->pDrawable->pScreen);
    
    ValidatePicture (pDst);
    (*ps->CompositeRects) (op, pDst, color, nRect, rects);
}

void
CompositeTrapezoids (CARD8	    op,
		     PicturePtr	    pSrc,
		     PicturePtr	    pDst,
		     PictFormatPtr  maskFormat,
		     INT16	    xSrc,
		     INT16	    ySrc,
		     int	    ntrap,
		     xTrapezoid	    *traps)
{
    PictureScreenPtr	ps = GetPictureScreen(pDst->pDrawable->pScreen);
    
    ValidatePicture (pSrc);
    ValidatePicture (pDst);
    (*ps->Trapezoids) (op, pSrc, pDst, maskFormat, xSrc, ySrc, ntrap, traps);
}

void
CompositeTriangles (CARD8	    op,
		    PicturePtr	    pSrc,
		    PicturePtr	    pDst,
		    PictFormatPtr   maskFormat,
		    INT16	    xSrc,
		    INT16	    ySrc,
		    int		    ntriangles,
		    xTriangle	    *triangles)
{
    PictureScreenPtr	ps = GetPictureScreen(pDst->pDrawable->pScreen);
    
    ValidatePicture (pSrc);
    ValidatePicture (pDst);
    (*ps->Triangles) (op, pSrc, pDst, maskFormat, xSrc, ySrc, ntriangles, triangles);
}

void
CompositeTriStrip (CARD8	    op,
		   PicturePtr	    pSrc,
		   PicturePtr	    pDst,
		   PictFormatPtr    maskFormat,
		   INT16	    xSrc,
		   INT16	    ySrc,
		   int		    npoints,
		   xPointFixed	    *points)
{
    PictureScreenPtr	ps = GetPictureScreen(pDst->pDrawable->pScreen);
    
    ValidatePicture (pSrc);
    ValidatePicture (pDst);
    (*ps->TriStrip) (op, pSrc, pDst, maskFormat, xSrc, ySrc, npoints, points);
}

void
CompositeTriFan (CARD8		op,
		 PicturePtr	pSrc,
		 PicturePtr	pDst,
		 PictFormatPtr	maskFormat,
		 INT16		xSrc,
		 INT16		ySrc,
		 int		npoints,
		 xPointFixed	*points)
{
    PictureScreenPtr	ps = GetPictureScreen(pDst->pDrawable->pScreen);
    
    ValidatePicture (pSrc);
    ValidatePicture (pDst);
    (*ps->TriFan) (op, pSrc, pDst, maskFormat, xSrc, ySrc, npoints, points);
}

void
AddTraps (PicturePtr	pPicture,
	  INT16		xOff,
	  INT16		yOff,
	  int		ntrap,
	  xTrap		*traps)
{
    PictureScreenPtr	ps = GetPictureScreen(pPicture->pDrawable->pScreen);
    
    ValidatePicture (pPicture);
    (*ps->AddTraps) (pPicture, xOff, yOff, ntrap, traps);
}

#define MAX_FIXED_48_16	    ((xFixed_48_16) 0x7fffffff)
#define MIN_FIXED_48_16	    (-((xFixed_48_16) 1 << 31))

_X_EXPORT Bool
PictureTransformPoint3d (PictTransformPtr transform,
                         PictVectorPtr	vector)
{
    PictVector	    result;
    int		    i, j;
    xFixed_32_32    partial;
    xFixed_48_16    v;

    for (j = 0; j < 3; j++)
    {
	v = 0;
	for (i = 0; i < 3; i++)
	{
	    partial = ((xFixed_48_16) transform->matrix[j][i] *
		       (xFixed_48_16) vector->vector[i]);
	    v += partial >> 16;
	}
	if (v > MAX_FIXED_48_16 || v < MIN_FIXED_48_16)
	    return FALSE;
	result.vector[j] = (xFixed) v;
    }
    if (!result.vector[2])
	return FALSE;
    *vector = result;
    return TRUE;
}


_X_EXPORT Bool
PictureTransformPoint (PictTransformPtr transform,
		       PictVectorPtr	vector)
{
    PictVector	    result;
    int		    i, j;
    xFixed_32_32    partial;
    xFixed_48_16    v;

    for (j = 0; j < 3; j++)
    {
	v = 0;
	for (i = 0; i < 3; i++)
	{
	    partial = ((xFixed_48_16) transform->matrix[j][i] * 
		       (xFixed_48_16) vector->vector[i]);
	    v += partial >> 16;
	}
	if (v > MAX_FIXED_48_16 || v < MIN_FIXED_48_16)
	    return FALSE;
	result.vector[j] = (xFixed) v;
    }
    if (!result.vector[2])
	return FALSE;
    for (j = 0; j < 2; j++)
    {
	partial = (xFixed_48_16) result.vector[j] << 16;
	v = partial / result.vector[2];
	if (v > MAX_FIXED_48_16 || v < MIN_FIXED_48_16)
	    return FALSE;
	vector->vector[j] = (xFixed) v;
    }
    vector->vector[2] = xFixed1;
    return TRUE;
}
