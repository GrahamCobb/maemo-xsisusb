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

#define NEED_EVENTS
#define NEED_REPLIES

#include <tslib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/extensions/xspwire.h>
#include <X11/extensions/xspproto.h>
#include <X11/extensions/XI.h>
#include <X11/extensions/XIproto.h>

#include "misc.h"
#include "os.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "scrnintstr.h"
#include "inputstr.h"
#include "XIstubs.h"
#include "spext.h"

#define XSP_EXTENSION_NAME "XSP"
#define XSP_NUMBER_EVENTS  XSPNumberEvents
#define XSP_NUMBER_ERRORS  XSPNumberErrors

#define XSP_EXTENSION_MAJOR         1
#define XSP_EXTENSION_MINOR         0
#define XSP_EXTENSION_PATCHLEVEL    1

extern int THUMB_PRESSURE;

/*
 * for /dev/dspctl/ctl
 */

#define OMAP_DSP_IOCTL_FBEN                     53
#define OMAP_DSP_IOCTL_FBDIS                    54


static unsigned char    XSPReqCode;
int                     XSPEventBase;
int                     XSPReqBase;
int                     XSPErrorBase;

int xspScrPrivateIndex = -1;
static int XSPGeneration = -1;

static ClientPtr xsp_ts_client;
static DeviceIntPtr xsp_ts_dev;
static void XSPClientCallback (CallbackListPtr *list,
        pointer closure,
        pointer data);


extern void (*tslib_raw_event_hook)(int x, int y, int pressure, void *closure);
extern void *tslib_raw_event_closure;
void (*xsp_dsp_event_hook)(void);

int xsp_init_screen(ScreenPtr pScreen);

static DISPATCH_PROC(ProcXSPDispatch);
static DISPATCH_PROC(SProcXSPDispatch);
static void ProcXSPCloseDown(ExtensionEntry *);

static DISPATCH_PROC(ProcXSPQueryVersion);
static DISPATCH_PROC(ProcXSPSetTSRawMode);
static DISPATCH_PROC(ProcXSPSetTSCalibration);
static DISPATCH_PROC(ProcXSPRegisterDSPArea);
static DISPATCH_PROC(ProcXSPCancelDSPArea);
static DISPATCH_PROC(ProcXSPSetPixelDoubling);
static DISPATCH_PROC(ProcXSPSetThumbPressure);

static DISPATCH_PROC(SProcXSPQueryVersion);
static DISPATCH_PROC(SProcXSPSetTSRawMode);
static DISPATCH_PROC(SProcXSPSetTSCalibration);
static DISPATCH_PROC(SProcXSPRegisterDSPArea);
static DISPATCH_PROC(SProcXSPCancelDSPArea);
static DISPATCH_PROC(SProcXSPSetPixelDoubling);
static DISPATCH_PROC(SProcXSPSetThumbPressure);

void ProcXSPCloseDown (ExtensionEntry*);

static int rects_intersect(xRectangle *area1, xRectangle *area2)
{
    return !(area2->x + area2->width <= area1->x ||
             area2->x >= area1->x + area1->width ||
             area2->y + area2->height <= area2->y ||
             area2->y >= area1->y + area1->height);
}

static void xsp_ts_event_hook (int x, int y, int pressure, void *closure)
{
    ClientPtr pClient = (ClientPtr) closure;
    xXSPRawTouchscreenEvent        ev;

    ev.type = XSPEventBase + X_XSPTSRaw;
    ev.sequenceNumber = pClient->sequence;
    ev.x = x;
    ev.y = y;
    ev.pressure = pressure;

    if (!pClient->clientGone)
        WriteEventsToClient (pClient, 1, (xEvent *) &ev);
}


int xsp_init_screen(ScreenPtr pScreen)
{
    xspScrPrivPtr pScrPriv;

    if (XSPGeneration != serverGeneration) {
        if ((xspScrPrivateIndex = AllocateScreenPrivateIndex()) == -1) {
            FatalError("XSPExtensionInit: AllocateScreenPrivateIndex failed");
            return FALSE;
        }

        XSPGeneration = serverGeneration;
    }

    if (pScreen->devPrivates[xspScrPrivateIndex].ptr) {
        return TRUE; /* already allocated */
    }

    pScrPriv = (xspScrPrivPtr)xalloc(sizeof (xspScrPrivRec));
    if (!pScrPriv) {
        return FALSE;
    }

    memset(pScrPriv, '\0', sizeof(xspScrPrivRec));
    pScreen->devPrivates[xspScrPrivateIndex].ptr = (pointer)pScrPriv;

    return TRUE;
}

void XSPExtensionInit(void)
{
    ExtensionEntry *extEntry;
    int s;

    if (!AddCallback(&ClientStateCallback, XSPClientCallback, 0))
        return;

    extEntry = AddExtension(XSP_EXTENSION_NAME,
            XSP_NUMBER_EVENTS,
            XSP_NUMBER_ERRORS,
            ProcXSPDispatch, SProcXSPDispatch,
            ProcXSPCloseDown,
            StandardMinorOpcode);
    if (!extEntry)
    {
        FatalError("XSPExtensionInit: AddExtension failed");
        return;
    }

    for (s=0; s < screenInfo.numScreens; s++) {
        if (!xsp_init_screen(screenInfo.screens[s])) {
            FatalError("XSP :: xsp_init_screen failed");
            return;
        }
    }
    XSPReqCode = (unsigned char)extEntry->base;
    XSPEventBase = extEntry->eventBase;
    XSPErrorBase = extEntry->errorBase;
    xsp_ts_client = 0;
}

static int ProcXSPDispatch(client)
    register ClientPtr	client;
{
    REQUEST(xReq);
    switch (stuff->data)
    {
        case X_XSPQueryVersion:
            return ProcXSPQueryVersion(client);
        case X_XSPSetTSCalibration:
            return ProcXSPSetTSCalibration(client);
        case X_XSPSetTSRawMode:
            return ProcXSPSetTSRawMode(client);
        case X_XSPRegisterDSPArea:
            return ProcXSPRegisterDSPArea(client);
        case X_XSPCancelDSPArea:
            return ProcXSPCancelDSPArea(client);
        case X_XSPSetPixelDoubling:
            return ProcXSPSetPixelDoubling(client);
	case X_XSPSetThumbPressure:
	    return ProcXSPSetThumbPressure(client);
	default:
            return BadRequest;
    }
}

static int SProcXSPDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data)
    {
        case X_XSPQueryVersion:
            return SProcXSPQueryVersion(client);
        case X_XSPSetTSCalibration:
            return SProcXSPSetTSCalibration(client);
        case X_XSPSetTSRawMode:
            return SProcXSPSetTSRawMode(client);
        case X_XSPRegisterDSPArea:
            return SProcXSPRegisterDSPArea(client);
        case X_XSPCancelDSPArea:
            return SProcXSPCancelDSPArea(client);
	case X_XSPSetPixelDoubling:
	    return SProcXSPSetPixelDoubling(client);
	case X_XSPSetThumbPressure:
	    return SProcXSPSetThumbPressure(client);
        default:
            ErrorF("XSP: BadRequest: %d\n", stuff->data);
            return BadRequest;
    }
}


static int ProcXSPQueryVersion(client)
    register ClientPtr	client;
{
    xXSPQueryVersionReply rep;
    register int n;

    REQUEST_SIZE_MATCH(xXSPQueryVersionReq);
    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.majorVersion = XSP_VERSION_MAJOR;
    rep.minorVersion = XSP_VERSION_MINOR;
    if (client->swapped) {
        swaps(&rep.sequenceNumber, n);
        swaps(&rep.minorVersion, n);
    }
    WriteToClient(client, sizeof(xXSPQueryVersionReply), (char *)&rep);
    return(client->noClientException);
}

static int SProcXSPQueryVersion(ClientPtr client)
{
    register int n;
    REQUEST(xXSPQueryVersionReq);

    swaps(&stuff->length, n);
    REQUEST_SIZE_MATCH(xXSPQueryVersionReq);
    swaps(&stuff->minorVersion, n);
    return ProcXSPQueryVersion(client);
}

static DeviceIntPtr find_touchscreen(void)
{
    Atom touchscreen = MakeAtom(XI_TOUCHSCREEN, strlen(XI_TOUCHSCREEN), 1);
    DeviceIntPtr pDev;

    for (pDev = inputInfo.devices; pDev; pDev = pDev->next) {
        if (pDev->type == touchscreen)
            return pDev;
    }

    return NULL;
}

static int ProcXSPSetTSRawMode (ClientPtr client)
{
    REQUEST(xXSPSetTSRawModeReq);
    xXSPSetTSRawModeReply rep;
    TSRawEvent *rawevent = NULL;
    DeviceIntPtr pDev = NULL;

    REQUEST_SIZE_MATCH (xXSPSetTSRawModeReq);

    memset (&rep, 0, sizeof (rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;

    if (stuff->on) {
        if (xsp_ts_client == NULL) {
            /* Start calibrating.  */
            rep.status = AlreadyGrabbed; /* FIXME blatantly wrong */
            pDev = find_touchscreen();
            if (pDev) {
                xsp_ts_client = client;
                xsp_ts_dev = pDev;
                rawevent = (TSRawEvent *)xcalloc(sizeof(rawevent), 1);
                if (rawevent) {
                    rawevent->control = DEVICE_RAWEVENT;
                    rawevent->length = sizeof(rawevent);
                    rawevent->hook = xsp_ts_event_hook;
                    rawevent->closure = client;
                    ChangeDeviceControl(client, pDev,
                                        (xDeviceCtl *) rawevent);
                    xfree(rawevent);
                    rep.status = GrabSuccess;
                }
            }
        }
        else {
            rep.status = AlreadyGrabbed;
        }
    }
    else {
        if (xsp_ts_client == client) {
            /* Stop calibrating.  */
            rawevent = (TSRawEvent *)xcalloc(sizeof(TSRawEvent), 1);
            if (rawevent) {
                rawevent->control = DEVICE_RAWEVENT;
                rawevent->length = sizeof(rawevent);
                rawevent->hook = NULL;
                rawevent->closure = NULL;
                ChangeDeviceControl(xsp_ts_client, xsp_ts_dev, (xDeviceCtl *) rawevent);
                xfree(rawevent);
                xsp_ts_client = NULL;
                xsp_ts_dev = NULL;
                rep.status = GrabSuccess;
            }
        }
        else {
            rep.status = AlreadyGrabbed;
        }
    }

    if (client->swapped) {
        int n;

        swaps (&rep.sequenceNumber, n);
        swaps (&rep.status, n);
    }
    WriteToClient(client, sizeof (rep), (char *) &rep);
    return (client->noClientException);
}

static int SProcXSPSetTSRawMode (ClientPtr client)
{
    REQUEST(xXSPSetTSRawModeReq);
    int n;

    REQUEST_SIZE_MATCH (xXSPSetTSRawModeReq);

    swaps(&stuff->on, n);

    return ProcXSPSetTSRawMode(client);
}

static int ProcXSPSetTSCalibration(ClientPtr client)
{
    REQUEST(xXSPSetTSCalibrationReq);
    xXSPSetTSCalibrationReply rep;
    DeviceIntPtr pDev;

    REQUEST_SIZE_MATCH (xXSPSetTSCalibrationReq);

    memset (&rep, 0, sizeof (rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.status = 0;

    if (ts_write_calibration(stuff->calconst1,
                             stuff->calconst2,
                             stuff->calconst3,
                             stuff->calconst4,
                             stuff->calconst5,
                             stuff->calconst6,
                             stuff->calconst7) >= 0) {
        /* cycle input to take new values into use */
        pDev = find_touchscreen();
        if (pDev) {
            DisableDevice(pDev);
            if (EnableDevice(pDev))
                rep.status = 1;
            else
                ErrorF("xsp/calibration: failed to re-enable touchscreen\n");
        }
    }


    if (client->swapped) {
        int n;

        swaps (&rep.sequenceNumber, n);
        swaps (&rep.status, n);
    }

    WriteToClient(client, sizeof (rep), (char *) &rep);
    return (client->noClientException);
}


static int SProcXSPSetTSCalibration(ClientPtr client)
{
    REQUEST(xXSPSetTSCalibrationReq);
    register int n;
    REQUEST_SIZE_MATCH(xXSPSetTSCalibrationReq);

    swaps(&stuff->calconst1, n);
    swaps(&stuff->calconst2, n);
    swaps(&stuff->calconst3, n);
    swaps(&stuff->calconst4, n);
    swaps(&stuff->calconst5, n);
    swaps(&stuff->calconst6, n);
    swaps(&stuff->calconst7, n);

    return ProcXSPSetTSCalibration(client);
}


static int ProcXSPRegisterDSPArea(ClientPtr client)
{

    REQUEST(xXSPRegisterDSPAreaReq);
    xXSPRegisterDSPAreaReply rep;
    register int n;
    int dsp_ctl_fd;
    xRectangle rect;
    REQUEST_SIZE_MATCH(xXSPRegisterDSPAreaReq);

    ScreenPtr scr;
    xspScrPrivPtr pScrPriv;

    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.status = 1;

    if (stuff->screen_num < 0 || stuff->screen_num > (screenInfo.numScreens - 1)) {
        rep.status = 0;
        goto getout;
    }

    scr = screenInfo.screens[stuff->screen_num];
    pScrPriv = (pointer)scr->devPrivates[xspScrPrivateIndex].ptr;
    rect.x = stuff->x;
    rect.y = stuff->y;
    rect.width = stuff->width;
    rect.height = stuff->height;

    if ((dsp_ctl_fd = open("/dev/dspctl/ctl", O_RDWR)) < 0) {
        ErrorF("Xomap: Unable to open /dev/dspctl/ctl to start DSP\n");
        rep.status = 0;
    } else {
        if (ioctl(dsp_ctl_fd, OMAP_DSP_IOCTL_FBEN) < 0) {
            ErrorF("Xomap: ioctl OMAP_DSP_IOCTL_FBDBEN failed");
            rep.status = 0;
        }
        close(dsp_ctl_fd);
    }

    if (rep.status == 1) {
        pScrPriv->dsp_enabled = 1;
        pScrPriv->dsp_client = client;
        memcpy(&pScrPriv->dsp_area, &rect, sizeof(rect));
    }

getout:
    if (client->swapped) {
        swaps(&rep.sequenceNumber, n);
        swaps(&rep.status, n);
    }

    WriteToClient(client, sizeof(rep), (char *)&rep);
    return (client->noClientException);
}

static int SProcXSPRegisterDSPArea(ClientPtr client)
{
    REQUEST(xXSPRegisterDSPAreaReq);
    register int n;
    REQUEST_SIZE_MATCH(xXSPRegisterDSPAreaReq);
    swaps(&stuff->screen_num, n);
    swaps(&stuff->x, n);
    swaps(&stuff->y, n);
    swaps(&stuff->width, n);
    swaps(&stuff->height, n);

    return ProcXSPRegisterDSPArea(client);
}

static void stop_dsp(xspScrPrivPtr pScrPriv)
{
    int dsp_ctl_fd;

    pScrPriv->dsp_client = NULL;
    pScrPriv->dsp_enabled = 0;
    pScrPriv->dsp_area.x = 0xff;
    pScrPriv->dsp_area.y = 0xff;
    pScrPriv->dsp_area.width = 0;
    pScrPriv->dsp_area.height = 0;

    dsp_ctl_fd = open("/dev/dspctl/ctl", O_RDWR);
    if (dsp_ctl_fd < 0) {
        ErrorF("Unable to open /dev/dspctl/ctl to stop DSP\n");
        return;
    }

    if (ioctl(dsp_ctl_fd, OMAP_DSP_IOCTL_FBDIS) < 0) {
        DebugF("OMAP_DSP_IOCTL_FBDIS failed\n");
        return;
    }

    close(dsp_ctl_fd);
}

static int ProcXSPCancelDSPArea(ClientPtr client)
{
    REQUEST(xXSPCancelDSPAreaReq);
    xXSPCancelDSPAreaReply rep;
    REQUEST_SIZE_MATCH(xXSPCancelDSPAreaReq);
    xspScrPrivPtr pScrPriv;

    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.status = 1;

    if (stuff->screen_num < 0 || stuff->screen_num >= screenInfo.numScreens) {
        rep.status = 0;
        goto getout;
    }

    pScrPriv = xspGetScrPriv(screenInfo.screens[stuff->screen_num]);
    stop_dsp(pScrPriv);
    rep.status = !pScrPriv->dsp_enabled;

getout:
    if (client->swapped) {
        int n;

        swaps(&rep.sequenceNumber, n);
        swaps(&rep.status, n);
    }

    WriteToClient(client, sizeof(rep), (char *)&rep);
    return (client->noClientException);
}

static int SProcXSPCancelDSPArea(ClientPtr client)
{
    REQUEST(xXSPCancelDSPAreaReq);
    register int n;
    REQUEST_SIZE_MATCH(xXSPCancelDSPAreaReq);
    swaps(&stuff->screen_num, n);

    return ProcXSPCancelDSPArea(client);
}

static int ProcXSPSetThumbPressure(ClientPtr client)
{
    REQUEST(xXSPSetThumbPressureReq);
    REQUEST_SIZE_MATCH(xXSPSetThumbPressureReq);
    DeviceIntPtr pDev = NULL;
    Atom touchscreen = MakeAtom(XI_TOUCHSCREEN, strlen(XI_TOUCHSCREEN), 1);

    if (stuff->pressure > 0 && stuff->pressure < 255) {
        for (pDev = inputInfo.devices; pDev; pDev = pDev->next) {
            if (pDev->type == touchscreen) {
                if (pDev->absolute)
                    pDev->absolute->button_threshold = stuff->pressure;
                else
                    return BadMatch;
                break;
            }
        }
    }
    else
      return BadValue;
    
    return (client->noClientException);
}

static int SProcXSPSetThumbPressure(ClientPtr client)
{
    REQUEST(xXSPSetThumbPressureReq);
    register int n;
    REQUEST_SIZE_MATCH(xXSPSetThumbPressureReq);

    swaps(&stuff->screen_num, n);
    swaps(&stuff->pressure, n);

    return (client->noClientException);
}


static int ProcXSPSetPixelDoubling(ClientPtr client)
{
    REQUEST(xXSPSetPixelDoublingReq);
    xXSPSetPixelDoublingReply rep;
    REQUEST_SIZE_MATCH(xXSPSetPixelDoublingReq);
    xspScrPrivPtr pScrPriv;
    int ret;

    memset(&rep, 0, sizeof(rep));
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;

    if (stuff->screen_num < 0 || stuff->screen_num > (screenInfo.numScreens - 1)) {
        rep.status = 0;
        goto getout;
    }

    pScrPriv = screenInfo.screens[stuff->screen_num]->devPrivates[xspScrPrivateIndex].ptr;

    if (pScrPriv->callback) {
        ret = pScrPriv->callback(stuff->state ? XSP_EVENT_PIXEL_DOUBLE_EN :
                                                XSP_EVENT_PIXEL_DOUBLE_DIS,
                                 stuff->screen_num, pScrPriv->closure);
        if (ret == Success) {
            if (stuff->state) {
                pScrPriv->pixel_doubling = 1;
                pScrPriv->pixel_doubling_client = client;
            }
            else {
                pScrPriv->pixel_doubling = 0;
            }
            rep.status = 0;
        }
        else {
            /* status always seems to be 0? */
            rep.status = 0;
        }
    }


getout:
    if (client->swapped) {
        int n;

        swaps(&rep.sequenceNumber, n);
        swaps(&rep.status, n);
    }

    WriteToClient(client, sizeof(rep), (char *)&rep);
    return (client->noClientException);
}


static int SProcXSPSetPixelDoubling(ClientPtr client)
{
    REQUEST(xXSPSetPixelDoublingReq);
    register int n;
    REQUEST_SIZE_MATCH(xXSPSetPixelDoublingReq);
    swaps(&stuff->screen_num, n);
    swaps(&stuff->state, n);

    return ProcXSPSetPixelDoubling(client);
}



static void XSPClientCallback (CallbackListPtr *list,
        pointer closure,
        pointer data)
{
    NewClientInfoRec    *clientinfo = (NewClientInfoRec *) data;
    ClientPtr           pClient = clientinfo->client;
    xspScrPrivPtr pScrPriv;
    TSRawEvent *rawevent = NULL;
    int s;

    if (clientinfo->setup != NULL)
        return;

    if (xsp_ts_client != NULL && xsp_ts_client == pClient) {
        /* Stop calibrating.  */
        rawevent = (TSRawEvent *)xcalloc(sizeof(TSRawEvent), 1);
        if (!rawevent)
            return;
        rawevent->control = DEVICE_RAWEVENT;
        rawevent->length = sizeof(rawevent);
        rawevent->hook = NULL;
        rawevent->closure = NULL;
        ChangeDeviceControl(xsp_ts_client, xsp_ts_dev, (xDeviceCtl *) rawevent);
        xfree(rawevent);
        xsp_ts_client = NULL;
        xsp_ts_dev = NULL;
    }

    /* cancel dsp area */
    for (s=0; s < screenInfo.numScreens; s++) {
        pScrPriv = xspGetScrPriv(screenInfo.screens[s]);

        if (pScrPriv->dsp_client != NULL 
            && pScrPriv->dsp_client == pClient) {
            pScrPriv->dsp_enabled = 0;
            pScrPriv->dsp_client = NULL;
            pScrPriv->dsp_area.x = 0xff;
            pScrPriv->dsp_area.y = 0xff;
            pScrPriv->dsp_area.width = 0;
            pScrPriv->dsp_area.height = 0;
        }

        if (pScrPriv->pixel_doubling && pScrPriv->pixel_doubling_client &&
            pScrPriv->pixel_doubling_client == pClient) {
            if (pScrPriv->callback)
                pScrPriv->callback(XSP_EVENT_PIXEL_DOUBLE_DIS, s,
                                   pScrPriv->closure);

            pScrPriv->pixel_doubling = 0;
            pScrPriv->pixel_doubling_client = NULL;
        }
    }
}

int XSPSetEventCallback(int screen, xsp_event_callback callback, void *closure)
{
    xspScrPrivPtr pScrPriv = NULL;

    if (screen < 0 || screen >= screenInfo.numScreens || XSPGeneration == -1)
        return BadValue;

    pScrPriv = xspGetScrPriv(screenInfo.screens[screen]);
    pScrPriv->callback = callback;
    pScrPriv->closure = closure;

    return Success;
}

void XSPCheckDamage(int screen, xRectangle *area)
{
    xspScrPrivPtr pScrPriv;
    xXSPDSPStoppedEvent xE;

    if (screen < 0 || screen >= screenInfo.numScreens || XSPGeneration == -1)
        return;

    pScrPriv = xspGetScrPriv(screenInfo.screens[screen]);
    if (pScrPriv && pScrPriv->dsp_enabled && pScrPriv->dsp_client) {
        if (rects_intersect(area, &pScrPriv->dsp_area)) {
            xE.type = XSPEventBase + X_XSPDSPStopped;
            xE.sequenceNumber = pScrPriv->dsp_client->sequence;
            if (!pScrPriv->dsp_client->clientGone)
                WriteEventsToClient(pScrPriv->dsp_client, 1, (xEvent *) &xE);

            stop_dsp(pScrPriv);
        }
    }
}

static void ProcXSPCloseDown(ExtensionEntry *entry)
{
    int i;

    if (XSPGeneration == -1)
        return;

    for (i=0; i < screenInfo.numScreens; i++) {
        xfree(screenInfo.screens[i]->devPrivates[xspScrPrivateIndex].ptr);
    }
    xspScrPrivateIndex = -1;
    XSPGeneration = -1;
    return;
}
