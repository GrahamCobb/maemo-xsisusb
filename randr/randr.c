/*
 * Copyright © 2000 Compaq Computer Corporation
 * Copyright © 2002 Hewlett-Packard Company
 * Copyright © 2006 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 *
 * Author:  Jim Gettys, Hewlett-Packard Company, Inc.
 *	    Keith Packard, Intel Corporation
 */

#define NEED_REPLIES
#define NEED_EVENTS
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "randrstr.h"

/* From render.h */
#ifndef SubPixelUnknown
#define SubPixelUnknown 0
#endif

#define RR_VALIDATE
static int	RRNScreens;

#define wrap(priv,real,mem,func) {\
    priv->mem = real->mem; \
    real->mem = func; \
}

#define unwrap(priv,real,mem) {\
    real->mem = priv->mem; \
}

static int ProcRRDispatch (ClientPtr pClient);
static int SProcRRDispatch (ClientPtr pClient);

int	RREventBase;
int	RRErrorBase;
RESTYPE RRClientType, RREventType; /* resource types for event masks */
int	RRClientPrivateIndex;

int	rrPrivIndex = -1;

static void
RRClientCallback (CallbackListPtr	*list,
		  pointer		closure,
		  pointer		data)
{
    NewClientInfoRec	*clientinfo = (NewClientInfoRec *) data;
    ClientPtr		pClient = clientinfo->client;
    rrClientPriv(pClient);
    RRTimesPtr		pTimes = (RRTimesPtr) (pRRClient + 1);
    int			i;

    pRRClient->major_version = 0;
    pRRClient->minor_version = 0;
    for (i = 0; i < screenInfo.numScreens; i++)
    {
	ScreenPtr   pScreen = screenInfo.screens[i];
	rrScrPriv(pScreen);

	if (pScrPriv)
	{
	    pTimes[i].setTime = pScrPriv->lastSetTime;
	    pTimes[i].configTime = pScrPriv->lastConfigTime;
	}
    }
}

static void
RRResetProc (ExtensionEntry *extEntry)
{
}
    
static Bool
RRCloseScreen (int i, ScreenPtr pScreen)
{
    rrScrPriv(pScreen);
    int		    j;

    unwrap (pScrPriv, pScreen, CloseScreen);
    for (j = pScrPriv->numCrtcs - 1; j >= 0; j--)
	RRCrtcDestroy (pScrPriv->crtcs[j]);
    for (j = pScrPriv->numOutputs - 1; j >= 0; j--)
	RROutputDestroy (pScrPriv->outputs[j]);
    
    xfree (pScrPriv);
    RRNScreens -= 1;	/* ok, one fewer screen with RandR running */
    return (*pScreen->CloseScreen) (i, pScreen);    
}

static void
SRRScreenChangeNotifyEvent(xRRScreenChangeNotifyEvent *from,
			   xRRScreenChangeNotifyEvent *to)
{
    to->type = from->type;
    to->rotation = from->rotation;
    cpswaps(from->sequenceNumber, to->sequenceNumber);
    cpswapl(from->timestamp, to->timestamp);
    cpswapl(from->configTimestamp, to->configTimestamp);
    cpswapl(from->root, to->root);
    cpswapl(from->window, to->window);
    cpswaps(from->sizeID, to->sizeID);
    cpswaps(from->widthInPixels, to->widthInPixels);
    cpswaps(from->heightInPixels, to->heightInPixels);
    cpswaps(from->widthInMillimeters, to->widthInMillimeters);
    cpswaps(from->heightInMillimeters, to->heightInMillimeters);
    cpswaps(from->subpixelOrder, to->subpixelOrder);
}

static void
SRRCrtcChangeNotifyEvent(xRRCrtcChangeNotifyEvent *from,
			 xRRCrtcChangeNotifyEvent *to)
{
    to->type = from->type;
    to->subCode = from->subCode;
    cpswaps(from->sequenceNumber, to->sequenceNumber);
    cpswapl(from->timestamp, to->timestamp);
    cpswapl(from->window, to->window);
    cpswapl(from->crtc, to->crtc);
    cpswapl(from->mode, to->mode);
    cpswapl(from->window, to->window);
    cpswaps(from->rotation, to->rotation);
    cpswaps(from->x, to->x);
    cpswaps(from->y, to->y);
    cpswaps(from->width, to->width);
    cpswaps(from->height, to->height);
}

static void
SRROutputChangeNotifyEvent(xRROutputChangeNotifyEvent *from,
			   xRROutputChangeNotifyEvent *to)
{
    to->type = from->type;
    to->subCode = from->subCode;
    cpswaps(from->sequenceNumber, to->sequenceNumber);
    cpswapl(from->timestamp, to->timestamp);
    cpswapl(from->configTimestamp, to->configTimestamp);
    cpswapl(from->window, to->window);
    cpswapl(from->output, to->output);
    cpswapl(from->crtc, to->crtc);
    cpswapl(from->mode, to->mode);
    cpswaps(from->rotation, to->rotation);
}

static void
SRROutputPropertyNotifyEvent(xRROutputPropertyNotifyEvent *from,
			     xRROutputPropertyNotifyEvent *to)
{
    to->type = from->type;
    to->subCode = from->subCode;
    cpswaps(from->sequenceNumber, to->sequenceNumber);
    cpswapl(from->window, to->window);
    cpswapl(from->output, to->output);
    cpswapl(from->atom, to->atom);
    cpswapl(from->timestamp, to->timestamp);
}

static void
SRRNotifyEvent (xEvent *from,
		xEvent *to)
{
    switch (from->u.u.detail) {
    case RRNotify_CrtcChange:
	SRRCrtcChangeNotifyEvent ((xRRCrtcChangeNotifyEvent *) from,
				  (xRRCrtcChangeNotifyEvent *) to);
	break;
    case RRNotify_OutputChange:
	SRROutputChangeNotifyEvent ((xRROutputChangeNotifyEvent *) from,
				    (xRROutputChangeNotifyEvent *) to);
	break;
    case RRNotify_OutputProperty:
	SRROutputPropertyNotifyEvent ((xRROutputPropertyNotifyEvent *) from,
				      (xRROutputPropertyNotifyEvent *) to);
	break;
    default:
	break;
    }
}

static int RRGeneration;

Bool RRInit (void)
{
    if (RRGeneration != serverGeneration)
    {
	if (!RRModeInit ())
	    return FALSE;
	if (!RRCrtcInit ())
	    return FALSE;
	if (!RROutputInit ())
	    return FALSE;
	RRGeneration = serverGeneration;
    }
    return TRUE;
}

static int RRScreenGeneration;

Bool RRScreenInit(ScreenPtr pScreen)
{
    rrScrPrivPtr   pScrPriv;

    if (!RRInit ())
	return FALSE;

    if (RRScreenGeneration != serverGeneration)
    {
	if ((rrPrivIndex = AllocateScreenPrivateIndex()) < 0)
	    return FALSE;
	RRScreenGeneration = serverGeneration;
    }

    pScrPriv = (rrScrPrivPtr) xalloc (sizeof (rrScrPrivRec));
    if (!pScrPriv)
	return FALSE;

    SetRRScreen(pScreen, pScrPriv);

    /*
     * Calling function best set these function vectors
     */
    pScrPriv->rrGetInfo = 0;
    pScrPriv->maxWidth = pScrPriv->minWidth = pScreen->width;
    pScrPriv->maxHeight = pScrPriv->minHeight = pScreen->height;
    
    pScrPriv->width = pScreen->width;
    pScrPriv->height = pScreen->height;
    pScrPriv->mmWidth = pScreen->mmWidth;
    pScrPriv->mmHeight = pScreen->mmHeight;
#if RANDR_12_INTERFACE
    pScrPriv->rrScreenSetSize = NULL;
    pScrPriv->rrCrtcSet = NULL;
    pScrPriv->rrCrtcSetGamma = NULL;
#endif
#if RANDR_10_INTERFACE    
    pScrPriv->rrSetConfig = 0;
    pScrPriv->rotations = RR_Rotate_0;
    pScrPriv->reqWidth = pScreen->width;
    pScrPriv->reqHeight = pScreen->height;
    pScrPriv->nSizes = 0;
    pScrPriv->pSizes = NULL;
    pScrPriv->rotation = RR_Rotate_0;
    pScrPriv->rate = 0;
    pScrPriv->size = 0;
#endif
    
    /*
     * This value doesn't really matter -- any client must call
     * GetScreenInfo before reading it which will automatically update
     * the time
     */
    pScrPriv->lastSetTime = currentTime;
    pScrPriv->lastConfigTime = currentTime;
    
    wrap (pScrPriv, pScreen, CloseScreen, RRCloseScreen);

    pScrPriv->numOutputs = 0;
    pScrPriv->outputs = NULL;
    pScrPriv->numCrtcs = 0;
    pScrPriv->crtcs = NULL;
    
    RRNScreens += 1;	/* keep count of screens that implement randr */
    return TRUE;
}

/*ARGSUSED*/
static int
RRFreeClient (pointer data, XID id)
{
    RREventPtr   pRREvent;
    WindowPtr	    pWin;
    RREventPtr   *pHead, pCur, pPrev;

    pRREvent = (RREventPtr) data;
    pWin = pRREvent->window;
    pHead = (RREventPtr *) LookupIDByType(pWin->drawable.id, RREventType);
    if (pHead) {
	pPrev = 0;
	for (pCur = *pHead; pCur && pCur != pRREvent; pCur=pCur->next)
	    pPrev = pCur;
	if (pCur)
	{
	    if (pPrev)
	    	pPrev->next = pRREvent->next;
	    else
	    	*pHead = pRREvent->next;
	}
    }
    xfree ((pointer) pRREvent);
    return 1;
}

/*ARGSUSED*/
static int
RRFreeEvents (pointer data, XID id)
{
    RREventPtr   *pHead, pCur, pNext;

    pHead = (RREventPtr *) data;
    for (pCur = *pHead; pCur; pCur = pNext) {
	pNext = pCur->next;
	FreeResource (pCur->clientResource, RRClientType);
	xfree ((pointer) pCur);
    }
    xfree ((pointer) pHead);
    return 1;
}

void
RRExtensionInit (void)
{
    ExtensionEntry *extEntry;

    if (RRNScreens == 0) return;

    RRClientPrivateIndex = AllocateClientPrivateIndex ();
    if (!AllocateClientPrivate (RRClientPrivateIndex,
				sizeof (RRClientRec) +
				screenInfo.numScreens * sizeof (RRTimesRec)))
	return;
    if (!AddCallback (&ClientStateCallback, RRClientCallback, 0))
	return;

    RRClientType = CreateNewResourceType(RRFreeClient);
    if (!RRClientType)
	return;
    RREventType = CreateNewResourceType(RRFreeEvents);
    if (!RREventType)
	return;
    extEntry = AddExtension (RANDR_NAME, RRNumberEvents, RRNumberErrors,
			     ProcRRDispatch, SProcRRDispatch,
			     RRResetProc, StandardMinorOpcode);
    if (!extEntry)
	return;
    RRErrorBase = extEntry->errorBase;
    RREventBase = extEntry->eventBase;
    EventSwapVector[RREventBase + RRScreenChangeNotify] = (EventSwapPtr) 
	SRRScreenChangeNotifyEvent;
    EventSwapVector[RREventBase + RRNotify] = (EventSwapPtr)
	SRRNotifyEvent;

    RRXineramaExtensionInit();
}

static int
TellChanged (WindowPtr pWin, pointer value)
{
    RREventPtr			*pHead, pRREvent;
    ClientPtr			client;
    ScreenPtr			pScreen = pWin->drawable.pScreen;
    rrScrPriv(pScreen);
    int				i;

    pHead = (RREventPtr *) LookupIDByType (pWin->drawable.id, RREventType);
    if (!pHead)
	return WT_WALKCHILDREN;

    for (pRREvent = *pHead; pRREvent; pRREvent = pRREvent->next) 
    {
	client = pRREvent->client;
	if (client == serverClient || client->clientGone)
	    continue;

	if (pRREvent->mask & RRScreenChangeNotifyMask)
	    RRDeliverScreenEvent (client, pWin, pScreen);
	
	if (pRREvent->mask & RRCrtcChangeNotifyMask)
	{
	    for (i = 0; i < pScrPriv->numCrtcs; i++)
	    {
		RRCrtcPtr   crtc = pScrPriv->crtcs[i];
		if (crtc->changed)
		    RRDeliverCrtcEvent (client, pWin, crtc);
	    }
	}
	
	if (pRREvent->mask & RROutputChangeNotifyMask)
	{
	    for (i = 0; i < pScrPriv->numOutputs; i++)
	    {
		RROutputPtr   output = pScrPriv->outputs[i];
		if (output->changed)
		    RRDeliverOutputEvent (client, pWin, output);
	    }
	}
    }
    return WT_WALKCHILDREN;
}

/*
 * Something changed; send events and adjust pointer position
 */
void
RRTellChanged (ScreenPtr pScreen)
{
    rrScrPriv (pScreen);
    int i;
    
    if (pScrPriv->changed)
    {
	UpdateCurrentTime ();
	if (pScrPriv->configChanged)
	{
	    pScrPriv->lastConfigTime = currentTime;
	    pScrPriv->configChanged = FALSE;
	}
	pScrPriv->changed = FALSE;
	WalkTree (pScreen, TellChanged, (pointer) pScreen);
	for (i = 0; i < pScrPriv->numOutputs; i++)
	    pScrPriv->outputs[i]->changed = FALSE;
	for (i = 0; i < pScrPriv->numCrtcs; i++)
	    pScrPriv->crtcs[i]->changed = FALSE;
	if (pScrPriv->layoutChanged)
	{
	    pScrPriv->layoutChanged = FALSE;
	    RRPointerScreenConfigured (pScreen);
	    RRSendConfigNotify (pScreen);
	}
    }
}

/*
 * Return the first output which is connected to an active CRTC
 * Used in emulating 1.0 behaviour
 */
RROutputPtr
RRFirstOutput (ScreenPtr pScreen)
{
    rrScrPriv(pScreen);
    RROutputPtr		    output;
    int	i, j;
    
    for (i = 0; i < pScrPriv->numCrtcs; i++)
    {
	RRCrtcPtr   crtc = pScrPriv->crtcs[i];
	for (j = 0; j < pScrPriv->numOutputs; j++)
	{
	    output = pScrPriv->outputs[j];
	    if (output->crtc == crtc)
		return output;
	}
    }
    return NULL;
}

CARD16
RRVerticalRefresh (xRRModeInfo *mode)
{
    CARD32  refresh;
    CARD32  dots = mode->hTotal * mode->vTotal;
    if (!dots)
	return 0;
    refresh = (mode->dotClock + dots/2) / dots;
    if (refresh > 0xffff)
	refresh = 0xffff;
    return (CARD16) refresh;
}

static int
ProcRRDispatch (ClientPtr client)
{
    REQUEST(xReq);
    if (stuff->data >= RRNumberRequests || !ProcRandrVector[stuff->data])
	return BadRequest;
    return (*ProcRandrVector[stuff->data]) (client);
}

static int
SProcRRDispatch (ClientPtr client)
{
    REQUEST(xReq);
    if (stuff->data >= RRNumberRequests || !ProcRandrVector[stuff->data])
	return BadRequest;
    return (*SProcRandrVector[stuff->data]) (client);
}

