/*
 * Copyright © 2006 Keith Packard
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
 */

#include "randrstr.h"
#include "swaprep.h"

RESTYPE	RRCrtcType;

/*
 * Notify the CRTC of some change
 */
void
RRCrtcChanged (RRCrtcPtr crtc, Bool layoutChanged)
{
    ScreenPtr	pScreen = crtc->pScreen;

    crtc->changed = TRUE;
    if (pScreen)
    {
	rrScrPriv(pScreen);
    
	pScrPriv->changed = TRUE;
	/*
	 * Send ConfigureNotify on any layout change
	 */
	if (layoutChanged)
	    pScrPriv->layoutChanged = TRUE;
    }
}

/*
 * Create a CRTC
 */
RRCrtcPtr
RRCrtcCreate (void	*devPrivate)
{
    RRCrtcPtr	crtc;
    
    if (!RRInit())
	return NULL;
    crtc = xalloc (sizeof (RRCrtcRec));
    if (!crtc)
	return NULL;
    crtc->id = FakeClientID (0);
    crtc->pScreen = NULL;
    crtc->mode = NULL;
    crtc->x = 0;
    crtc->y = 0;
    crtc->rotation = RR_Rotate_0;
    crtc->rotations = RR_Rotate_0;
    crtc->outputs = NULL;
    crtc->numOutputs = 0;
    crtc->gammaSize = 0;
    crtc->gammaRed = crtc->gammaBlue = crtc->gammaGreen = NULL;
    crtc->changed = FALSE;
    crtc->devPrivate = devPrivate;

    if (!AddResource (crtc->id, RRCrtcType, (pointer) crtc))
	return NULL;

    return crtc;
}

/*
 * Attach a Crtc to a screen. This is done as a separate step
 * so that an xf86-based driver can create CRTCs in PreInit
 * before the Screen has been created
 */

Bool
RRCrtcAttachScreen (RRCrtcPtr crtc, ScreenPtr pScreen)
{
    rrScrPriv (pScreen);
    RRCrtcPtr	*crtcs;

    /* make space for the crtc pointer */
    if (pScrPriv->numCrtcs)
	crtcs = xrealloc (pScrPriv->crtcs, 
			  (pScrPriv->numCrtcs + 1) * sizeof (RRCrtcPtr));
    else
	crtcs = xalloc (sizeof (RRCrtcPtr));
    if (!crtcs)
	return FALSE;
    
    /* attach the screen and crtc together */
    crtc->pScreen = pScreen;
    pScrPriv->crtcs = crtcs;
    pScrPriv->crtcs[pScrPriv->numCrtcs++] = crtc;

    RRCrtcChanged (crtc, TRUE);
    return TRUE;
}

/*
 * Notify the extension that the Crtc has been reconfigured,
 * the driver calls this whenever it has updated the mode
 */
Bool
RRCrtcNotify (RRCrtcPtr	    crtc,
	      RRModePtr	    mode,
	      int	    x,
	      int	    y,
	      Rotation	    rotation,
	      int	    numOutputs,
	      RROutputPtr   *outputs)
{
    int	    i, j;
    
    /*
     * Check to see if any of the new outputs were
     * not in the old list and mark them as changed
     */
    for (i = 0; i < numOutputs; i++)
    {
	for (j = 0; j < crtc->numOutputs; j++)
	    if (outputs[i] == crtc->outputs[j])
		break;
	if (j == crtc->numOutputs)
	{
	    RROutputChanged (outputs[i], FALSE);
	    RRCrtcChanged (crtc, FALSE);
	}
    }
    /*
     * Check to see if any of the old outputs are
     * not in the new list and mark them as changed
     */
    for (j = 0; j < crtc->numOutputs; j++)
    {
	for (i = 0; i < numOutputs; i++)
	    if (outputs[i] == crtc->outputs[j])
		break;
	if (i == numOutputs)
	{
	    RROutputChanged (crtc->outputs[j], FALSE);
	    RRCrtcChanged (crtc, FALSE);
	}
    }
    /*
     * Reallocate the crtc output array if necessary
     */
    if (numOutputs != crtc->numOutputs)
    {
	RROutputPtr *newoutputs;
	
	if (numOutputs)
	{
	    if (crtc->numOutputs)
		newoutputs = xrealloc (crtc->outputs,
				    numOutputs * sizeof (RROutputPtr));
	    else
		newoutputs = xalloc (numOutputs * sizeof (RROutputPtr));
	    if (!newoutputs)
		return FALSE;
	}
	else
	{
	    if (crtc->outputs)
		xfree (crtc->outputs);
	    newoutputs = NULL;
	}
	crtc->outputs = newoutputs;
	crtc->numOutputs = numOutputs;
    }
    /*
     * Copy the new list of outputs into the crtc
     */
    memcpy (crtc->outputs, outputs, numOutputs * sizeof (RROutputPtr));
    /*
     * Update remaining crtc fields
     */
    if (mode != crtc->mode)
    {
	if (crtc->mode)
	    RRModeDestroy (crtc->mode);
	crtc->mode = mode;
	if (mode != NULL)
	    mode->refcnt++;
	RRCrtcChanged (crtc, TRUE);
    }
    if (x != crtc->x)
    {
	crtc->x = x;
	RRCrtcChanged (crtc, TRUE);
    }
    if (y != crtc->y)
    {
	crtc->y = y;
	RRCrtcChanged (crtc, TRUE);
    }
    if (rotation != crtc->rotation)
    {
	crtc->rotation = rotation;
	RRCrtcChanged (crtc, TRUE);
    }
    return TRUE;
}

void
RRDeliverCrtcEvent (ClientPtr client, WindowPtr pWin, RRCrtcPtr crtc)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    rrScrPriv (pScreen);
    xRRCrtcChangeNotifyEvent	ce;
    RRModePtr	mode = crtc->mode;
    
    ce.type = RRNotify + RREventBase;
    ce.subCode = RRNotify_CrtcChange;
    ce.sequenceNumber = client->sequence;
    ce.timestamp = pScrPriv->lastSetTime.milliseconds;
    ce.window = pWin->drawable.id;
    ce.crtc = crtc->id;
    ce.rotation = crtc->rotation;
    if (mode)
    {
	ce.mode = mode->mode.id;
	ce.x = crtc->x;
	ce.y = crtc->y;
	ce.width = mode->mode.width;
	ce.height = mode->mode.height;
    }
    else
    {
	ce.mode = None;
	ce.x = 0;
	ce.y = 0;
	ce.width = 0;
	ce.height = 0;
    }
    WriteEventsToClient (client, 1, (xEvent *) &ce);
}

/*
 * Request that the Crtc be reconfigured
 */
Bool
RRCrtcSet (RRCrtcPtr    crtc,
	   RRModePtr	mode,
	   int		x,
	   int		y,
	   Rotation	rotation,
	   int		numOutputs,
	   RROutputPtr  *outputs)
{
    ScreenPtr	pScreen = crtc->pScreen;

    /* See if nothing changed */
    if (crtc->mode == mode &&
	crtc->x == x &&
	crtc->y == y &&
	crtc->rotation == rotation &&
	crtc->numOutputs == numOutputs &&
	!memcmp (crtc->outputs, outputs, numOutputs * sizeof (RROutputPtr)))
    {
	return TRUE;
    }
    if (pScreen)
    {
#if RANDR_12_INTERFACE
	rrScrPriv(pScreen);
	if (pScrPriv->rrCrtcSet)
	{
	    return (*pScrPriv->rrCrtcSet) (pScreen, crtc, mode, x, y, 
					   rotation, numOutputs, outputs);
	}
#endif
#if RANDR_10_INTERFACE
	if (pScrPriv->rrSetConfig)
	{
	    RRScreenSize	    size;
	    RRScreenRate	    rate;
	    Bool		    ret;

	    if (!mode)
	    {
		RRCrtcNotify (crtc, NULL, x, y, rotation, 0, NULL);
		return TRUE;
	    }

	    size.width = mode->mode.width;
	    size.height = mode->mode.height;
	    if (outputs[0]->mmWidth && outputs[0]->mmHeight)
	    {
		size.mmWidth = outputs[0]->mmWidth;
		size.mmHeight = outputs[0]->mmHeight;
	    }
	    else
	    {
		size.mmWidth = pScreen->mmWidth;
		size.mmHeight = pScreen->mmHeight;
	    }
	    size.nRates = 1;
	    rate.rate = RRVerticalRefresh (&mode->mode);
	    size.pRates = &rate;
	    ret = (*pScrPriv->rrSetConfig) (pScreen, rotation, rate.rate, &size);
	    /*
	     * Old 1.0 interface tied screen size to mode size
	     */
	    if (ret)
	    {
		RRCrtcNotify (crtc, mode, x, y, rotation, 1, outputs);
		RRScreenSizeNotify (pScreen);
	    }
	    return ret;
	}
#endif
	RRTellChanged (pScreen);
    }
    return FALSE;
}

/*
 * Destroy a Crtc at shutdown
 */
void
RRCrtcDestroy (RRCrtcPtr crtc)
{
    FreeResource (crtc->id, 0);
}

static int
RRCrtcDestroyResource (pointer value, XID pid)
{
    RRCrtcPtr	crtc = (RRCrtcPtr) value;
    ScreenPtr	pScreen = crtc->pScreen;

    if (pScreen)
    {
	rrScrPriv(pScreen);
	int		i;
    
	for (i = 0; i < pScrPriv->numCrtcs; i++)
	{
	    if (pScrPriv->crtcs[i] == crtc)
	    {
		memmove (pScrPriv->crtcs + i, pScrPriv->crtcs + i + 1,
			 (pScrPriv->numCrtcs - (i + 1)) * sizeof (RRCrtcPtr));
		--pScrPriv->numCrtcs;
		break;
	    }
	}
    }
    if (crtc->gammaRed)
	xfree (crtc->gammaRed);
    if (crtc->mode)
	RRModeDestroy (crtc->mode);
    xfree (crtc);
    return 1;
}

/*
 * Request that the Crtc gamma be changed
 */

Bool
RRCrtcGammaSet (RRCrtcPtr   crtc,
		CARD16	    *red,
		CARD16	    *green,
		CARD16	    *blue)
{
    Bool	ret = TRUE;
#if RANDR_12_INTERFACE
    ScreenPtr	pScreen = crtc->pScreen;
#endif
    
    memcpy (crtc->gammaRed, red, crtc->gammaSize * sizeof (CARD16));
    memcpy (crtc->gammaGreen, green, crtc->gammaSize * sizeof (CARD16));
    memcpy (crtc->gammaBlue, blue, crtc->gammaSize * sizeof (CARD16));
#if RANDR_12_INTERFACE
    if (pScreen)
    {
	rrScrPriv(pScreen);
	if (pScrPriv->rrCrtcSetGamma)
	    ret = (*pScrPriv->rrCrtcSetGamma) (pScreen, crtc);
    }
#endif
    return ret;
}

/*
 * Notify the extension that the Crtc gamma has been changed
 * The driver calls this whenever it has changed the gamma values
 * in the RRCrtcRec
 */

Bool
RRCrtcGammaNotify (RRCrtcPtr	crtc)
{
    return TRUE;    /* not much going on here */
}

/**
 * Returns the width/height that the crtc scans out from the framebuffer
 */
void
RRCrtcGetScanoutSize(RRCrtcPtr crtc, int *width, int *height)
{
    if (crtc->mode == NULL) {
	*width = 0;
	*height = 0;
	return;
    }

    switch (crtc->rotation & 0xf) {
    case RR_Rotate_0:
    case RR_Rotate_180:
	*width = crtc->mode->mode.width;
	*height = crtc->mode->mode.height;
	break;
    case RR_Rotate_90:
    case RR_Rotate_270:
	*width = crtc->mode->mode.height;
	*height = crtc->mode->mode.width;
	break;
    }
}

/*
 * Set the size of the gamma table at server startup time
 */

Bool
RRCrtcGammaSetSize (RRCrtcPtr	crtc,
		    int		size)
{
    CARD16  *gamma;

    if (size == crtc->gammaSize)
	return TRUE;
    if (size)
    {
	gamma = xalloc (size * 3 * sizeof (CARD16));
	if (!gamma)
	    return FALSE;
    }
    else
	gamma = NULL;
    if (crtc->gammaRed)
	xfree (crtc->gammaRed);
    crtc->gammaRed = gamma;
    crtc->gammaGreen = gamma + size;
    crtc->gammaBlue = gamma + size*2;
    crtc->gammaSize = size;
    return TRUE;
}

/*
 * Set the allowable rotations of the CRTC.
 */
Bool
RRCrtcSetRotations (RRCrtcPtr crtc,
		    Rotation rotations)
{
    crtc->rotations = rotations;
    return TRUE;
}

/*
 * Initialize crtc type
 */
Bool
RRCrtcInit (void)
{
    RRCrtcType = CreateNewResourceType (RRCrtcDestroyResource);
    if (!RRCrtcType)
	return FALSE;
#ifdef XResExtension
	RegisterResourceName (RRCrtcType, "CRTC");
#endif
    return TRUE;
}

int
ProcRRGetCrtcInfo (ClientPtr client)
{
    REQUEST(xRRGetCrtcInfoReq);
    xRRGetCrtcInfoReply	rep;
    RRCrtcPtr			crtc;
    CARD8			*extra;
    unsigned long		extraLen;
    ScreenPtr			pScreen;
    rrScrPrivPtr		pScrPriv;
    RRModePtr			mode;
    RROutput			*outputs;
    RROutput			*possible;
    int				i, j, k, n;
    
    REQUEST_SIZE_MATCH(xRRGetCrtcInfoReq);
    crtc = LookupCrtc(client, stuff->crtc, DixReadAccess);

    if (!crtc)
	return RRErrorBase + BadRRCrtc;

    /* All crtcs must be associated with screens before client
     * requests are processed
     */
    pScreen = crtc->pScreen;
    pScrPriv = rrGetScrPriv(pScreen);

    mode = crtc->mode;
    
    rep.type = X_Reply;
    rep.status = RRSetConfigSuccess;
    rep.sequenceNumber = client->sequence;
    rep.length = 0;
    rep.timestamp = pScrPriv->lastSetTime.milliseconds;
    rep.x = crtc->x;
    rep.y = crtc->y;
    rep.width = mode ? mode->mode.width : 0;
    rep.height = mode ? mode->mode.height : 0;
    rep.mode = mode ? mode->mode.id : 0;
    rep.rotation = crtc->rotation;
    rep.rotations = crtc->rotations;
    rep.nOutput = crtc->numOutputs;
    k = 0;
    for (i = 0; i < pScrPriv->numOutputs; i++)
	for (j = 0; j < pScrPriv->outputs[i]->numCrtcs; j++)
	    if (pScrPriv->outputs[i]->crtcs[j] == crtc)
		k++;
    rep.nPossibleOutput = k;
    
    rep.length = rep.nOutput + rep.nPossibleOutput;

    extraLen = rep.length << 2;
    if (extraLen)
    {
	extra = xalloc (extraLen);
	if (!extra)
	    return BadAlloc;
    }
    else
	extra = NULL;

    outputs = (RROutput *) extra;
    possible = (RROutput *) (outputs + rep.nOutput);
    
    for (i = 0; i < crtc->numOutputs; i++)
    {
	outputs[i] = crtc->outputs[i]->id;
	if (client->swapped)
	    swapl (&outputs[i], n);
    }
    k = 0;
    for (i = 0; i < pScrPriv->numOutputs; i++)
	for (j = 0; j < pScrPriv->outputs[i]->numCrtcs; j++)
	    if (pScrPriv->outputs[i]->crtcs[j] == crtc)
	    {
		possible[k] = pScrPriv->outputs[i]->id;
		if (client->swapped)
		    swapl (&possible[k], n);
		k++;
	    }
    
    if (client->swapped) {
	swaps(&rep.sequenceNumber, n);
	swapl(&rep.length, n);
	swapl(&rep.timestamp, n);
	swaps(&rep.x, n);
	swaps(&rep.y, n);
	swaps(&rep.width, n);
	swaps(&rep.height, n);
	swapl(&rep.mode, n);
	swaps(&rep.rotation, n);
	swaps(&rep.rotations, n);
	swaps(&rep.nOutput, n);
	swaps(&rep.nPossibleOutput, n);
    }
    WriteToClient(client, sizeof(xRRGetCrtcInfoReply), (char *)&rep);
    if (extraLen)
    {
	WriteToClient (client, extraLen, (char *) extra);
	xfree (extra);
    }
    
    return client->noClientException;
}

int
ProcRRSetCrtcConfig (ClientPtr client)
{
    REQUEST(xRRSetCrtcConfigReq);
    xRRSetCrtcConfigReply   rep;
    ScreenPtr		    pScreen;
    rrScrPrivPtr	    pScrPriv;
    RRCrtcPtr		    crtc;
    RRModePtr		    mode;
    int			    numOutputs;
    RROutputPtr		    *outputs = NULL;
    RROutput		    *outputIds;
    TimeStamp		    configTime;
    TimeStamp		    time;
    Rotation		    rotation;
    int			    i, j;
    
    REQUEST_AT_LEAST_SIZE(xRRSetCrtcConfigReq);
    numOutputs = (stuff->length - (SIZEOF (xRRSetCrtcConfigReq) >> 2));
    
    crtc = LookupIDByType (stuff->crtc, RRCrtcType);
    if (!crtc)
    {
	client->errorValue = stuff->crtc;
	return RRErrorBase + BadRRCrtc;
    }
    if (stuff->mode == None)
    {
	mode = NULL;
	if (numOutputs > 0)
	    return BadMatch;
    }
    else
    {
	mode = LookupIDByType (stuff->mode, RRModeType);
	if (!mode)
	{
	    client->errorValue = stuff->mode;
	    return RRErrorBase + BadRRMode;
	}
	if (numOutputs == 0)
	    return BadMatch;
    }
    if (numOutputs)
    {
	outputs = xalloc (numOutputs * sizeof (RROutputPtr));
	if (!outputs)
	    return BadAlloc;
    }
    else
	outputs = NULL;
    
    outputIds = (RROutput *) (stuff + 1);
    for (i = 0; i < numOutputs; i++)
    {
	outputs[i] = (RROutputPtr) LookupIDByType (outputIds[i], RROutputType);
	if (!outputs[i])
	{
	    client->errorValue = outputIds[i];
	    if (outputs)
		xfree (outputs);
	    return RRErrorBase + BadRROutput;
	}
	/* validate crtc for this output */
	for (j = 0; j < outputs[i]->numCrtcs; j++)
	    if (outputs[i]->crtcs[j] == crtc)
		break;
	if (j == outputs[i]->numCrtcs)
	{
	    if (outputs)
		xfree (outputs);
	    return BadMatch;
	}
	/* validate mode for this output */
	for (j = 0; j < outputs[i]->numModes + outputs[i]->numUserModes; j++)
	{
	    RRModePtr	m = (j < outputs[i]->numModes ? 
			     outputs[i]->modes[j] :
			     outputs[i]->userModes[j - outputs[i]->numModes]);
	    if (m == mode)
		break;
	}
	if (j == outputs[i]->numModes + outputs[i]->numUserModes)
	{
	    if (outputs)
		xfree (outputs);
	    return BadMatch;
	}
    }
    /* validate clones */
    for (i = 0; i < numOutputs; i++)
    {
	for (j = 0; j < numOutputs; j++)
	{
	    int k;
	    if (i == j)
		continue;
	    for (k = 0; k < outputs[i]->numClones; k++)
	    {
		if (outputs[i]->clones[k] == outputs[j])
		    break;
	    }
	    if (k == outputs[i]->numClones)
	    {
		if (outputs)
		    xfree (outputs);
		return BadMatch;
	    }
	}
    }

    pScreen = crtc->pScreen;
    pScrPriv = rrGetScrPriv(pScreen);
    
    time = ClientTimeToServerTime(stuff->timestamp);
    configTime = ClientTimeToServerTime(stuff->configTimestamp);
    
    if (!pScrPriv)
    {
	time = currentTime;
	rep.status = RRSetConfigFailed;
	goto sendReply;
    }
    
    /*
     * if the client's config timestamp is not the same as the last config
     * timestamp, then the config information isn't up-to-date and
     * can't even be validated
     */
    if (CompareTimeStamps (configTime, pScrPriv->lastConfigTime) != 0)
    {
	rep.status = RRSetConfigInvalidConfigTime;
	goto sendReply;
    }
    
    /*
     * Validate requested rotation
     */
    rotation = (Rotation) stuff->rotation;

    /* test the rotation bits only! */
    switch (rotation & 0xf) {
    case RR_Rotate_0:
    case RR_Rotate_90:
    case RR_Rotate_180:
    case RR_Rotate_270:
	break;
    default:
	/*
	 * Invalid rotation
	 */
	client->errorValue = stuff->rotation;
	if (outputs)
	    xfree (outputs);
	return BadValue;
    }

    if (mode)
    {
	if ((~crtc->rotations) & rotation)
	{
	    /*
	     * requested rotation or reflection not supported by screen
	     */
	    client->errorValue = stuff->rotation;
	    if (outputs)
		xfree (outputs);
	    return BadMatch;
	}
    
#ifdef RANDR_12_INTERFACE
	/*
	 * Check screen size bounds if the DDX provides a 1.2 interface
	 * for setting screen size. Else, assume the CrtcSet sets
	 * the size along with the mode
	 */
	if (pScrPriv->rrScreenSetSize)
	{
	    int source_width = mode->mode.width;
	    int	source_height = mode->mode.height;

	    if (rotation == RR_Rotate_90 || rotation == RR_Rotate_270)
	    {
		source_width = mode->mode.height;
		source_height = mode->mode.width;
	    }
	    if (stuff->x + source_width > pScreen->width)
	    {
		client->errorValue = stuff->x;
		if (outputs)
		    xfree (outputs);
		return BadValue;
	    }
	    
	    if (stuff->y + source_height > pScreen->height)
	    {
		client->errorValue = stuff->y;
		if (outputs)
		    xfree (outputs);
		return BadValue;
	    }
	}
#endif
    }
    
    /*
     * Make sure the requested set-time is not older than
     * the last set-time
     */
    if (CompareTimeStamps (time, pScrPriv->lastSetTime) < 0)
    {
	rep.status = RRSetConfigInvalidTime;
	goto sendReply;
    }

    if (!RRCrtcSet (crtc, mode, stuff->x, stuff->y,
		   rotation, numOutputs, outputs))
    {
	rep.status = RRSetConfigFailed;
	goto sendReply;
    }
    rep.status = RRSetConfigSuccess;
    
sendReply:
    if (outputs)
	xfree (outputs);
    
    rep.type = X_Reply;
    /* rep.status has already been filled in */
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.newTimestamp = pScrPriv->lastConfigTime.milliseconds;

    if (client->swapped) 
    {
	int n;
    	swaps(&rep.sequenceNumber, n);
    	swapl(&rep.length, n);
	swapl(&rep.newTimestamp, n);
    }
    WriteToClient(client, sizeof(xRRSetCrtcConfigReply), (char *)&rep);
    
    return client->noClientException;
}

int
ProcRRGetCrtcGammaSize (ClientPtr client)
{
    REQUEST(xRRGetCrtcGammaSizeReq);
    xRRGetCrtcGammaSizeReply	reply;
    RRCrtcPtr			crtc;
    int				n;

    REQUEST_SIZE_MATCH(xRRGetCrtcGammaSizeReq);
    crtc = LookupCrtc (client, stuff->crtc, DixReadAccess);
    if (!crtc)
	return RRErrorBase + BadRRCrtc;
    
    reply.type = X_Reply;
    reply.sequenceNumber = client->sequence;
    reply.length = 0;
    reply.size = crtc->gammaSize;
    if (client->swapped) {
	swaps (&reply.sequenceNumber, n);
	swapl (&reply.length, n);
	swaps (&reply.size, n);
    }
    WriteToClient (client, sizeof (xRRGetCrtcGammaSizeReply), (char *) &reply);
    return client->noClientException;
}

int
ProcRRGetCrtcGamma (ClientPtr client)
{
    REQUEST(xRRGetCrtcGammaReq);
    xRRGetCrtcGammaReply	reply;
    RRCrtcPtr			crtc;
    int				n;
    unsigned long		len;
    
    REQUEST_SIZE_MATCH(xRRGetCrtcGammaReq);
    crtc = LookupCrtc (client, stuff->crtc, DixReadAccess);
    if (!crtc)
	return RRErrorBase + BadRRCrtc;
    
    len = crtc->gammaSize * 3 * 2;
    
    reply.type = X_Reply;
    reply.sequenceNumber = client->sequence;
    reply.length = (len + 3) >> 2;
    reply.size = crtc->gammaSize;
    if (client->swapped) {
	swaps (&reply.sequenceNumber, n);
	swapl (&reply.length, n);
	swaps (&reply.size, n);
    }
    WriteToClient (client, sizeof (xRRGetCrtcGammaReply), (char *) &reply);
    if (crtc->gammaSize)
    {
	client->pSwapReplyFunc = (ReplySwapPtr)CopySwap16Write;
	WriteSwappedDataToClient (client, len, (char *) crtc->gammaRed);
    }
    return client->noClientException;
}

int
ProcRRSetCrtcGamma (ClientPtr client)
{
    REQUEST(xRRSetCrtcGammaReq);
    RRCrtcPtr			crtc;
    unsigned long		len;
    CARD16			*red, *green, *blue;
    
    REQUEST_AT_LEAST_SIZE(xRRSetCrtcGammaReq);
    crtc = LookupCrtc (client, stuff->crtc, DixWriteAccess);
    if (!crtc)
	return RRErrorBase + BadRRCrtc;
    
    len = client->req_len - (sizeof (xRRSetCrtcGammaReq) >> 2);
    if (len < (stuff->size * 3 + 1) >> 1)
	return BadLength;

    if (stuff->size != crtc->gammaSize)
	return BadMatch;
    
    red = (CARD16 *) (stuff + 1);
    green = red + crtc->gammaSize;
    blue = green + crtc->gammaSize;
    
    RRCrtcGammaSet (crtc, red, green, blue);

    return Success;
}

