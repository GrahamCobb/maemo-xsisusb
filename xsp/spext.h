/*
 *
 * Copyright © 2004 Nokia
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Nokia not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Nokia makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * NOKIA DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL NOKIA BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SPEXT_H_
#define _SPEXT_H_

#include "miscstruct.h"
#include "dixstruct.h"

#define xspGetScrPriv(pScr) \
    ((xspScrPrivPtr) (pScr)->devPrivates[xspScrPrivateIndex].ptr)

#define xspScrPriv(pScr) \
    xspScrPrivPtr    pScrPriv = xspGetScrPriv(pScr)

#define XSP_EVENT_PIXEL_DOUBLE_EN  1
#define XSP_EVENT_PIXEL_DOUBLE_DIS 2

extern int xspScrPrivateIndex;
extern int XSPEventBase;

typedef int (*xsp_event_callback)(int event,
                                  int screen,
                                  void *closure);

typedef struct _xspScrPriv {
    int dsp_enabled;
    ClientPtr dsp_client;
    xRectangle dsp_area;

    int pixel_doubling;
    ClientPtr pixel_doubling_client;

    xsp_event_callback callback;
    void *closure;
} xspScrPrivRec, *xspScrPrivPtr;

void XSPExtensionInit(void);

extern void (*xsp_dsp_event_hook)(void);
extern void (*xsp_ts_set_calibration)(int calconst1, 
        int calconst2, 
        int calconst3, 
        int calconst4, 
        int calconst5, 
        int calconst6, 
        int calconst7);

void XSPCheckDamage(int screen, xRectangle *area);
int XSPSetEventCallback(int screen, xsp_event_callback callback,
                        void *closure);

#endif /* _SPEXT_H_ */
