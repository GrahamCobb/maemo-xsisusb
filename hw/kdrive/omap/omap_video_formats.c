/*
 * Copyright © 2006-2007 Nokia Corporation
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

/* 

   XFree86 Xv DDX written by Mark Vojkovich (markv@valinux.com) 
   Adapted for KDrive by Pontus Lidman <pontus.lidman@nokia.com>

   Copyright (C) 2000, 2001 - Nokia Home Communications
   Copyright (C) 1998, 1999 - The XFree86 Project Inc.

All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, and/or sell copies of the Software, and to permit persons
to whom the Software is furnished to do so, provided that the above
copyright notice(s) and this permission notice appear in all copies of
the Software and that both the above copyright notice(s) and this
permission notice appear in supporting documentation.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
OF THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
HOLDERS INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY
SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER
RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

Except as contained in this notice, the name of a copyright holder
shall not be used in advertising or otherwise to promote the sale, use
or other dealings in this Software without prior written authorization
of the copyright holder.

*/

/**
 * None of the functions in this file need to deal with rotation:
 * we can rotate on scanout from the LCD controller, so all our
 * copies are unrotated, as rotating while copying is a horrific
 * speed hit.
 */

#ifdef HAVE_KDRIVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include "kdrive.h"
#include "omap.h"
#include "fourcc.h"

/**
 * Copy YUV422/YUY2 data with no scaling.
 */
void
omap_copy_packed(KdScreenInfo *screen, CARD8 *src, CARD8 *dst, int randr,
    int srcPitch, int dstPitch, int srcW, int srcH, int top, int left,
    int h, int w)
{
    /* memcpy FTW on ARM. */
    if (srcPitch == dstPitch) {
        memcpy(dst, src, h * srcPitch);
    }
    else {
        while (h--) {
            memcpy(dst, src, srcPitch);
            src += srcPitch;
            dst += dstPitch;
        }
    }
}


/**
 * Copy packed video data for downscaling, where 'scaling' in this case
 * means just removing lines.  Unfortunately, since we don't want to get
 * into the business of swapping the U and V channels, we remove a
 * macroblock (2x1) at a time.
 */
void
omap_copy_scale_packed(KdScreenInfo *screen,
                       struct omap_video_info *video_info, CARD8 *src,
                       CARD8 *dst, int randr, int srcPitch, int dstPitch,
                       const int srcW, const int srcH, int top, int left,
                       int h, int w, const int dstW, const int dstH)
{
    int i = 0, k = 0, ih = 0, jh = 0, jhn = (srcH - dstH);
    int kh = 0, khn = (srcW - dstW), lh = 0;

    if ((randr & RR_Rotate_All) != RR_Rotate_0) {
        ErrorF("omapCopyPackedData: rotation not supported\n");
        return;
    }

    if (top || left) {
        ErrorF("omapCopyPackedData: partial updates not supported\n");
        return;
    }

    if (srcW & 1 || dstW & 1) {
        ErrorF("omapCopyPackedData: widths must be multiples of two\n");
        return;
    }

    w >>= 1;

    for (i = 0; i < h; i++, src += srcPitch, ih += (srcH - dstH)) {
        if (video_info->vscale && (jh == ih || (jh < ih && jhn > ih))) {
            jh += srcH;
            jhn += srcH;
            continue;
        }

        /* memcpy the whole lot if we can: it's a lot quicker. */
        if (!video_info->hscale) {
            memcpy(dst, src, srcPitch);
        }
        else {
	    CARD32 *s = (CARD32 *) src;
            CARD32 *d = (CARD32 *) dst;

            kh = 0;
            khn = 2 * srcW;
            lh = 2 * (srcW - dstW);
            /* failing this, do 32-bit copies by hand for the pixels we
             * need. */
            for (k = 0; k < w; k++, s++, lh += 2 * (srcW - dstW)) {
                if (k != (w - 1) && (kh == lh || (kh < lh && khn > lh))) {
                    kh += (2 * srcW);
                    khn += (2 * srcW);
                }
                else {
                    *d++ = *s;
                }
            }

        }
        dst += dstPitch;
    }
}


/**
 * Copy I420/YV12 data to YUY2, with no scaling.  Originally from kxv.c.
 */
void
omap_copy_planar(KdScreenInfo *screen, CARD8 *src, CARD8 *dst, int randr,
    int srcPitch, int srcPitch2, int dstPitch, int srcW, int srcH, int height,
    int top, int left, int h, int w, int id)
{
    int i, j;
    CARD8 *src1, *src2, *src3, *dst1;

    /* compute source data pointers */
    src1 = src;
    src2 = src1 + height * srcPitch;
    src3 = src2 + (height >> 1) * srcPitch2;

    if (id == FOURCC_I420) {
        CARD8 *srct = src3;
        src3 = src2;
        src2 = srct;
    }

    dst1 = dst;

    w >>= 1;
    for (j = 0; j < h; j++) {
	CARD32 *dst = (CARD32 *)dst1;
        CARD16 *s1 = (CARD16 *) src1;
	CARD8 *s2 = src2;
	CARD8 *s3 = src3;

	for (i = 0; i < w; i++) {
            *dst++ = (*s1 & 0x00ff) | ((*s1 & 0xff00) << 8) | (*s3 << 8) | (*s2 << 24);
            s1++;
	    s2++;
	    s3++;
	}
	src1 += srcPitch;
	dst1 += dstPitch;
	if (j & 1) {
	    src2 += srcPitch2;
	    src3 += srcPitch2;
	}
    }
}


/**
 * Copy and expand planar (I420) -> packed (UYVY) video data, including
 * downscaling, by just removing two lines at a time.
 *
 * FIXME: Target for arg reduction.
 */
void
omap_copy_scale_planar(KdScreenInfo *screen,
                       struct omap_video_info *video_info, CARD8 *src,
                       CARD8 *dstb, int randr, int srcPitch, int srcPitch2,
                       int dstPitch, int srcW, int srcH, int height, int top,
                       int left, int h, int w, int id, int dstW, int dstH)
{
    CARD8 *src1, *src2, *src3, *dst1;
    int srcDown = srcPitch, srcDown2 = srcPitch2;
    int srcRight = 2, srcRight2 = 1, srcNext = 1;
    int i = 0, k = 0, ih = (srcH - dstH), jh = 0, jhn = srcH;
    int kh = 0, khn = 0, lh = 0;

    if ((randr & RR_Rotate_All) != RR_Rotate_0) {
        ErrorF("omapExpandPlanarData: rotation not supported\n");
        return;
    }
    if (top || left) {
        ErrorF("omapExpandPlanarData: partial updates not supported\n");
        return;
    }

    /* compute source data pointers */
    src1 = src;
    src2 = src1 + height * srcPitch;
    src3 = src2 + (height >> 1) * srcPitch2;

    if (id == FOURCC_I420) {
        CARD8 *tmp = src2;
        src2 = src3;
        src3 = tmp;
    }

    dst1 = dstb;

    w >>= 1;
    for (i = 0; i < h; i++, ih += (srcH - dstH)) {
        if (video_info->vscale && (jh == ih || (jh < ih && jhn > ih))) {
            jh += srcH;
            jhn += srcH;
        }

        else {
	    CARD32 *dst = (CARD32 *)dst1;
            CARD8 *s1l = src1;
            CARD8 *s1r = src1 + srcNext;
            CARD8 *s2 = src2;
            CARD8 *s3 = src3;
        
            kh = 0;
            khn = 2 * srcW;
            lh = 2 * (srcW - dstW);

            for (k = 0; k < w; k++, lh += 2 * (srcW - dstW)) {
                if (video_info->hscale && k != (w - 1) &&
                    (kh == lh || (kh < lh && khn > lh))) {
                    kh += (2 * srcW);
                    khn += (2 * srcW); 
                }
                else {
                    *dst++ = *s1l | (*s1r << 16) | (*s3 << 8) | (*s2 << 24);
                }

                s1l += srcRight;
                s1r += srcRight;
                s2 += srcRight2;
                s3 += srcRight2;
            }

            dst1 += dstPitch;
        }

        src1 += srcDown;
	if (i & 1) {
	    src2 += srcDown2;
	    src3 += srcDown2;
	}
    }
}


/**
 * Copy I420 data to the custom 'YUV420' format, which is actually:
 * y11 u11,u12,u21,u22 u13,u14,u23,u24 y12 y14 y13
 * y21 v11,v12,v21,v22 v13,v14,v23,v24 y22 y24 y23
 *
 * The third and fourth luma components are swapped.  Yes, this is weird.
 *
 * So, while we have the same 2x2 macroblocks in terms of colour granularity,
 * we actually require 4x2.  We lop off the last 1-3 lines if width is not a
 * multiple of four, and let the hardware expand.
 *
 * FIXME: Target for arg reduction.
 */
void
omap_copy_yuv420(KdScreenInfo *screen, struct omap_video_info *video_info,
                 CARD8 *srcb, CARD8 *dstb, int randr, int srcPitch,
                 int srcPitch2, int dstPitch, int srcW, int srcH, int top,
                 int left, int h, int w, int id)
{
    CARD8 *srcy, *srcu, *srcv, *dst;
    CARD16 *d1;
    CARD32 *d2;
    int i, j;

    if ((randr & RR_Rotate_All) != RR_Rotate_0) {
        ErrorF("omapCopyPlanarData: rotation not supported\n");
        return;
    }

    if (top || left || h != srcH || w != srcW) {
        ErrorF("omapCopyPlanarData: offset updates not supported\n");
        return;
    }

    srcy = srcb;
    srcv = srcy + h * srcPitch;
    srcu = srcv + (h >> 1) * srcPitch2;
    dst = dstb;

    if (id == FOURCC_I420) {
        CARD8 *tmp = srcv;
        srcv = srcu;
        srcu = tmp;
    }

    w >>= 2;
    for (i = 0; i < h; i++) {
        CARD32 *sy = (CARD32 *) srcy;
        CARD16 *sc;

        sc = (CARD16 *) ((i & 1) ? srcv : srcu);
        d1 = (CARD16 *) dst;

        for (j = 0; j < w; j++) {
            if (((unsigned long) d1) & 3) {
                /* Luma 1, chroma 1. */
                *d1++ = (*sy & 0x000000ff) | ((*sc & 0x00ff) << 8);
                /* Chroma 2, luma 2. */
                *d1++ = ((*sc & 0xff00) >> 8) | (*sy & 0x0000ff00);
            }
            else {
                d2 = (CARD32 *) d1;
                /* Luma 1, chroma 1, chroma 2, luma 2. */
                *d2++ = (*sy & 0x000000ff) | (*sc << 8) |
                        ((*sy & 0x0000ff00) << 16);
                d1 = (CARD16 *) d2;
            }
            /* Luma 4, luma 3. */
            *d1++ = ((*sy & 0xff000000) >> 24) | ((*sy & 0x00ff0000) >> 8);
            sy++;
            sc++;
        }

        dst += dstPitch;
        srcy += srcPitch;
        if (i & 1) {
            srcu += srcPitch2;
            srcv += srcPitch2;
        }
    }
}


/**
 * Paint a given drawable with a colourkey.
 */
void
omap_paint_ckey(DrawablePtr drawable, RegionPtr region, Pixel fg)
{
    GCPtr	gc;
    CARD32    	val[2];
    xRectangle	*rects, *r;
    BoxPtr	box = REGION_RECTS(region);
    int		num_boxes = REGION_NUM_RECTS(region);

    rects = ALLOCATE_LOCAL(num_boxes * sizeof(xRectangle));
    if (!rects)
	goto out_rects;

    r = rects;
    while (num_boxes--) {
	r->x = box->x1 - drawable->x;
	r->y = box->y1 - drawable->y;
	r->width = box->x2 - box->x1;
	r->height = box->y2 - box->y1;
	r++;
	box++;
    }

    gc = GetScratchGC (drawable->depth, drawable->pScreen);
    if (!gc)
	goto out_gc;

    val[0] = fg;
    val[1] = IncludeInferiors;
    ChangeGC(gc, GCForeground|GCSubwindowMode, val);

    ValidateGC(drawable, gc);

    /* Savage hack: we need to not generate damage, so we ignore the chain
     * and call straight through to fb. */
    fbPolyFillRect(drawable, gc, REGION_NUM_RECTS(region), rects);

    FreeScratchGC(gc);

out_gc:
    DEALLOCATE_LOCAL(rects);
out_rects:
    return;
}
