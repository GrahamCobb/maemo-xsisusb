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

#include <string.h>

#include "fb.h"

#ifdef RENDER

#include "picturestr.h"
#include "mipict.h"
#include "fbpict.h"
#include "fbmmx.h"

CARD32
fbOver (CARD32 x, CARD32 y)
{
    CARD16  a = ~x >> 24;
    CARD16  t;
    CARD32  m,n,o,p;

    m = FbOverU(x,y,0,a,t);
    n = FbOverU(x,y,8,a,t);
    o = FbOverU(x,y,16,a,t);
    p = FbOverU(x,y,24,a,t);
    return m|n|o|p;
}

CARD32
fbOver24 (CARD32 x, CARD32 y)
{
    CARD16  a = ~x >> 24;
    CARD16  t;
    CARD32  m,n,o;

    m = FbOverU(x,y,0,a,t);
    n = FbOverU(x,y,8,a,t);
    o = FbOverU(x,y,16,a,t);
    return m|n|o;
}

CARD32
fbIn (CARD32 x, CARD8 y)
{
    CARD16  a = y;
    CARD16  t;
    CARD32  m,n,o,p;

    m = FbInU(x,0,a,t);
    n = FbInU(x,8,a,t);
    o = FbInU(x,16,a,t);
    p = FbInU(x,24,a,t);
    return m|n|o|p;
}

/*
 * Naming convention:
 *
 *  opSRCxMASKxDST
 */

void
fbCompositeSolidMask_nx8x8888 (FbComposeData *params)
{
    CARD32	src, srca;
    CARD32	*dstLine, *dst, d, dstMask;
    CARD8	*maskLine, *mask, m;
    FbStride	dstStride, maskStride;
    CARD16	w, h;

    fbComposeGetSolid(params->src, src, params->dest->format);

    dstMask = FbFullMask (params->dest->pDrawable->depth);
    srca = src >> 24;
    if (src == 0)
	return;

    fbComposeGetStart (params->dest, params->xDest, params->yDest, CARD32,
                       dstStride, dstLine, 1);
    fbComposeGetStart (params->mask, params->xMask, params->yMask, CARD8,
                       maskStride, maskLine, 1);

    h = params->height;
    while (h--)
    {
	dst = dstLine;
	dstLine += dstStride;
	mask = maskLine;
	maskLine += maskStride;
	w = params->width;

	while (w--)
	{
	    m = READ(mask++);
	    if (m == 0xff)
	    {
		if (srca == 0xff)
		    WRITE(dst, src & dstMask);
		else
		    WRITE(dst, fbOver (src, READ(dst)) & dstMask);
	    }
	    else if (m)
	    {
		d = fbIn (src, m);
		WRITE(dst, fbOver (d, READ(dst)) & dstMask);
	    }
	    dst++;
	}
    }

    fbFinishAccess (params->mask->pDrawable);
    fbFinishAccess (params->dest->pDrawable);
}

void
fbCompositeSolidMask_nx8888x8888C (FbComposeData *params)
{
    CARD32	src, srca;
    CARD32	*dstLine, *dst, d, dstMask;
    CARD32	*maskLine, *mask, ma;
    FbStride	dstStride, maskStride;
    CARD16	w, h;
    CARD32	m, n, o, p;

    fbComposeGetSolid(params->src, src, params->dest->format);

    dstMask = FbFullMask (params->dest->pDrawable->depth);
    srca = src >> 24;
    if (src == 0)
	return;

    fbComposeGetStart (params->dest, params->xDest, params->yDest, CARD32,
                       dstStride, dstLine, 1);
    fbComposeGetStart (params->mask, params->xMask, params->yMask, CARD32,
                       maskStride, maskLine, 1);

    h = params->height;
    while (h--)
    {
	dst = dstLine;
	dstLine += dstStride;
	mask = maskLine;
	maskLine += maskStride;
	w = params->width;

	while (w--)
	{
	    ma = READ(mask++);
	    if (ma == 0xffffffff)
	    {
		if (srca == 0xff)
		    WRITE(dst, src & dstMask);
		else
		    WRITE(dst, fbOver (src, READ(dst)) & dstMask);
	    }
	    else if (ma)
	    {
		d = READ(dst);
#define FbInOverC(src,srca,msk,dst,i,result) { \
    CARD16  __a = FbGet8(msk,i); \
    CARD32  __t, __ta; \
    CARD32  __i; \
    __t = FbIntMult (FbGet8(src,i), __a,__i); \
    __ta = (CARD8) ~FbIntMult (srca, __a,__i); \
    __t = __t + FbIntMult(FbGet8(dst,i),__ta,__i); \
    __t = (CARD32) (CARD8) (__t | (-(__t >> 8))); \
    result = __t << (i); \
}
		FbInOverC (src, srca, ma, d, 0, m);
		FbInOverC (src, srca, ma, d, 8, n);
		FbInOverC (src, srca, ma, d, 16, o);
		FbInOverC (src, srca, ma, d, 24, p);
		WRITE(dst, m|n|o|p);
	    }
	    dst++;
	}
    }

    fbFinishAccess (params->mask->pDrawable);
    fbFinishAccess (params->dest->pDrawable);
}

void
fbCompositeSolidMask_nx8x0888 (FbComposeData *params)
{
    CARD32	src, srca;
    CARD8	*dstLine, *dst;
    CARD32	d;
    CARD8	*maskLine, *mask, m;
    FbStride	dstStride, maskStride;
    CARD16	w, h;

    fbComposeGetSolid(params->src, src, params->dest->format);

    srca = src >> 24;
    if (src == 0)
	return;

    fbComposeGetStart (params->dest, params->xDest, params->yDest, CARD8,
                       dstStride, dstLine, 3);
    fbComposeGetStart (params->mask, params->xMask, params->yMask, CARD8,
                       maskStride, maskLine, 1);

    h = params->height;
    while (h--)
    {
	dst = dstLine;
	dstLine += dstStride;
	mask = maskLine;
	maskLine += maskStride;
	w = params->width;

	while (w--)
	{
	    m = READ(mask++);
	    if (m == 0xff)
	    {
		if (srca == 0xff)
		    d = src;
		else
		{
		    d = Fetch24(dst);
		    d = fbOver24 (src, d);
		}
		Store24(dst,d);
	    }
	    else if (m)
	    {
		d = fbOver24 (fbIn(src,m), Fetch24(dst));
		Store24(dst,d);
	    }
	    dst += 3;
	}
    }

    fbFinishAccess (params->mask->pDrawable);
    fbFinishAccess (params->dest->pDrawable);
}

void
fbCompositeSolidMask_nx8x0565 (FbComposeData *params)
{
    CARD32	src, srca;
    CARD16	*dstLine, *dst;
    CARD32	d;
    CARD8	*maskLine, *mask, m;
    FbStride	dstStride, maskStride;
    CARD16	w, h;

    fbComposeGetSolid(params->src, src, params->dest->format);

    srca = src >> 24;
    if (src == 0)
	return;

    fbComposeGetStart (params->dest, params->xDest, params->yDest, CARD16,
                       dstStride, dstLine, 1);
    fbComposeGetStart (params->mask, params->xMask, params->yMask, CARD8,
                       maskStride, maskLine, 1);

    h = params->height;
    while (h--)
    {
	dst = dstLine;
	dstLine += dstStride;
	mask = maskLine;
	maskLine += maskStride;
	w = params->width;

	while (w--)
	{
	    m = READ(mask++);
	    if (m == 0xff)
	    {
		if (srca == 0xff)
		    d = src;
		else
		{
		    d = READ(dst);
		    d = fbOver24 (src, cvt0565to8888(d));
		}
		WRITE(dst, cvt8888to0565(d));
	    }
	    else if (m)
	    {
		d = READ(dst);
		d = fbOver24 (fbIn(src,m), cvt0565to8888(d));
		WRITE(dst, cvt8888to0565(d));
	    }
	    dst++;
	}
    }

    fbFinishAccess (params->mask->pDrawable);
    fbFinishAccess (params->dest->pDrawable);
}

void
fbCompositeSolidMask_nx8888x0565C (FbComposeData *params)
{
    CARD32	src, srca;
    CARD16	src16;
    CARD16	*dstLine, *dst;
    CARD32	d;
    CARD32	*maskLine, *mask, ma;
    FbStride	dstStride, maskStride;
    CARD16	w, h;
    CARD32	m, n, o;

    fbComposeGetSolid (params->src, src, params->dest->format);

    srca = src >> 24;
    if (src == 0)
	return;

    src16 = cvt8888to0565(src);

    fbComposeGetStart (params->dest, params->xDest, params->yDest, CARD16,
                       dstStride, dstLine, 1);
    fbComposeGetStart (params->mask, params->xMask, params->yMask, CARD32,
                       maskStride, maskLine, 1);

    h = params->height;
    while (h--)
    {
	dst = dstLine;
	dstLine += dstStride;
	mask = maskLine;
	maskLine += maskStride;
	w = params->width;

	while (w--)
	{
	    ma = READ(mask++);
	    if (ma == 0xffffffff)
	    {
		if (srca == 0xff)
		{
		    WRITE(dst, src16);
		}
		else
		{
		    d = READ(dst);
		    d = fbOver24 (src, cvt0565to8888(d));
		    WRITE(dst, cvt8888to0565(d));
		}
	    }
	    else if (ma)
	    {
		d = READ(dst);
		d = cvt0565to8888(d);
		FbInOverC (src, srca, ma, d, 0, m);
		FbInOverC (src, srca, ma, d, 8, n);
		FbInOverC (src, srca, ma, d, 16, o);
		d = m|n|o;
		WRITE(dst, cvt8888to0565(d));
	    }
	    dst++;
	}
    }

    fbFinishAccess (params->mask->pDrawable);
    fbFinishAccess (params->dest->pDrawable);
}

void
fbCompositeSrc_8888x8888 (FbComposeData *params)
{
    CARD32	*dstLine, *dst, dstMask;
    CARD32	*srcLine, *src, s;
    FbStride	dstStride, srcStride;
    CARD8	a;
    CARD16	w, h;

    fbComposeGetStart (params->dest, params->xDest, params->yDest, CARD32,
                       dstStride, dstLine, 1);
    fbComposeGetStart (params->src, params->xSrc, params->ySrc, CARD32,
                       srcStride, srcLine, 1);

    dstMask = FbFullMask (params->dest->pDrawable->depth);

    h = params->height;
    while (h--)
    {
	dst = dstLine;
	dstLine += dstStride;
	src = srcLine;
	srcLine += srcStride;
	w = params->width;

	while (w--)
	{
	    s = READ(src++);
	    a = s >> 24;
	    if (a == 0xff)
		WRITE(dst, s & dstMask);
	    else if (a)
		WRITE(dst, fbOver (s, READ(dst)) & dstMask);
	    dst++;
	}
    }

    fbFinishAccess (params->src->pDrawable);
    fbFinishAccess (params->dest->pDrawable);
}

void
fbCompositeSrc_8888x0888 (FbComposeData *params)
{
    CARD8	*dstLine, *dst;
    CARD32	d;
    CARD32	*srcLine, *src, s;
    CARD8	a;
    FbStride	dstStride, srcStride;
    CARD16	w, h;

    fbComposeGetStart (params->dest, params->xDest, params->yDest, CARD8,
                       dstStride, dstLine, 3);
    fbComposeGetStart (params->src, params->xSrc, params->ySrc, CARD32,
                       srcStride, srcLine, 1);

    h = params->height;
    while (h--)
    {
	dst = dstLine;
	dstLine += dstStride;
	src = srcLine;
	srcLine += srcStride;
	w = params->width;

	while (w--)
	{
	    s = READ(src++);
	    a = s >> 24;
	    if (a)
	    {
		if (a == 0xff)
		    d = s;
		else
		    d = fbOver24 (s, Fetch24(dst));
		Store24(dst,d);
	    }
	    dst += 3;
	}
    }

    fbFinishAccess (params->src->pDrawable);
    fbFinishAccess (params->dest->pDrawable);
}

void
fbCompositeSrc_8888x0565 (FbComposeData *params)
{
    CARD16	*dstLine, *dst;
    CARD32	d;
    CARD32	*srcLine, *src, s;
    CARD8	a;
    FbStride	dstStride, srcStride;
    CARD16	w, h;

    fbComposeGetStart (params->src, params->xSrc, params->ySrc, CARD32,
                       srcStride, srcLine, 1);
    fbComposeGetStart (params->dest, params->xDest, params->yDest, CARD16,
                       dstStride, dstLine, 1);

    h = params->height;
    while (h--)
    {
	dst = dstLine;
	dstLine += dstStride;
	src = srcLine;
	srcLine += srcStride;
	w = params->width;

	while (w--)
	{
	    s = READ(src++);
	    a = s >> 24;
	    if (a)
	    {
		if (a == 0xff)
		    d = s;
		else
		{
		    d = READ(dst);
		    d = fbOver24 (s, cvt0565to8888(d));
		}
		WRITE(dst, cvt8888to0565(d));
	    }
	    dst++;
	}
    }

    fbFinishAccess (params->dest->pDrawable);
    fbFinishAccess (params->src->pDrawable);
}

void
fbCompositeSrc_0565x0565 (FbComposeData *params)
{
    CARD16	*dstLine, *dst;
    CARD16	*srcLine, *src;
    FbStride	dstStride, srcStride;
    CARD16	w, h;

    fbComposeGetStart (params->src, params->xSrc, params->ySrc, CARD16,
                       srcStride, srcLine, 1);

    fbComposeGetStart (params->dest, params->xDest, params->yDest, CARD16,
                       dstStride, dstLine, 1);

    h = params->height;
    while (h--)
    {
	dst = dstLine;
	dstLine += dstStride;
	src = srcLine;
	srcLine += srcStride;
	w = params->width;

	while (w--)
	    WRITE(dst, READ(src++));
    }

    fbFinishAccess (params->dest->pDrawable);
    fbFinishAccess (params->src->pDrawable);
}

void
fbCompositeSrcAdd_8000x8000 (FbComposeData *params)
{
    CARD8	*dstLine, *dst;
    CARD8	*srcLine, *src;
    FbStride	dstStride, srcStride;
    CARD16	w, h;
    CARD8	s, d;
    CARD16	t;

    fbComposeGetStart (params->src, params->xSrc, params->ySrc, CARD8,
                       srcStride, srcLine, 1);
    fbComposeGetStart (params->dest, params->xDest, params->yDest, CARD8,
                       dstStride, dstLine, 1);

    h = params->height;
    while (h--)
    {
	dst = dstLine;
	dstLine += dstStride;
	src = srcLine;
	srcLine += srcStride;
	w = params->width;

	while (w--)
	{
	    s = READ(src++);
	    if (s)
	    {
		if (s != 0xff)
		{
		    d = READ(dst);
		    t = d + s;
		    s = t | (0 - (t >> 8));
		}
		WRITE(dst, s);
	    }
	    dst++;
	}
    }

    fbFinishAccess (params->dest->pDrawable);
    fbFinishAccess (params->src->pDrawable);
}

void
fbCompositeSrcAdd_8888x8888 (FbComposeData *params)
{
    CARD32	*dstLine, *dst;
    CARD32	*srcLine, *src;
    FbStride	dstStride, srcStride;
    CARD16	w, h;
    CARD32	s, d;
    CARD16	t;
    CARD32	m,n,o,p;

    fbComposeGetStart (params->src, params->xSrc, params->ySrc, CARD32,
                       srcStride, srcLine, 1);
    fbComposeGetStart (params->dest, params->xDest, params->yDest, CARD32,
                       dstStride, dstLine, 1);

    h = params->height;
    while (h--)
    {
	dst = dstLine;
	dstLine += dstStride;
	src = srcLine;
	srcLine += srcStride;
	w = params->width;

	while (w--)
	{
	    s = READ(src++);
	    if (s)
	    {
		if (s != 0xffffffff)
		{
		    d = READ(dst);
		    if (d)
		    {
			m = FbAdd(s,d,0,t);
			n = FbAdd(s,d,8,t);
			o = FbAdd(s,d,16,t);
			p = FbAdd(s,d,24,t);
			s = m|n|o|p;
		    }
		}
		WRITE(dst, s);
	    }
	    dst++;
	}
    }

    fbFinishAccess (params->dest->pDrawable);
    fbFinishAccess (params->src->pDrawable);
}

void
fbCompositeSrcAdd_1000x1000 (FbComposeData *params)
{
    FbBits	*dstBits, *srcBits;
    FbStride	dstStride, srcStride;
    int		dstBpp, srcBpp;
    int		dstXoff, dstYoff;
    int		srcXoff, srcYoff;

    fbGetDrawable(params->src->pDrawable, srcBits, srcStride, srcBpp,
                  srcXoff, srcYoff);

    fbGetDrawable(params->dest->pDrawable, dstBits, dstStride, dstBpp,
                  dstXoff, dstYoff);

    fbBlt (srcBits + srcStride * (params->ySrc + srcYoff),
	   srcStride,
	   params->xSrc + srcXoff,

	   dstBits + dstStride * (params->yDest + dstYoff),
	   dstStride,
	   params->xDest + dstXoff,

	   params->width,
	   params->height,

	   GXor,
	   FB_ALLONES,
	   srcBpp,

	   FALSE,
	   FALSE);

    fbFinishAccess(params->dest->pDrawable);
    fbFinishAccess(params->src->pDrawable);
}

void
fbCompositeSolidMask_nx1xn (FbComposeData *params)
{
    FbBits	*dstBits;
    FbStip	*maskBits;
    FbStride	dstStride, maskStride;
    int		dstBpp, maskBpp;
    int		dstXoff, dstYoff;
    int		maskXoff, maskYoff;
    FbBits	src;

    fbComposeGetSolid(params->src, src, params->dest->format);

    if ((src & 0xff000000) != 0xff000000)
    {
	fbCompositeGeneral (params);
	return;
    }
    fbGetStipDrawable (params->mask->pDrawable, maskBits, maskStride,
                       maskBpp, maskXoff, maskYoff);
    fbGetDrawable (params->dest->pDrawable, dstBits, dstStride, dstBpp,
                   dstXoff, dstYoff);

    switch (dstBpp) {
    case 32:
	break;
    case 24:
	break;
    case 16:
	src = cvt8888to0565(src);
	break;
    }

    src = fbReplicatePixel (src, dstBpp);

    fbBltOne (maskBits + maskStride * (params->yMask + maskYoff),
	      maskStride,
	      params->xMask + maskXoff,

	      dstBits + dstStride * (params->yDest + dstYoff),
	      dstStride,
	      (params->xDest + dstXoff) * dstBpp,
	      dstBpp,

	      params->width * dstBpp,
	      params->height,

	      0x0,
	      src,
	      FB_ALLONES,
	      0x0);

    fbFinishAccess (params->dest->pDrawable);
    fbFinishAccess (params->mask->pDrawable);
}

# define mod(a,b)	((b) == 1 ? 0 : (a) >= 0 ? (a) % (b) : (b) - (-a) % (b))

void
fbComposite (CARD8      op,
	     PicturePtr pSrc,
	     PicturePtr pMask,
	     PicturePtr pDst,
	     INT16      xSrc,
	     INT16      ySrc,
	     INT16      xMask,
	     INT16      yMask,
	     INT16      xDst,
	     INT16      yDst,
	     CARD16     width,
	     CARD16     height)
{
    RegionRec	    region;
    int		    n;
    BoxPtr	    pbox;
    CompositeFunc   func = NULL;
    FbComposeData   params;
    Bool	    srcRepeat = pSrc->pDrawable && pSrc->repeat;
    Bool	    maskRepeat = FALSE;
    Bool	    srcAlphaMap = pSrc->alphaMap != 0;
    Bool	    maskAlphaMap = FALSE;
    Bool	    dstAlphaMap = pDst->alphaMap != 0;
    int		    w, h;

#ifdef USE_MMX
    static Bool mmx_setup = FALSE;
    if (!mmx_setup) {
        fbComposeSetupMMX();
        mmx_setup = TRUE;
    }
#endif
        
    xDst += pDst->pDrawable->x;
    yDst += pDst->pDrawable->y;
    if (pSrc->pDrawable) {
        xSrc += pSrc->pDrawable->x;
        ySrc += pSrc->pDrawable->y;
    }
    if (pMask && pMask->pDrawable)
    {
	xMask += pMask->pDrawable->x;
	yMask += pMask->pDrawable->y;
	maskRepeat = pMask->repeat == RepeatNormal;
	maskAlphaMap = pMask->alphaMap != 0;
    }

    if (pSrc->pDrawable && (!pMask || pMask->pDrawable)
        && !pSrc->transform && !(pMask && pMask->transform)
        && !maskAlphaMap && !srcAlphaMap && !dstAlphaMap
        && (pSrc->filter != PictFilterConvolution)
        && (!pMask || pMask->filter != PictFilterConvolution))
    switch (op) {
    case PictOpSrc:
#ifdef USE_MMX
	if (!pMask && pSrc->format == pDst->format &&
	    pSrc->format != PICT_a8 && pSrc->pDrawable != pDst->pDrawable)
	{
	    func = fbCompositeCopyAreammx;
	}
#endif
	break;
    case PictOpOver:
	if (pMask)
	{
	    if (fbCanGetSolid(pSrc) &&
		!maskRepeat)
	    {
		srcRepeat = FALSE;
		if (PICT_FORMAT_COLOR(pSrc->format)) {
		    switch (pMask->format) {
		    case PICT_a8:
			switch (pDst->format) {
			case PICT_r5g6b5:
			case PICT_b5g6r5:
#ifdef USE_MMX
			    if (fbHaveMMX())
				func = fbCompositeSolidMask_nx8x0565mmx;
			    else
#endif
				func = fbCompositeSolidMask_nx8x0565;
			    break;
			case PICT_r8g8b8:
			case PICT_b8g8r8:
			    func = fbCompositeSolidMask_nx8x0888;
			    break;
			case PICT_a8r8g8b8:
			case PICT_x8r8g8b8:
			case PICT_a8b8g8r8:
			case PICT_x8b8g8r8:
#ifdef USE_MMX
			    if (fbHaveMMX())
				func = fbCompositeSolidMask_nx8x8888mmx;
			    else
#endif
				func = fbCompositeSolidMask_nx8x8888;
			    break;
			default:
			    break;
			}
			break;
		    case PICT_a8r8g8b8:
			if (pMask->componentAlpha) {
			    switch (pDst->format) {
			    case PICT_a8r8g8b8:
			    case PICT_x8r8g8b8:
#ifdef USE_MMX
				if (fbHaveMMX())
				    func = fbCompositeSolidMask_nx8888x8888Cmmx;
				else
#endif
				    func = fbCompositeSolidMask_nx8888x8888C;
				break;
			    case PICT_r5g6b5:
#ifdef USE_MMX
				if (fbHaveMMX())
				    func = fbCompositeSolidMask_nx8888x0565Cmmx;
				else
#endif
				    func = fbCompositeSolidMask_nx8888x0565C;
				break;
			    default:
				break;
			    }
			}
			break;
		    case PICT_a8b8g8r8:
			if (pMask->componentAlpha) {
			    switch (pDst->format) {
			    case PICT_a8b8g8r8:
			    case PICT_x8b8g8r8:
#ifdef USE_MMX
				if (fbHaveMMX())
				    func = fbCompositeSolidMask_nx8888x8888Cmmx;
				else
#endif
				    func = fbCompositeSolidMask_nx8888x8888C;
				break;
			    case PICT_b5g6r5:
#ifdef USE_MMX
				if (fbHaveMMX())
				    func = fbCompositeSolidMask_nx8888x0565Cmmx;
				else
#endif
				    func = fbCompositeSolidMask_nx8888x0565C;
				break;
			    default:
				break;
			    }
			}
			break;
		    case PICT_a1:
			switch (pDst->format) {
			case PICT_r5g6b5:
			case PICT_b5g6r5:
			case PICT_r8g8b8:
			case PICT_b8g8r8:
			case PICT_a8r8g8b8:
			case PICT_x8r8g8b8:
			case PICT_a8b8g8r8:
			case PICT_x8b8g8r8:
			    func = fbCompositeSolidMask_nx1xn;
			    break;
			default:
			    break;
			}
			break;
		    default:
			break;
		    }
		default:
		    break;
		}
	    }
	    else if (! srcRepeat) /* has mask and non-repeating source */
	    {
		if (pSrc->pDrawable == pMask->pDrawable &&
		    xSrc == xMask && ySrc == yMask &&
		    !pMask->componentAlpha && !maskRepeat)
		{
		    /* source == mask: non-premultiplied data */
		    switch (pSrc->format) {
		    case PICT_x8b8g8r8:
			switch (pMask->format) {
			case PICT_a8r8g8b8:
			case PICT_a8b8g8r8:
			    switch (pDst->format) {
			    case PICT_a8r8g8b8:
			    case PICT_x8r8g8b8:
#ifdef USE_MMX
				if (fbHaveMMX())
				    func = fbCompositeSrc_8888RevNPx8888mmx;
#endif
				break;
			    case PICT_r5g6b5:
#ifdef USE_MMX
				if (fbHaveMMX())
				    func = fbCompositeSrc_8888RevNPx0565mmx;
#endif
				break;
			    default:
				break;
			    }
			    break;
			default:
			    break;
			}
			break;
		    case PICT_x8r8g8b8:
			switch (pMask->format) {
			case PICT_a8r8g8b8:
			case PICT_a8b8g8r8:
			    switch (pDst->format) {
			    case PICT_a8b8g8r8:
			    case PICT_x8b8g8r8:
#ifdef USE_MMX
				if (fbHaveMMX())
				    func = fbCompositeSrc_8888RevNPx8888mmx;
#endif
				break;
			    case PICT_r5g6b5:
#ifdef USE_MMX
				if (fbHaveMMX())
				    func = fbCompositeSrc_8888RevNPx0565mmx;
#endif
				break;
			    default:
				break;
			    }
			    break;
			default:
			    break;
			}
			break;
		    default:
			break;
		    }
		    break;
		}
		else
		{
		    /* non-repeating source, repeating mask => translucent window */
		    if (fbCanGetSolid(pMask))
		    {
			if (pSrc->format == PICT_x8r8g8b8 &&
			    pDst->format == PICT_x8r8g8b8 &&
			    pMask->format == PICT_a8)
			{
#ifdef USE_MMX
			    if (fbHaveMMX())
				func = fbCompositeSrc_8888x8x8888mmx;
#endif
			}
		    }
		}
	    }
	}
	else /* no mask */
	{
	    if (fbCanGetSolid(pSrc))
	    {
		/* no mask and repeating source */
		switch (pSrc->format) {
		case PICT_a8r8g8b8:
		    switch (pDst->format) {
		    case PICT_a8r8g8b8:
		    case PICT_x8r8g8b8:
#ifdef USE_MMX
			if (fbHaveMMX())
			{
			    srcRepeat = FALSE;
			    func = fbCompositeSolid_nx8888mmx;
			}
#endif
			break;
		    case PICT_r5g6b5:
#ifdef USE_MMX
			if (fbHaveMMX())
			{
			    srcRepeat = FALSE;
			    func = fbCompositeSolid_nx0565mmx;
			}
#endif
			break;
		    default:
			break;
		    }
		    break;
		default:
		    break;
		}
	    }
	    else if (! srcRepeat)
	    {
		switch (pSrc->format) {
		case PICT_a8r8g8b8:
		    switch (pDst->format) {
		    case PICT_a8r8g8b8:
		    case PICT_x8r8g8b8:
#ifdef USE_MMX
			if (fbHaveMMX())
			    func = fbCompositeSrc_8888x8888mmx;
			else
#endif
			    func = fbCompositeSrc_8888x8888;
			break;
		    case PICT_r8g8b8:
			func = fbCompositeSrc_8888x0888;
			break;
		    case PICT_r5g6b5:
			    func = fbCompositeSrc_8888x0565;
			break;
		    default:
			break;
		    }
		    break;
		case PICT_x8r8g8b8:
		    switch (pDst->format) {
		    case PICT_a8r8g8b8:
		    case PICT_x8r8g8b8:
#ifdef USE_MMX
			if (fbHaveMMX())
			    func = fbCompositeCopyAreammx;
#endif
			break;
		    default:
			break;
		    }
		case PICT_x8b8g8r8:
		    switch (pDst->format) {
		    case PICT_a8b8g8r8:
		    case PICT_x8b8g8r8:
#ifdef USE_MMX
			if (fbHaveMMX())
			    func = fbCompositeCopyAreammx;
#endif
			break;
		    default:
			break;
		    }
		    break;
		case PICT_a8b8g8r8:
		    switch (pDst->format) {
		    case PICT_a8b8g8r8:
		    case PICT_x8b8g8r8:
#ifdef USE_MMX
			if (fbHaveMMX())
			    func = fbCompositeSrc_8888x8888mmx;
			else
#endif
			    func = fbCompositeSrc_8888x8888;
			break;
		    case PICT_b8g8r8:
			func = fbCompositeSrc_8888x0888;
			break;
		    case PICT_b5g6r5:
			    func = fbCompositeSrc_8888x0565;
			break;
		    default:
			break;
		    }
		    break;
		case PICT_r5g6b5:
		    switch (pDst->format) {
		    case PICT_r5g6b5:
			func = fbCompositeSrc_0565x0565;
			break;
		    default:
			break;
		    }
		    break;
		case PICT_b5g6r5:
		    switch (pDst->format) {
		    case PICT_b5g6r5:
			func = fbCompositeSrc_0565x0565;
			break;
		    default:
			break;
		    }
		    break;
		default:
		    break;
		}
	    }
	}
	break;
    case PictOpAdd:
	if (pMask == 0)
	{
	    switch (pSrc->format) {
	    case PICT_a8r8g8b8:
		switch (pDst->format) {
		case PICT_a8r8g8b8:
#ifdef USE_MMX
		    if (fbHaveMMX())
			func = fbCompositeSrcAdd_8888x8888mmx;
		    else
#endif
			func = fbCompositeSrcAdd_8888x8888;
		    break;
		default:
		    break;
		}
		break;
	    case PICT_a8b8g8r8:
		switch (pDst->format) {
		case PICT_a8b8g8r8:
#ifdef USE_MMX
		    if (fbHaveMMX())
			func = fbCompositeSrcAdd_8888x8888mmx;
		    else
#endif
			func = fbCompositeSrcAdd_8888x8888;
		    break;
		default:
		    break;
		}
		break;
	    case PICT_a8:
		switch (pDst->format) {
		case PICT_a8:
#ifdef USE_MMX
		    if (fbHaveMMX())
			func = fbCompositeSrcAdd_8000x8000mmx;
		    else
#endif
			func = fbCompositeSrcAdd_8000x8000;
		    break;
		default:
		    break;
		}
		break;
	    case PICT_a1:
		switch (pDst->format) {
		case PICT_a1:
		    func = fbCompositeSrcAdd_1000x1000;
		    break;
		default:
		    break;
		}
		break;
	    default:
		break;
	    }
	}
	break;
    }

    params.op = op;
    params.src = pSrc;
    params.mask = pMask;
    params.dest = pDst;
    params.xSrc = xSrc;
    params.ySrc = ySrc;
    params.xMask = xMask;
    params.yMask = yMask;
    params.xDest = xDst;
    params.yDest = yDst;
    params.width = width;
    params.height = height;

    if (!func) {
         /* no fast path, use the general code */
         fbCompositeGeneral(&params);
         return;
    }

    if (!miComputeCompositeRegion (&region,
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
                                   height))
        return;

    n = REGION_NUM_RECTS (&region);
    pbox = REGION_RECTS (&region);
    while (n--)
    {
	h = pbox->y2 - pbox->y1;
	params.ySrc = pbox->y1 - yDst + ySrc;
	params.yMask = pbox->y1 - yDst + yMask;
	params.yDest = pbox->y1;
	while (h)
	{
	    params.height = h;
	    w = pbox->x2 - pbox->x1;
	    params.xSrc = pbox->x1 - xDst + xSrc;
	    params.xMask = pbox->x1 - xDst + xMask;
	    params.xDest = pbox->x1;
	    if (maskRepeat)
	    {
		params.yMask = mod (params.yMask - pMask->pDrawable->y,
                                    pMask->pDrawable->height);
		if (params.height > pMask->pDrawable->height - params.yMask)
		    params.height = pMask->pDrawable->height - params.yMask;
		params.yMask += pMask->pDrawable->y;
	    }
	    if (srcRepeat)
	    {
		params.ySrc = mod (params.ySrc - pSrc->pDrawable->y,
                                   pSrc->pDrawable->height);
		if (params.height > pSrc->pDrawable->height - params.ySrc)
		    params.height = pSrc->pDrawable->height - params.ySrc;
		params.ySrc += pSrc->pDrawable->y;
	    }
	    while (w)
	    {
		params.width = w;
		if (maskRepeat)
		{
		    params.xMask = mod (params.xMask - pMask->pDrawable->x,
                                        pMask->pDrawable->width);
		    if (params.width > pMask->pDrawable->width - params.xMask)
			params.width = pMask->pDrawable->width - params.xMask;
		    params.xMask += pMask->pDrawable->x;
		}
		if (srcRepeat)
		{
		    params.xSrc = mod (params.xSrc - pSrc->pDrawable->x,
                                       pSrc->pDrawable->width);
		    if (params.width > pSrc->pDrawable->width - params.xSrc)
			params.width = pSrc->pDrawable->width - params.xSrc;
		    params.xSrc += pSrc->pDrawable->x;
		}

		(*func) (&params);

		w -= params.width;
		params.xSrc += params.width;
		params.xMask += params.width;
		params.xDest += params.width;
	    }
	    h -= params.height;
	    params.ySrc += params.height;
	    params.yMask += params.height;
	    params.yDest += params.height;
	}
	pbox++;
    }
    REGION_UNINIT (pDst->pDrawable->pScreen, &region);
}

#endif /* RENDER */

Bool
fbPictureInit (ScreenPtr pScreen, PictFormatPtr formats, int nformats)
{

#ifdef RENDER

    PictureScreenPtr    ps;

    if (!miPictureInit (pScreen, formats, nformats))
	return FALSE;
    ps = GetPictureScreen(pScreen);
    ps->Composite = fbComposite;
    ps->Glyphs = miGlyphs;
    ps->CompositeRects = miCompositeRects;
    ps->RasterizeTrapezoid = fbRasterizeTrapezoid;
    ps->AddTraps = fbAddTraps;
    ps->AddTriangles = fbAddTriangles;

#endif /* RENDER */

    return TRUE;
}

#ifdef USE_MMX
/* The CPU detection code needs to be in a file not compiled with
 * "-mmmx -msse", as gcc would generate CMOV instructions otherwise
 * that would lead to SIGILL instructions on old CPUs that don't have
 * it.
 */
#if !defined(__amd64__) && !defined(__x86_64__)

#ifdef HAVE_GETISAX
#include <sys/auxv.h>
#endif

enum CPUFeatures {
    NoFeatures = 0,
    MMX = 0x1,
    MMX_Extensions = 0x2, 
    SSE = 0x6,
    SSE2 = 0x8,
    CMOV = 0x10
};

static unsigned int detectCPUFeatures(void) {
    unsigned int features = 0;
    unsigned int result;

#ifdef HAVE_GETISAX
    if (getisax(&result, 1)) {
        if (result & AV_386_CMOV)
            features |= CMOV;
        if (result & AV_386_MMX)
            features |= MMX;
        if (result & AV_386_AMD_MMX)
            features |= MMX_Extensions;
        if (result & AV_386_SSE)
            features |= SSE;
        if (result & AV_386_SSE2)
            features |= SSE2;
    }
#else
    char vendor[13];
    vendor[0] = 0;
    vendor[12] = 0;
    /* see p. 118 of amd64 instruction set manual Vol3 */
    /* We need to be careful about the handling of %ebx and
     * %esp here. We can't declare either one as clobbered
     * since they are special registers (%ebx is the "PIC
     * register" holding an offset to global data, %esp the
     * stack pointer), so we need to make sure they have their
     * original values when we access the output operands.
     */
    __asm__ ("pushf\n"
             "pop %%eax\n"
             "mov %%eax, %%ecx\n"
             "xor $0x00200000, %%eax\n"
             "push %%eax\n"
             "popf\n"
             "pushf\n"
             "pop %%eax\n"
             "mov $0x0, %%edx\n"
             "xor %%ecx, %%eax\n"
             "jz 1\n"

             "mov $0x00000000, %%eax\n"
	     "push %%ebx\n"
             "cpuid\n"
             "mov %%ebx, %%eax\n"
	     "pop %%ebx\n"
	     "mov %%eax, %1\n"
             "mov %%edx, %2\n"
             "mov %%ecx, %3\n"
             "mov $0x00000001, %%eax\n"
	     "push %%ebx\n"
             "cpuid\n"
	     "pop %%ebx\n"
             "1:\n"
             "mov %%edx, %0\n"
             : "=r" (result), 
               "=m" (vendor[0]), 
               "=m" (vendor[4]), 
               "=m" (vendor[8])
             :
             : "%eax", "%ecx", "%edx"
        );

    if (result) {
        /* result now contains the standard feature bits */
        if (result & (1 << 15))
            features |= CMOV;
        if (result & (1 << 23))
            features |= MMX;
        if (result & (1 << 25))
            features |= SSE;
        if (result & (1 << 26))
            features |= SSE2;
        if ((features & MMX) && !(features & SSE) &&
            (strcmp(vendor, "AuthenticAMD") == 0 ||
             strcmp(vendor, "Geode by NSC") == 0)) {
            /* check for AMD MMX extensions */

            unsigned int result;            
            __asm__("push %%ebx\n"
                    "mov $0x80000000, %%eax\n"
                    "cpuid\n"
                    "xor %%edx, %%edx\n"
                    "cmp $0x1, %%eax\n"
                    "jge 2\n"
                    "mov $0x80000001, %%eax\n"
                    "cpuid\n"
                    "2:\n"
                    "pop %%ebx\n"
                    "mov %%edx, %0\n"
                    : "=r" (result)
                    :
                    : "%eax", "%ecx", "%edx"
                );
            if (result & (1<<22))
                features |= MMX_Extensions;
        }
    }
#endif /* HAVE_GETISAX */
    return features;
}

Bool
fbHaveMMX (void)
{
    static Bool initialized = FALSE;
    static Bool mmx_present;

    if (!initialized)
    {
        unsigned int features = detectCPUFeatures();
	mmx_present = (features & (MMX|MMX_Extensions)) == (MMX|MMX_Extensions);
        initialized = TRUE;
    }
    
    return mmx_present;
}
#endif /* __amd64__ */
#endif
