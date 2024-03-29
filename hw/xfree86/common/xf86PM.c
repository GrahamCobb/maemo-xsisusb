/*
 * Copyright (c) 2000-2002 by The XFree86 Project, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/X.h>
#include "xf86.h"
#include "xf86Priv.h"
#include "xf86Xinput.h"

int (*xf86PMGetEventFromOs)(int fd,pmEvent *events,int num) = NULL;
pmWait (*xf86PMConfirmEventToOs)(int fd,pmEvent event) = NULL;

static Bool suspended = FALSE;

static int
eventName(pmEvent event, char **str)
{
    switch(event) {
    case XF86_APM_SYS_STANDBY: *str="System Standby Request"; return 0;
    case XF86_APM_SYS_SUSPEND: *str="System Suspend Request"; return 0;
    case XF86_APM_CRITICAL_SUSPEND: *str="Critical Suspend"; return 0;
    case XF86_APM_USER_STANDBY: *str="User System Standby Request"; return 0;
    case XF86_APM_USER_SUSPEND: *str="User System Suspend Request"; return 0;
    case XF86_APM_STANDBY_RESUME: *str="System Standby Resume"; return 0;
    case XF86_APM_NORMAL_RESUME: *str="Normal Resume System"; return 0;
    case XF86_APM_CRITICAL_RESUME: *str="Critical Resume System"; return 0;
    case XF86_APM_LOW_BATTERY: *str="Battery Low"; return 3;
    case XF86_APM_POWER_STATUS_CHANGE: *str="Power Status Change";return 3;
    case XF86_APM_UPDATE_TIME: *str="Update Time";return 3;
    case XF86_APM_CAPABILITY_CHANGED: *str="Capability Changed"; return 3;
    case XF86_APM_STANDBY_FAILED: *str="Standby Request Failed"; return 0;
    case XF86_APM_SUSPEND_FAILED: *str="Suspend Request Failed"; return 0;
    default: *str="Unknown Event"; return 0;
    }
}

static void
suspend (pmEvent event, Bool undo)
{
    int i;
    InputInfoPtr pInfo;

   xf86inSuspend = TRUE;
    
    for (i = 0; i < xf86NumScreens; i++) {
        xf86EnableAccess(xf86Screens[i]);
	if (xf86Screens[i]->EnableDisableFBAccess)
	    (*xf86Screens[i]->EnableDisableFBAccess) (i, FALSE);
    }
#if !defined(__EMX__)
    pInfo = xf86InputDevs;
    while (pInfo) {
	DisableDevice(pInfo->dev);
	pInfo = pInfo->next;
    }
#endif
    xf86EnterServerState(SETUP);
    for (i = 0; i < xf86NumScreens; i++) {
        xf86EnableAccess(xf86Screens[i]);
	if (xf86Screens[i]->PMEvent)
	    xf86Screens[i]->PMEvent(i,event,undo);
	else {
	    xf86Screens[i]->LeaveVT(i, 0);
	    xf86Screens[i]->vtSema = FALSE;
	}
    }
    xf86AccessLeave();      
    xf86AccessLeaveState(); 
}

static void
resume(pmEvent event, Bool undo)
{
    int i;
    InputInfoPtr pInfo;

    xf86AccessEnter();
    xf86EnterServerState(SETUP);
    for (i = 0; i < xf86NumScreens; i++) {
        xf86EnableAccess(xf86Screens[i]);
	if (xf86Screens[i]->PMEvent)
	    xf86Screens[i]->PMEvent(i,event,undo);
	else {
	    xf86Screens[i]->vtSema = TRUE;
	    xf86Screens[i]->EnterVT(i, 0);
	}
    }
    xf86EnterServerState(OPERATING);
    for (i = 0; i < xf86NumScreens; i++) {
        xf86EnableAccess(xf86Screens[i]);
	if (xf86Screens[i]->EnableDisableFBAccess)
	    (*xf86Screens[i]->EnableDisableFBAccess) (i, TRUE);
    }
    SaveScreens(SCREEN_SAVER_FORCER, ScreenSaverReset);
#if !defined(__EMX__)
    pInfo = xf86InputDevs;
    while (pInfo) {
	EnableDevice(pInfo->dev);
	pInfo = pInfo->next;
    }
#endif
    xf86inSuspend = FALSE;
}

static void
DoApmEvent(pmEvent event, Bool undo)
{
    /* 
     * we leave that as a global function for now. I don't know if 
     * this might cause problems in the future. It is a global server 
     * variable therefore it needs to be in a server info structure
     */
    int i, setup = 0;
    
    switch(event) {
#if 0
    case XF86_APM_SYS_STANDBY:
    case XF86_APM_USER_STANDBY:
#endif
    case XF86_APM_SYS_SUSPEND:
    case XF86_APM_CRITICAL_SUSPEND: /*do we want to delay a critical suspend?*/
    case XF86_APM_USER_SUSPEND:
	/* should we do this ? */
	if (!undo && !suspended) {
	    suspend(event,undo);
	    suspended = TRUE;
	} else if (undo && suspended) {
	    resume(event,undo);
	    suspended = FALSE;
	}
	break;
#if 0
    case XF86_APM_STANDBY_RESUME:
#endif
    case XF86_APM_NORMAL_RESUME:
    case XF86_APM_CRITICAL_RESUME:
	if (suspended) {
	    resume(event,undo);
	    suspended = FALSE;
	}
	break;
    default:
	for (i = 0; i < xf86NumScreens; i++) {
	    if (xf86Screens[i]->PMEvent) {
		if (!setup) xf86EnterServerState(SETUP);
		setup = 1;
		xf86EnableAccess(xf86Screens[i]);
		xf86Screens[i]->PMEvent(i,event,undo);
	    }
	}
	if (setup) xf86EnterServerState(OPERATING);
	break;
    }
}

#define MAX_NO_EVENTS 8

void
xf86HandlePMEvents(int fd, pointer data)
{
    pmEvent events[MAX_NO_EVENTS];
    int i,n;
    Bool wait = FALSE;

    if (!xf86PMGetEventFromOs)
	return;

    if ((n = xf86PMGetEventFromOs(fd,events,MAX_NO_EVENTS))) {
	do {
	    for (i = 0; i < n; i++) {
		char *str = NULL;
		int verb = eventName(events[i],&str);

		xf86MsgVerb(X_INFO,verb,"PM Event received: %s\n",str);
		DoApmEvent(events[i],FALSE);
		switch (xf86PMConfirmEventToOs(fd,events[i])) {
		case PM_WAIT:
		    wait = TRUE;
		    break;
		case PM_CONTINUE:
		    wait = FALSE;
		    break;
		case PM_FAILED:
		    DoApmEvent(events[i],TRUE);
		    wait = FALSE;
		    break;
		default:
		    break;
		}
	    }
	    if (wait)
		n = xf86PMGetEventFromOs(fd,events,MAX_NO_EVENTS);
	    else
		break;
	} while (1);
    }
}
