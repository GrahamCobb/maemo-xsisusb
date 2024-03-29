/************************************************************

Copyright 1989, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.

Copyright 1989 by Hewlett-Packard Company, Palo Alto, California.

			All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Hewlett-Packard not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

HEWLETT-PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
HEWLETT-PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

********************************************************/

/***********************************************************************
 *
 * Request to select input from an extension device.
 *
 */

#define	 NEED_EVENTS
#define	 NEED_REPLIES

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/X.h>	/* for inputstr.h    */
#include <X11/Xproto.h>	/* Request macro     */
#include "inputstr.h"	/* DeviceIntPtr      */
#include "windowstr.h"	/* window structure  */
#include <X11/extensions/XI.h>
#include <X11/extensions/XIproto.h>
#include "extnsionst.h"
#include "extinit.h"	/* LookupDeviceIntRec */
#include "exevents.h"
#include "exglobals.h"

#include "grabdev.h"
#include "selectev.h"

extern Mask ExtExclusiveMasks[];
extern Mask ExtValidMasks[];

static int
HandleDevicePresenceMask(ClientPtr client, WindowPtr win,
                         XEventClass *cls, CARD16 *count)
{
    int i, j;
    Mask mask;

    /* We use the device ID 256 to select events that aren't bound to
     * any device.  For now we only handle the device presence event,
     * but this could be extended to other events that aren't bound to
     * a device.
     *
     * In order not to break in CreateMaskFromList() we remove the
     * entries with device ID 256 from the XEventClass array.
     */

    mask = 0;
    for (i = 0, j = 0; i < *count; i++) {
        if (cls[i] >> 8 != 256) {
            cls[j] = cls[i];
            j++;
            continue;
        }

        switch (cls[i] & 0xff) {
        case _devicePresence:
            mask |= DevicePresenceNotifyMask;
            break;
        }
    }

    *count = j;

    if (mask == 0)
        return Success;

    /* We always only use mksidx = 0 for events not bound to
     * devices */

    if (AddExtensionClient (win, client, mask, 0) != Success)
        return BadAlloc;

    RecalculateDeviceDeliverableEvents(win);

    return Success;
}

/***********************************************************************
 *
 * Handle requests from clients with a different byte order.
 *
 */

int
SProcXSelectExtensionEvent(register ClientPtr client)
{
    register char n;
    register long *p;
    register int i;

    REQUEST(xSelectExtensionEventReq);
    swaps(&stuff->length, n);
    REQUEST_AT_LEAST_SIZE(xSelectExtensionEventReq);
    swapl(&stuff->window, n);
    swaps(&stuff->count, n);
    p = (long *)&stuff[1];
    for (i = 0; i < stuff->count; i++) {
	swapl(p, n);
	p++;
    }
    return (ProcXSelectExtensionEvent(client));
}

/***********************************************************************
 *
 * This procedure selects input from an extension device.
 *
 */

int
ProcXSelectExtensionEvent(register ClientPtr client)
{
    int ret;
    int i;
    WindowPtr pWin;
    struct tmask tmp[EMASKSIZE];

    REQUEST(xSelectExtensionEventReq);
    REQUEST_AT_LEAST_SIZE(xSelectExtensionEventReq);

    if (stuff->length != (sizeof(xSelectExtensionEventReq) >> 2) + stuff->count) {
	SendErrorToClient(client, IReqCode, X_SelectExtensionEvent, 0,
			  BadLength);
	return Success;
    }

    ret = dixLookupWindow(&pWin, stuff->window, client, DixUnknownAccess);
    if (ret != Success) {
	SendErrorToClient(client, IReqCode, X_SelectExtensionEvent, 0, ret);
	return Success;
    }

    if (HandleDevicePresenceMask(client, pWin, (XEventClass *) & stuff[1],
                                &stuff->count) != Success) {
       SendErrorToClient(client, IReqCode, X_SelectExtensionEvent, 0,
                         BadAlloc);
       return Success;
    }

    if ((ret = CreateMaskFromList(client, (XEventClass *) & stuff[1],
				  stuff->count, tmp, NULL,
				  X_SelectExtensionEvent)) != Success)
	return Success;

    for (i = 0; i < EMASKSIZE; i++)
	if (tmp[i].dev != NULL) {
	    if ((ret =
		 SelectForWindow((DeviceIntPtr) tmp[i].dev, pWin, client,
				 tmp[i].mask, ExtExclusiveMasks[i],
				 ExtValidMasks[i])) != Success) {
		SendErrorToClient(client, IReqCode, X_SelectExtensionEvent, 0,
				  ret);
		return Success;
	    }
	}

    return Success;
}
