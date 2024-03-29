/*
 * Id: kinput.c,v 1.1 1999/11/02 03:54:46 keithp Exp $
 *
 * Copyright � 1999 Keith Packard
 * Copyright � 2006 Nokia Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the authors not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  The authors make no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * THE AUTHORS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE AUTHORS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
/* $RCSId: xc/programs/Xserver/hw/kdrive/kinput.c,v 1.30 2002/11/13 16:37:39 keithp Exp $ */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif
#include "kdrive.h"
#include "inputstr.h"

#define XK_PUBLISHING
#include <X11/keysym.h>
#if HAVE_X11_XF86KEYSYM_H
#include <X11/XF86keysym.h>
#endif
#include <signal.h>
#include <stdio.h>
#ifdef sun
#include <sys/file.h> /* needed for FNONBLOCK & FASYNC */
#endif

#ifdef XKB
#include <xkbsrv.h>
#endif

#include <X11/extensions/XI.h>
#include <X11/extensions/XIproto.h>
#include "XIstubs.h" /* even though we don't use stubs.  cute, no? */
#include "exevents.h"
#include "extinit.h"
#include "exglobals.h"

#define AtomFromName(x) MakeAtom(x, strlen(x), 1)

struct KdConfigDevice {
    char *line;
    struct KdConfigDevice *next;
};

/* kdKeyboards and kdPointers hold all the real devices. */
static KdKeyboardInfo *kdKeyboards         = NULL;
static KdPointerInfo  *kdPointers          = NULL;
static struct KdConfigDevice *kdConfigKeyboards   = NULL;
static struct KdConfigDevice *kdConfigPointers    = NULL;

static KdKeyboardDriver *kdKeyboardDrivers = NULL;
static KdPointerDriver  *kdPointerDrivers  = NULL;

static xEvent           *kdEvents = NULL;

static Bool		kdInputEnabled;
static Bool		kdOffScreen;
static unsigned long	kdOffScreenTime;
static KdPointerMatrix	kdPointerMatrix = {
   { { 1, 0, 0 },
     { 0, 1, 0 } }
};

void KdResetInputMachine (void);
    
#define IsKeyDown(ki, key) ((ki->keyState[(key) >> 3] >> ((key) & 7)) & 1)
#define KEYMAP(ki)        (ki->common.dixdev->key->curKeySyms)
#define KEYMAPDDX(ki)     (ki->keySyms)
#define KEYCOL1(ki, k)    (KEYMAP(ki).map[((k)-(KEYMAP(ki).minKeyCode))*KEYMAP(ki).mapWidth])
#define KEYCOL1DDX(ki, k) (KEYMAPDDX(ki).map[((k)-(KEYMAPDDX(ki).minKeyCode))*KEYMAPDDX(ki).mapWidth])

#define KD_MAX_INPUT_FDS    8

typedef struct _kdInputFd {
    int	        fd;
    int         (*read) (int fd, void *closure);
    int	        (*enable) (int fd, void *closure);
    void        (*disable) (int fd, void *closure);
    void        *closure;
} KdInputFd;

static KdInputFd kdInputFds[KD_MAX_INPUT_FDS];
static int	 kdNumInputFds;

extern Bool      kdRawPointerCoordinates;

static void
KdRemoveDevice(void *data)
{
    KdInputDevice *device = data;
    KdKeyboardInfo *ki;
    KdPointerInfo *pi;

    if (device->type == KD_KEYBOARD) {
        ki = data;
        RemoveDevice(ki->common.dixdev);
    }
    else {
        pi = data;
        RemoveDevice(pi->common.dixdev);
    }
}

static void
KdSigio (int sig)
{
    int	i;
    int ret;
    void *device;

    for (i = 0; i < kdNumInputFds; i++) {
        device = kdInputFds[i].closure;
        ret = kdInputFds[i].read(kdInputFds[i].fd, device);
        if (ret < 0)
            KdRemoveDevice(device);
    }
}

static void
KdBlockSigio (void)
{
    sigset_t	set;
    
    sigemptyset (&set);
    sigaddset (&set, SIGIO);
    sigprocmask (SIG_BLOCK, &set, 0);
}

static void
KdUnblockSigio (void)
{
    sigset_t	set;
    
    sigemptyset (&set);
    sigaddset (&set, SIGIO);
    sigprocmask (SIG_UNBLOCK, &set, 0);
}

#ifdef DEBUG_SIGIO

void
KdAssertSigioBlocked (char *where)
{
    sigset_t	set, old;

    sigemptyset (&set);
    sigprocmask (SIG_BLOCK, &set, &old);
    if (!sigismember (&old, SIGIO)) {
	ErrorF ("SIGIO not blocked at %s\n", where);
        KdBacktrace(0);
    }
}

#else

#define KdAssertSigioBlocked(s)

#endif

static int  kdnFds;

#ifdef FNONBLOCK
#define NOBLOCK FNONBLOCK
#else
#define NOBLOCK FNDELAY
#endif

void
KdResetInputMachine (void)
{
    KdPointerInfo *pi;

    for (pi = kdPointers; pi; pi = pi->next) {
        pi->mouseState = start;
        pi->eventHeld = FALSE;
    }
}

static void
KdNonBlockFd (int fd)
{
    int	flags;
    flags = fcntl (fd, F_GETFL);
    flags |= FASYNC|NOBLOCK;
    fcntl (fd, F_SETFL, flags);
}

static void
KdAddFd (int fd)
{
    struct sigaction	act;
    sigset_t		set;
    
    kdnFds++;
    fcntl (fd, F_SETOWN, getpid());
    KdNonBlockFd (fd);
    AddEnabledDevice (fd);
    memset (&act, '\0', sizeof act);
    act.sa_handler = KdSigio;
    sigemptyset (&act.sa_mask);
    sigaddset (&act.sa_mask, SIGIO);
    sigaddset (&act.sa_mask, SIGALRM);
    sigaddset (&act.sa_mask, SIGVTALRM);
    sigaction (SIGIO, &act, 0);
    sigemptyset (&set);
    sigprocmask (SIG_SETMASK, &set, 0);
}

static void
KdRemoveFd (int fd)
{
    struct sigaction	act;
    int			flags;
    
    kdnFds--;
    RemoveEnabledDevice (fd);
    flags = fcntl (fd, F_GETFL);
    flags &= ~(FASYNC|NOBLOCK);
    fcntl (fd, F_SETFL, flags);
    if (kdnFds == 0)
    {
	memset (&act, '\0', sizeof act);
	act.sa_handler = SIG_IGN;
	sigemptyset (&act.sa_mask);
	sigaction (SIGIO, &act, 0);
    }
}

Bool
KdRegisterFd (int fd, int (*read) (int fd, void *closure), void *closure)
{
    if (kdNumInputFds == KD_MAX_INPUT_FDS)
	return FALSE;
    kdInputFds[kdNumInputFds].fd = fd;
    kdInputFds[kdNumInputFds].read = read;
    kdInputFds[kdNumInputFds].enable = 0;
    kdInputFds[kdNumInputFds].disable = 0;
    kdInputFds[kdNumInputFds].closure = closure;
    kdNumInputFds++;
    if (kdInputEnabled)
	KdAddFd (fd);
    return TRUE;
}

void
KdUnregisterFd (void *closure, int fd, Bool do_close)
{
    int	i, j;

    for (i = 0; i < kdNumInputFds; i++) {
	if (kdInputFds[i].closure == closure &&
            (fd == -1 || kdInputFds[i].fd == fd)) {
	    if (kdInputEnabled)
		KdRemoveFd (kdInputFds[i].fd);
	    if (do_close)
		close (kdInputFds[i].fd);
	    kdNumInputFds--;
	    for (j = i; j < kdNumInputFds; j++)
		kdInputFds[j] = kdInputFds[j+1];
            break;
	}
    }
}

void
KdUnregisterFds (void *closure, Bool do_close)
{
    KdUnregisterFd(closure, -1, do_close);
}

void
KdDisableInput (void)
{
    KdKeyboardInfo *ki;
    KdPointerInfo *pi;
    int found = 0, i = 0;

    KdBlockSigio();

    for (ki = kdKeyboards; ki; ki = ki->next) {
        if (ki->driver && ki->driver->Disable)
            (*ki->driver->Disable) (ki);
    }

    for (pi = kdPointers; pi; pi = pi->next) {
        if (pi->driver && pi->driver->Disable)
            (*pi->driver->Disable) (pi);
    }

    if (kdNumInputFds) {
        ErrorF("[KdDisableInput] Buggy drivers: still %d input fds left!",
               kdNumInputFds);
        i = 0;
        while (i < kdNumInputFds) {
            found = 0;
            for (ki = kdKeyboards; ki; ki = ki->next) {
                if (ki == kdInputFds[i].closure) {
                    ErrorF("    fd %d belongs to keybd driver %s\n",
                           kdInputFds[i].fd,
                           ki->driver && ki->driver->name ?
                             ki->driver->name : "(unnamed!)");
                    found = 1;
                    break;
                }
            }

            if (found) {
                i++;
                continue;
            }

            for (pi = kdPointers; pi; pi = pi->next) {
                if (pi == kdInputFds[i].closure) {
                    ErrorF("    fd %d belongs to pointer driver %s\n",
                           kdInputFds[i].fd,
                           pi->driver && pi->driver->name ?
                             pi->driver->name : "(unnamed!)");
                    break;
                }
            }

            if (found) {
                i++;
                continue;
            }

            ErrorF("    fd %d not claimed by any active device!\n",
                   kdInputFds[i].fd);
            KdUnregisterFd(kdInputFds[i].closure, kdInputFds[i].fd, TRUE);
        }
    }

    kdInputEnabled = FALSE;
}

void
KdEnableInput (void)
{
    xEvent xE;
    KdKeyboardInfo *ki;
    KdPointerInfo *pi;
    
    kdInputEnabled = TRUE;

    for (ki = kdKeyboards; ki; ki = ki->next) {
        if (ki->driver && ki->driver->Enable)
            (*ki->driver->Enable) (ki);
    }

    for (pi = kdPointers; pi; pi = pi->next) {
        if (pi->driver && pi->driver->Enable)
            (*pi->driver->Enable) (pi);
    }

    /* reset screen saver */
    xE.u.keyButtonPointer.time = GetTimeInMillis ();
    NoticeEventTime (&xE);

    KdUnblockSigio ();
}

static KdKeyboardDriver *
KdFindKeyboardDriver (char *name)
{
    KdKeyboardDriver *ret;

    /* ask a stupid question ... */
    if (!name)
        return NULL;
    
    for (ret = kdKeyboardDrivers; ret; ret = ret->next) {
        if (strcmp(ret->name, name) == 0)
            return ret;
    }

    return NULL;
}

static KdPointerDriver *
KdFindPointerDriver (char *name)
{
    KdPointerDriver *ret;

    /* ask a stupid question ... */
    if (!name)
        return NULL;

    for (ret = kdPointerDrivers; ret; ret = ret->next) {
        if (strcmp(ret->name, name) == 0)
            return ret;
    }

    return NULL;
}

static int
KdPointerProc(DeviceIntPtr pDevice, int onoff)
{
    DevicePtr       pDev = (DevicePtr)pDevice;
    KdPointerInfo   *pi;
    Atom            xiclass;

    if (!pDev)
	return BadImplementation;

    for (pi = kdPointers; pi; pi = pi->next) {
        if (pi->common.dixdev && pi->common.dixdev->id == pDevice->id)
            break;
    }

    if (!pi || !pi->common.dixdev || pi->common.dixdev->id != pDevice->id) {
        ErrorF("[KdPointerProc] Failed to find pointer for device %d!\n",
               pDevice->id);
        return BadImplementation;
    }

    switch (onoff)
    {
    case DEVICE_INIT:
        DebugF("initialising pointer %s ...\n", pi->common.name);
        if (!pi->driver) {
            if (!pi->driverPrivate) {
                ErrorF("no driver specified for %s\n", pi->common.name);
                return BadImplementation;
            }

            pi->driver = KdFindPointerDriver(pi->driverPrivate);
            if (!pi->driver) {
                ErrorF("Couldn't find pointer driver %s\n",
                       pi->driverPrivate ? (char *) pi->driverPrivate :
                       "(unnamed)");
                return !Success;
            }
            xfree(pi->driverPrivate);
            pi->driverPrivate = NULL;
        }

        if (!pi->driver->Init) {
            ErrorF("no init function\n");
            return BadImplementation;
        }

        if ((*pi->driver->Init) (pi) != Success) {
            return !Success;
        }

	InitPointerDeviceStruct(pDev, pi->map, pi->nButtons,
	    GetMotionHistory,
	    (PtrCtrlProcPtr)NoopDDA,
	    GetMotionHistorySize(), pi->nAxes);

        if (pi->common.type == KD_TOUCHSCREEN) {
            InitAbsoluteClassDeviceStruct(pDevice);
            xiclass = AtomFromName(XI_TOUCHSCREEN);
        }
        else {
            xiclass = AtomFromName(XI_MOUSE);
        }

        AssignTypeAndName(pi->common.dixdev, xiclass,
                          pi->common.name ? pi->common.name :
                                            "Generic KDrive Pointer");

	return Success;
	
    case DEVICE_ON:
        if (pDev->on == TRUE)
            return Success;
        
        if (!pi->driver->Enable) {
            ErrorF("no enable function\n");
            return BadImplementation;
        }

        if ((*pi->driver->Enable) (pi) == Success) {
            pDev->on = TRUE;
            return Success;
        }
        else {
            return BadImplementation;
        }

	return Success;

    case DEVICE_OFF:
        if (pDev->on == FALSE) {
            return Success;
        }

        if (!pi->driver->Disable) {
            return BadImplementation;
        }
        else {
            (*pi->driver->Disable) (pi);
            pDev->on = FALSE;
            return Success;
        }

        return Success;

    case DEVICE_CLOSE:
	if (pDev->on) {
            if (!pi->driver->Disable) {
                return BadImplementation;
            }
            (*pi->driver->Disable) (pi);
            pDev->on = FALSE;
        }

        if (!pi->driver->Fini)
            return BadImplementation;

        (*pi->driver->Fini) (pi);

        KdRemovePointer(pi);
        
        return Success;
    }

    /* NOTREACHED */
    return BadImplementation;
}

Bool
LegalModifier(unsigned int key, DeviceIntPtr pDev)
{
    return TRUE;
}

static void
KdBell (int volume, DeviceIntPtr pDev, pointer arg, int something)
{
    KeybdCtrl *ctrl = arg;
    KdKeyboardInfo *ki = NULL;
    
    for (ki = kdKeyboards; ki; ki = ki->next) {
        if (ki->common.dixdev && ki->common.dixdev->id == pDev->id)
            break;
    }

    if (!ki || !ki->common.dixdev || ki->common.dixdev->id != pDev->id)
        return;
    
    KdRingBell(ki, volume, ctrl->bell_pitch, ctrl->bell_duration);
}

void
DDXRingBell(int volume, int pitch, int duration)
{
    KdKeyboardInfo *ki = NULL;

    if (kdOsFuncs->Bell) {
        (*kdOsFuncs->Bell)(volume, pitch, duration);
    }
    else {
        for (ki = kdKeyboards; ki; ki = ki->next) {
            if (ki->common.dixdev->coreEvents)
                KdRingBell(ki, volume, pitch, duration);
        }
    }
}

void
KdRingBell(KdKeyboardInfo *ki, int volume, int pitch, int duration)
{
    if (!ki || !ki->driver || !ki->driver->Bell)
        return;
        
    if (kdInputEnabled)
        (*ki->driver->Bell) (ki, volume, pitch, duration);
}


static void
KdSetLeds (KdKeyboardInfo *ki, int leds)
{
    if (!ki || !ki->driver)
        return;

    if (kdInputEnabled) {
        if (ki->driver->Leds)
            (*ki->driver->Leds) (ki, leds);
    }
}

void
KdSetLed (KdKeyboardInfo *ki, int led, Bool on)
{
    if (!ki || !ki->common.dixdev || !ki->common.dixdev->kbdfeed)
        return;

    NoteLedState (ki->common.dixdev, led, on);
    KdSetLeds (ki, ki->common.dixdev->kbdfeed->ctrl.leds);
}

void
KdSetPointerMatrix (KdPointerMatrix *matrix)
{
    kdPointerMatrix = *matrix;
}

void
KdComputePointerMatrix (KdPointerMatrix *m, Rotation randr, int width,
                        int height)
{
    int		    x_dir = 1, y_dir = 1;
    int		    i, j;
    int		    size[2];

    size[0] = width; size[1] = height;
    if (randr & RR_Reflect_X)
	x_dir = -1;
    if (randr & RR_Reflect_Y)
	y_dir = -1;
    switch (randr & (RR_Rotate_All)) {
    case RR_Rotate_0:
	m->matrix[0][0] = x_dir; m->matrix[0][1] = 0;
	m->matrix[1][0] = 0; m->matrix[1][1] = y_dir;
	break;
    case RR_Rotate_90:
	m->matrix[0][0] = 0; m->matrix[0][1] = -x_dir;
	m->matrix[1][0] = y_dir; m->matrix[1][1] = 0;
	break;
    case RR_Rotate_180:
	m->matrix[0][0] = -x_dir; m->matrix[0][1] = 0;
	m->matrix[1][0] = 0; m->matrix[1][1] = -y_dir;
	break;
    case RR_Rotate_270:
	m->matrix[0][0] = 0; m->matrix[0][1] = x_dir;
	m->matrix[1][0] = -y_dir; m->matrix[1][1] = 0;
	break;
    }
    for (i = 0; i < 2; i++)
    {
	m->matrix[i][2] = 0;
	for (j = 0 ; j < 2; j++)
	    if (m->matrix[i][j] < 0)
		m->matrix[i][2] = size[j] - 1;
    }
}

static void
KdKbdCtrl (DeviceIntPtr pDevice, KeybdCtrl *ctrl)
{
    KdKeyboardInfo *ki;

    for (ki = kdKeyboards; ki; ki = ki->next) {
        if (ki->common.dixdev && ki->common.dixdev->id == pDevice->id)
            break;
    }

    if (!ki || !ki->common.dixdev || ki->common.dixdev->id != pDevice->id)
        return;

    KdSetLeds(ki, ctrl->leds);
    ki->bellPitch = ctrl->bell_pitch;
    ki->bellDuration = ctrl->bell_duration; 
}

extern KeybdCtrl defaultKeyboardControl;

static void
KdInitAutoRepeats (KdKeyboardInfo *ki)
{
    int		    key_code;
    unsigned char   mask;
    int		    i;
    unsigned char   *repeats;

    repeats = defaultKeyboardControl.autoRepeats;
    memset (repeats, '\0', 32);
    for (key_code = KD_MIN_KEYCODE; key_code <= KD_MAX_KEYCODE; key_code++)
    {
	if (!ki->modmap[key_code])
	{
	    i = key_code >> 3;
	    mask = 1 << (key_code & 7);
	    repeats[i] |= mask;
	}
    }
}

const KdKeySymModsRec kdKeySymMods[] = {
  {  XK_Control_L,	ControlMask },
  {  XK_Control_R, ControlMask },
  {  XK_Shift_L,	ShiftMask },
  {  XK_Shift_R,	ShiftMask },
  {  XK_Caps_Lock,	LockMask },
  {  XK_Shift_Lock, LockMask },
  {  XK_Alt_L,	Mod1Mask },
  {  XK_Alt_R,	Mod1Mask },
  {  XK_Meta_L,	Mod1Mask },
  {  XK_Meta_R,	Mod1Mask },
  {  XK_Num_Lock,	Mod2Mask },
  {  XK_Super_L,	Mod3Mask },
  {  XK_Super_R,	Mod3Mask },
  {  XK_Hyper_L,	Mod3Mask },
  {  XK_Hyper_R,	Mod3Mask },
  {  XK_Mode_switch, Mod4Mask },
  /* PDA specific hacks */
#ifdef XF86XK_Start
  {  XF86XK_Start, ControlMask },
#endif
  {  XK_Menu, ShiftMask },
  {  XK_telephone, Mod1Mask },
#ifdef XF86XK_AudioRecord
  {  XF86XK_AudioRecord, Mod2Mask },
#endif
#ifdef XF86XK_Calendar
  {  XF86XK_Calendar, Mod3Mask }
#endif
};

#define NUM_SYM_MODS (sizeof(kdKeySymMods) / sizeof(kdKeySymMods[0]))

static void
KdInitModMap (KdKeyboardInfo *ki)
{
    int	    key_code;
    int	    row;
    int	    width;
    KeySym  *syms;
    int	    i;

    width = ki->keySyms.mapWidth;
    for (key_code = ki->keySyms.minKeyCode; key_code <= ki->keySyms.maxKeyCode; key_code++)
    {
	ki->modmap[key_code] = 0;
	syms = ki->keySyms.map + (key_code - ki->keySyms.minKeyCode) * width;
	for (row = 0; row < width; row++, syms++)
	{
	    for (i = 0; i < NUM_SYM_MODS; i++) 
	    {
		if (*syms == kdKeySymMods[i].modsym) 
		    ki->modmap[key_code] |= kdKeySymMods[i].modbit;
	    }
	}
    }
}

static int
KdKeyboardProc(DeviceIntPtr pDevice, int onoff)
{
    Bool        ret;
    DevicePtr   pDev = (DevicePtr)pDevice;
    KdKeyboardInfo *ki;
    Atom xiclass;

    if (!pDev)
	return BadImplementation;

    for (ki = kdKeyboards; ki; ki = ki->next) {
        if (ki->common.dixdev && ki->common.dixdev->id == pDevice->id)
            break;
    }

    if (!ki || !ki->common.dixdev || ki->common.dixdev->id != pDevice->id) {
        return BadImplementation;
    }

    switch (onoff)
    {
    case DEVICE_INIT:
        DebugF("initialising keyboard %s\n", ki->common.name);

        if (!ki->driver) {
            if (!ki->driverPrivate) {
                ErrorF("no driver specified!\n");
                return BadImplementation;
            }

            ki->driver = KdFindKeyboardDriver(ki->driverPrivate);
            if (!ki->driver) {
                ErrorF("Couldn't find keyboard driver %s\n",
                       ki->driverPrivate ? (char *) ki->driverPrivate :
                       "(unnamed)");
                return !Success;
            }
            xfree(ki->driverPrivate);
            ki->driverPrivate = NULL;
        }

        if (!ki->driver->Init) {
            ErrorF("Keyboard %s: no init function\n", ki->common.name);
            return BadImplementation;
        }

        if ((*ki->driver->Init) (ki) != Success) {
            return !Success;
        }

        KdInitModMap(ki);
        KdInitAutoRepeats(ki);

#ifndef XKB
        if (!noXkbExtension) {
            memset(&names, 0, sizeof(XkbComponentNamesRec));
            if (XkbInitialMap) 
                names.keymap = XkbInitialMap;

            XkbSetRulesDflts ("base", "pc105", "us", NULL, NULL);
            ret = XkbInitKeyboardDeviceStruct (pDevice,
                                               &names,
                                               &ki->keySyms,
                                               ki->modmap,
                                               KdBell, KdKbdCtrl);
        }
        else
#endif
	ret = InitKeyboardDeviceStruct(pDev,
				       &ki->keySyms,
				       ki->modmap,
				       KdBell, KdKbdCtrl);
	if (!ret) {
            ErrorF("Couldn't initialise keyboard %s\n", ki->common.name);
	    return BadImplementation;
        }

        xiclass = AtomFromName(XI_KEYBOARD);
        AssignTypeAndName(pDevice, xiclass,
                          ki->common.name ? ki->common.name :
                                            "Generic KDrive Keyboard");

        KdResetInputMachine();

        return Success;

    case DEVICE_ON:
        if (pDev->on == TRUE)
            return Success;

        if (!ki->driver->Enable)
            return BadImplementation;

        if ((*ki->driver->Enable) (ki) != Success) {
            return BadMatch;
        }

        pDev->on = TRUE;
        return Success;

    case DEVICE_OFF:
        if (pDev->on == FALSE)
            return Success;

        if (!ki->driver->Disable)
            return BadImplementation;

        (*ki->driver->Disable) (ki);
        pDev->on = FALSE;

        return Success;
        
        break;

    case DEVICE_CLOSE:
	if (pDev->on) {
            if (!ki->driver->Disable)
                return BadImplementation;

            (*ki->driver->Disable) (ki);
            pDev->on = FALSE;
	}

        if (!ki->driver->Fini)
            return BadImplementation;

        (*ki->driver->Fini) (ki);

        KdRemoveKeyboard(ki);

        return Success;
    }

    /* NOTREACHED */
    return BadImplementation;
}

void
KdAddPointerDriver (KdPointerDriver *driver)
{
    KdPointerDriver **prev;

    if (!driver)
        return;

    for (prev = &kdPointerDrivers; *prev; prev = &(*prev)->next) {
        if (*prev == driver)
            return;
    }
    *prev = driver;
}

void
KdRemovePointerDriver (KdPointerDriver *driver)
{
    KdPointerDriver *tmp;

    if (!driver)
        return;

    /* FIXME remove all pointers using this driver */
    for (tmp = kdPointerDrivers; tmp; tmp = tmp->next) {
        if (tmp->next == driver)
            tmp->next = driver->next;
    }
    if (tmp == driver)
        tmp = NULL;
}

void
KdAddKeyboardDriver (KdKeyboardDriver *driver)
{
    KdKeyboardDriver **prev;

    if (!driver)
        return;

    for (prev = &kdKeyboardDrivers; *prev; prev = &(*prev)->next) {
        if (*prev == driver)
            return;
    }
    *prev = driver;
}

void
KdRemoveKeyboardDriver (KdKeyboardDriver *driver)
{
    KdKeyboardDriver *tmp;

    if (!driver)
        return;

    /* FIXME remove all keyboards using this driver */
    for (tmp = kdKeyboardDrivers; tmp; tmp = tmp->next) {
        if (tmp->next == driver)
            tmp->next = driver->next;
    }
    if (tmp == driver)
        tmp = NULL;
}

KdKeyboardInfo *
KdNewKeyboard (void)
{
    KdKeyboardInfo *ki = xcalloc(1, sizeof(*ki));
    KdKeyboardInfo **prev;

    if (!ki)
        return NULL;

    ki->keySyms.map = (KeySym *)xcalloc(sizeof(KeySym),
                                        KD_MAX_LENGTH *
                                         kdDefaultKeySyms.mapWidth);
    if (!ki->keySyms.map) {
        xfree(ki);
        return NULL;
    }

    ki->common.dixdev = AddInputDevice(KdKeyboardProc, TRUE);
    if (!ki->common.dixdev) {
        ErrorF("Couldn't register keyboard device %s\n",
               ki->common.name ? ki->common.name : "(unnamed)");
        xfree(ki->keySyms.map);
        xfree(ki);
        return NULL;
    }

    memcpy(ki->keySyms.map, kdDefaultKeySyms.map,
           sizeof(KeySym) * (KD_MAX_LENGTH * kdDefaultKeySyms.mapWidth));
    ki->keySyms.minKeyCode = kdDefaultKeySyms.minKeyCode;
    ki->keySyms.maxKeyCode = kdDefaultKeySyms.maxKeyCode;
    ki->keySyms.mapWidth = kdDefaultKeySyms.mapWidth;
    ki->minScanCode = 0;
    ki->maxScanCode = 0;
    ki->leds = 0;
    ki->bellPitch = 1000;
    ki->bellDuration = 200;
    ki->common.path = NULL;
    ki->next = NULL;

    RegisterOtherDevice(ki->common.dixdev);

    for (prev = &kdKeyboards; *prev; prev = &(*prev)->next)
        ;
    *prev = ki;

    return ki;
}

int
KdAddConfigKeyboard (char *keyboard)
{
    struct KdConfigDevice **prev, *new;

    if (!keyboard)
        return Success;

    new = (struct KdConfigDevice *) xcalloc(sizeof(struct KdConfigDevice), 1);
    if (!new)
        return BadAlloc;

    new->line = xstrdup(keyboard);
    new->next = NULL;

    for (prev = &kdConfigKeyboards; *prev; prev = &(*prev)->next);
    *prev = new;

    return Success;
}

void
KdRemoveKeyboard (KdKeyboardInfo *ki)
{
    KdKeyboardInfo **prev;

    if (!ki)
        return;

    for (prev = &kdKeyboards; *prev; prev = &(*prev)->next) {
        if (*prev == ki) {
            *prev = ki->next;
            break;
        }
    }

    KdFreeKeyboard(ki);
}

int
KdAddConfigPointer (char *pointer)
{
    struct KdConfigDevice **prev, *new;

    if (!pointer)
        return Success;

    new = (struct KdConfigDevice *) xcalloc(sizeof(struct KdConfigDevice), 1);
    if (!new)
        return BadAlloc;

    new->line = xstrdup(pointer);
    new->next = NULL;

    for (prev = &kdConfigPointers; *prev; prev = &(*prev)->next);
    *prev = new;

    return Success;
}

KdPointerInfo *
KdNewPointer(void)
{
    KdPointerInfo *pi = xcalloc(1, sizeof(KdPointerInfo));
    KdPointerInfo **prev;
    int i;

    if (!pi)
        return NULL;

    pi->common.dixdev = AddInputDevice(KdPointerProc, TRUE);
    if (!pi->common.dixdev) {
        ErrorF("Couldn't add pointer device %s\n",
               pi->common.name ? pi->common.name : "(unnamed)");
        xfree(pi);
        return NULL;
    }

    pi->common.name = xstrdup("Generic Pointer");
    pi->common.path = NULL;
    pi->common.type = KD_MOUSE;
    pi->driver = NULL;
    pi->driverPrivate = NULL;
    pi->next = NULL;
    pi->nAxes = 3;
    pi->nButtons = KD_MAX_BUTTON;
    for (i = 1; i < KD_MAX_BUTTON; i++)
        pi->map[i] = i;

    pi->mouseState = start;
    pi->eventHeld = FALSE;

    RegisterOtherDevice(pi->common.dixdev);

    for (prev = &kdPointers; *prev; prev = &(*prev)->next)
        ;
    *prev = pi;

    return pi;
}

void
KdRemovePointer (KdPointerInfo *pi)
{
    KdPointerInfo **prev;

    if (!pi)
        return;

    for (prev = &kdPointers; *prev; prev = &(*prev)->next) {
        if (*prev == pi) {
            *prev = pi->next;
            break;
        }
    }

    KdFreePointer(pi);
}

KdKeyboardInfo *
KdParseKeyboard (char *arg)
{
    char            save[1024];
    char            delim;
    KdKeyboardInfo     *ki = NULL;

    ki = KdNewKeyboard();
    if (!ki)
        return NULL;

    ki->common.name = strdup("Unknown KDrive Keyboard");
    ki->common.path = NULL;
    ki->driver = NULL;
    ki->driverPrivate = NULL;
#ifdef XKB
    ki->xkb = NULL;
#endif
    ki->next = NULL;

    if (!arg)
    {
        ErrorF("keybd: no arg\n");
        KdFreeKeyboard (ki);
        return NULL;
    }

    if (strlen (arg) >= sizeof (save))
    {
        ErrorF("keybd: arg too long\n");
        KdFreeKeyboard (ki);
        return NULL;
    }

    arg = KdParseFindNext (arg, ",", save, &delim);
    if (!save[0])
    {
        ErrorF("keybd: failed on save[0]\n");
        KdFreeKeyboard (ki);
        return NULL;
    }

    if (strcmp (save, "auto") == 0)
        ki->driverPrivate = NULL;
    else
        ki->driverPrivate = xstrdup(save);

    /* FIXME actually implement options */

    return ki;
}

KdPointerInfo *
KdParsePointer (char *arg)
{
    char            save[1024];
    char            delim;
    KdPointerInfo   *pi = NULL;
    struct xserver_option *options = NULL, *newopt = NULL, **tmpo = NULL;
    int             i = 0;

    pi = KdNewPointer();
    if (!pi)
        return NULL;
    pi->emulateMiddleButton = kdEmulateMiddleButton;
    pi->transformCoordinates = !kdRawPointerCoordinates;
    pi->nButtons = 3;
    pi->common.type = KD_MOUSE;

    if (!arg)
    {
        ErrorF("mouse: no arg\n");
        KdFreePointer (pi);
        return NULL;
    }

    if (strlen (arg) >= sizeof (save))
    {
        ErrorF("mouse: arg too long\n");
        KdFreePointer (pi);
        return NULL;
    }
    arg = KdParseFindNext (arg, ",", save, &delim);
    if (!save[0])
    {
        ErrorF("failed on save[0]\n");
        KdFreePointer (pi);
        return NULL;
    }

    if (strcmp(save, "auto") == 0)
        pi->driverPrivate = NULL;
    else
        pi->driverPrivate = xstrdup(save);

    if (delim != ',')
    {
        return pi;
    }

    arg = KdParseFindNext (arg, ",", save, &delim);

    while (delim == ',')
    {
        arg = KdParseFindNext (arg, ",", save, &delim);
        if (save[0] == '{')
        {
            char *s = save + 1;
             i = 0;
             while (*s && *s != '}')
             {
                if ('1' <= *s && *s <= '0' + pi->nButtons)
                    pi->map[i] = *s - '0';
                else
                    UseMsg ();
                s++;
             }
        }
        else if (!strcmp (save, "emulatemiddle"))
            pi->emulateMiddleButton = TRUE;
        else if (!strcmp (save, "noemulatemiddle"))
            pi->emulateMiddleButton = FALSE;
        else if (!strcmp (save, "transformcoord"))
            pi->transformCoordinates = TRUE;
        else if (!strcmp (save, "rawcoord"))
            pi->transformCoordinates = FALSE;
        else
        {
            newopt = xcalloc(sizeof(*newopt), 1);
            if (!newopt)
            {
                KdFreePointer(pi);
                return NULL;
            }

            for (tmpo = &options; *tmpo; tmpo = &(*tmpo)->next)
            *tmpo = newopt;

            if (strchr(arg, '='))
            {
                i = (strchr(arg, '=') - arg);
                newopt->key = (char *)xalloc(i+1);
                strncpy(newopt->key, arg, i+1);
                newopt->value = xstrdup(strchr(arg, '=') + 1);
            }
            else
            {
                newopt->key = xstrdup(save);
                newopt->value = NULL;
            }
            newopt->next = NULL;
        }
    }

    if (options)
        merge_option_lists(&pi->common.dixdev->options, options);

    return pi;
}


void
KdInitInput (void)
{
    KdPointerInfo *pi;
    KdKeyboardInfo *ki;
    struct KdConfigDevice *dev;

    kdInputEnabled = TRUE;

    for (dev = kdConfigPointers; dev; dev = dev->next) {
        pi = KdParsePointer(dev->line);
        if (!pi)
            ErrorF("Failed to parse pointer\n");
    }
    for (dev = kdConfigKeyboards; dev; dev = dev->next) {
        ki = KdParseKeyboard(dev->line);
        if (!ki)
            ErrorF("Failed to parse keyboard\n");
    }

    if (!kdEvents)
        kdEvents = (xEvent *)xcalloc(sizeof(xEvent), GetMaximumEventsNum());
    if (!kdEvents)
        FatalError("Couldn't allocate event buffer\n");

    mieqInit();
}

/*
 * Middle button emulation state machine
 *
 *  Possible transitions:
 *	Button 1 press	    v1
 *	Button 1 release    ^1
 *	Button 2 press	    v2
 *	Button 2 release    ^2
 *	Button 3 press	    v3
 *	Button 3 release    ^3
 *	Button other press  vo
 *	Button other release ^o
 *	Mouse motion	    <>
 *	Keyboard event	    k
 *	timeout		    ...
 *	outside box	    <->
 *
 *  States:
 *	start
 *	button_1_pend
 *	button_1_down
 *	button_2_down
 *	button_3_pend
 *	button_3_down
 *	synthetic_2_down_13
 *	synthetic_2_down_3
 *	synthetic_2_down_1
 *
 *  Transition diagram
 *
 *  start
 *	v1  -> (hold) (settimeout) button_1_pend
 *	^1  -> (deliver) start
 *	v2  -> (deliver) button_2_down
 *	^2  -> (deliever) start
 *	v3  -> (hold) (settimeout) button_3_pend
 *	^3  -> (deliver) start
 *	vo  -> (deliver) start
 *	^o  -> (deliver) start
 *	<>  -> (deliver) start
 *	k   -> (deliver) start
 *
 *  button_1_pend	(button 1 is down, timeout pending)
 *	^1  -> (release) (deliver) start
 *	v2  -> (release) (deliver) button_1_down
 *	^2  -> (release) (deliver) button_1_down
 *	v3  -> (cleartimeout) (generate v2) synthetic_2_down_13
 *	^3  -> (release) (deliver) button_1_down
 *	vo  -> (release) (deliver) button_1_down
 *	^o  -> (release) (deliver) button_1_down
 *	<-> -> (release) (deliver) button_1_down
 *	<>  -> (deliver) button_1_pend
 *	k   -> (release) (deliver) button_1_down
 *	... -> (release) button_1_down
 *
 *  button_1_down	(button 1 is down)
 *	^1  -> (deliver) start
 *	v2  -> (deliver) button_1_down
 *	^2  -> (deliver) button_1_down
 *	v3  -> (deliver) button_1_down
 *	^3  -> (deliver) button_1_down
 *	vo  -> (deliver) button_1_down
 *	^o  -> (deliver) button_1_down
 *	<>  -> (deliver) button_1_down
 *	k   -> (deliver) button_1_down
 *
 *  button_2_down	(button 2 is down)
 *	v1  -> (deliver) button_2_down
 *	^1  -> (deliver) button_2_down
 *	^2  -> (deliver) start
 *	v3  -> (deliver) button_2_down
 *	^3  -> (deliver) button_2_down
 *	vo  -> (deliver) button_2_down
 *	^o  -> (deliver) button_2_down
 *	<>  -> (deliver) button_2_down
 *	k   -> (deliver) button_2_down
 *
 *  button_3_pend	(button 3 is down, timeout pending)
 *	v1  -> (generate v2) synthetic_2_down
 *	^1  -> (release) (deliver) button_3_down
 *	v2  -> (release) (deliver) button_3_down
 *	^2  -> (release) (deliver) button_3_down
 *	^3  -> (release) (deliver) start
 *	vo  -> (release) (deliver) button_3_down
 *	^o  -> (release) (deliver) button_3_down
 *	<-> -> (release) (deliver) button_3_down
 *	<>  -> (deliver) button_3_pend
 *	k   -> (release) (deliver) button_3_down
 *	... -> (release) button_3_down
 *
 *  button_3_down	(button 3 is down)
 *	v1  -> (deliver) button_3_down
 *	^1  -> (deliver) button_3_down
 *	v2  -> (deliver) button_3_down
 *	^2  -> (deliver) button_3_down
 *	^3  -> (deliver) start
 *	vo  -> (deliver) button_3_down
 *	^o  -> (deliver) button_3_down
 *	<>  -> (deliver) button_3_down
 *	k   -> (deliver) button_3_down
 *
 *  synthetic_2_down_13	(button 1 and 3 are down)
 *	^1  -> (generate ^2) synthetic_2_down_3
 *	v2  -> synthetic_2_down_13
 *	^2  -> synthetic_2_down_13
 *	^3  -> (generate ^2) synthetic_2_down_1
 *	vo  -> (deliver) synthetic_2_down_13
 *	^o  -> (deliver) synthetic_2_down_13
 *	<>  -> (deliver) synthetic_2_down_13
 *	k   -> (deliver) synthetic_2_down_13
 *
 *  synthetic_2_down_3 (button 3 is down)
 *	v1  -> (deliver) synthetic_2_down_3
 *	^1  -> (deliver) synthetic_2_down_3
 *	v2  -> synthetic_2_down_3
 *	^2  -> synthetic_2_down_3
 *	^3  -> start
 *	vo  -> (deliver) synthetic_2_down_3
 *	^o  -> (deliver) synthetic_2_down_3
 *	<>  -> (deliver) synthetic_2_down_3
 *	k   -> (deliver) synthetic_2_down_3
 *
 *  synthetic_2_down_1 (button 1 is down)
 *	^1  -> start
 *	v2  -> synthetic_2_down_1
 *	^2  -> synthetic_2_down_1
 *	v3  -> (deliver) synthetic_2_down_1
 *	^3  -> (deliver) synthetic_2_down_1
 *	vo  -> (deliver) synthetic_2_down_1
 *	^o  -> (deliver) synthetic_2_down_1
 *	<>  -> (deliver) synthetic_2_down_1
 *	k   -> (deliver) synthetic_2_down_1
 */
 
typedef enum _inputClass {
    down_1, up_1,
    down_2, up_2,
    down_3, up_3,
    down_o, up_o,
    motion, outside_box,
    keyboard, timeout,
    num_input_class
} KdInputClass;

typedef enum _inputAction {
    noop,
    hold,
    setto,
    deliver,
    release,
    clearto,
    gen_down_2,
    gen_up_2
} KdInputAction;

#define MAX_ACTIONS 2

typedef struct _inputTransition {
    KdInputAction  actions[MAX_ACTIONS];
    KdPointerState nextState;
} KdInputTransition;

static const
KdInputTransition  kdInputMachine[num_input_states][num_input_class] = {
    /* start */
    {
	{ { hold, setto },	    button_1_pend },	/* v1 */
	{ { deliver, noop },	    start },		/* ^1 */
	{ { deliver, noop },	    button_2_down },	/* v2 */
	{ { deliver, noop },	    start },		/* ^2 */
	{ { hold, setto },	    button_3_pend },	/* v3 */
	{ { deliver, noop },	    start },		/* ^3 */
	{ { deliver, noop },	    start },		/* vo */
	{ { deliver, noop },	    start },		/* ^o */
	{ { deliver, noop },	    start },		/* <> */
	{ { deliver, noop },	    start },		/* <-> */
	{ { noop, noop },	    start },		/* k */
	{ { noop, noop },	    start },		/* ... */
    },
    /* button_1_pend */
    {
	{ { noop, noop },	    button_1_pend },	/* v1 */
	{ { release, deliver },	    start },		/* ^1 */
	{ { release, deliver },	    button_1_down },	/* v2 */
	{ { release, deliver },	    button_1_down },	/* ^2 */
	{ { clearto, gen_down_2 },  synth_2_down_13 },	/* v3 */
	{ { release, deliver },	    button_1_down },	/* ^3 */
	{ { release, deliver },	    button_1_down },	/* vo */
	{ { release, deliver },	    button_1_down },	/* ^o */
	{ { deliver, noop },	    button_1_pend },	/* <> */
	{ { release, deliver },	    button_1_down },	/* <-> */
	{ { noop, noop },	    button_1_down },	/* k */
	{ { release, noop },	    button_1_down },	/* ... */
    },
    /* button_1_down */
    {
	{ { noop, noop },	    button_1_down },	/* v1 */
	{ { deliver, noop },	    start },		/* ^1 */
	{ { deliver, noop },	    button_1_down },	/* v2 */
	{ { deliver, noop },	    button_1_down },	/* ^2 */
	{ { deliver, noop },	    button_1_down },	/* v3 */
	{ { deliver, noop },	    button_1_down },	/* ^3 */
	{ { deliver, noop },	    button_1_down },	/* vo */
	{ { deliver, noop },	    button_1_down },	/* ^o */
	{ { deliver, noop },	    button_1_down },	/* <> */
	{ { deliver, noop },	    button_1_down },	/* <-> */
	{ { noop, noop },	    button_1_down },	/* k */
	{ { noop, noop },	    button_1_down },	/* ... */
    },
    /* button_2_down */
    {
	{ { deliver, noop },	    button_2_down },	/* v1 */
	{ { deliver, noop },	    button_2_down },	/* ^1 */
	{ { noop, noop },	    button_2_down },	/* v2 */
	{ { deliver, noop },	    start },		/* ^2 */
	{ { deliver, noop },	    button_2_down },	/* v3 */
	{ { deliver, noop },	    button_2_down },	/* ^3 */
	{ { deliver, noop },	    button_2_down },	/* vo */
	{ { deliver, noop },	    button_2_down },	/* ^o */
	{ { deliver, noop },	    button_2_down },	/* <> */
	{ { deliver, noop },	    button_2_down },	/* <-> */
	{ { noop, noop },	    button_2_down },	/* k */
	{ { noop, noop },	    button_2_down },	/* ... */
    },
    /* button_3_pend */
    {
	{ { clearto, gen_down_2 },  synth_2_down_13 },	/* v1 */
	{ { release, deliver },	    button_3_down },	/* ^1 */
	{ { release, deliver },	    button_3_down },	/* v2 */
	{ { release, deliver },	    button_3_down },	/* ^2 */
	{ { release, deliver },	    button_3_down },	/* v3 */
	{ { release, deliver },	    start },		/* ^3 */
	{ { release, deliver },	    button_3_down },	/* vo */
	{ { release, deliver },	    button_3_down },	/* ^o */
	{ { deliver, noop },	    button_3_pend },	/* <> */
	{ { release, deliver },	    button_3_down },	/* <-> */
	{ { release, noop },	    button_3_down },	/* k */
	{ { release, noop },	    button_3_down },	/* ... */
    },
    /* button_3_down */
    {
	{ { deliver, noop },	    button_3_down },	/* v1 */
	{ { deliver, noop },	    button_3_down },	/* ^1 */
	{ { deliver, noop },	    button_3_down },	/* v2 */
	{ { deliver, noop },	    button_3_down },	/* ^2 */
	{ { noop, noop },	    button_3_down },	/* v3 */
	{ { deliver, noop },	    start },		/* ^3 */
	{ { deliver, noop },	    button_3_down },	/* vo */
	{ { deliver, noop },	    button_3_down },	/* ^o */
	{ { deliver, noop },	    button_3_down },	/* <> */
	{ { deliver, noop },	    button_3_down },	/* <-> */
	{ { noop, noop },	    button_3_down },	/* k */
	{ { noop, noop },	    button_3_down },	/* ... */
    },
    /* synthetic_2_down_13 */
    {
	{ { noop, noop },	    synth_2_down_13 },	/* v1 */
	{ { gen_up_2, noop },	    synth_2_down_3 },	/* ^1 */
	{ { noop, noop },	    synth_2_down_13 },	/* v2 */
	{ { noop, noop },	    synth_2_down_13 },	/* ^2 */
	{ { noop, noop },	    synth_2_down_13 },	/* v3 */
	{ { gen_up_2, noop },	    synth_2_down_1 },	/* ^3 */
	{ { deliver, noop },	    synth_2_down_13 },	/* vo */
	{ { deliver, noop },	    synth_2_down_13 },	/* ^o */
	{ { deliver, noop },	    synth_2_down_13 },	/* <> */
	{ { deliver, noop },	    synth_2_down_13 },	/* <-> */
	{ { noop, noop },	    synth_2_down_13 },	/* k */
	{ { noop, noop },	    synth_2_down_13 },	/* ... */
    },
    /* synthetic_2_down_3 */
    {
	{ { deliver, noop },	    synth_2_down_3 },	/* v1 */
	{ { deliver, noop },	    synth_2_down_3 },	/* ^1 */
	{ { deliver, noop },	    synth_2_down_3 },	/* v2 */
	{ { deliver, noop },	    synth_2_down_3 },	/* ^2 */
	{ { noop, noop },	    synth_2_down_3 },	/* v3 */
	{ { noop, noop },	    start },		/* ^3 */
	{ { deliver, noop },	    synth_2_down_3 },	/* vo */
	{ { deliver, noop },	    synth_2_down_3 },	/* ^o */
	{ { deliver, noop },	    synth_2_down_3 },	/* <> */
	{ { deliver, noop },	    synth_2_down_3 },	/* <-> */
	{ { noop, noop },	    synth_2_down_3 },	/* k */
	{ { noop, noop },	    synth_2_down_3 },	/* ... */
    },
    /* synthetic_2_down_1 */
    {
	{ { noop, noop },	    synth_2_down_1 },	/* v1 */
	{ { noop, noop },	    start },		/* ^1 */
	{ { deliver, noop },	    synth_2_down_1 },	/* v2 */
	{ { deliver, noop },	    synth_2_down_1 },	/* ^2 */
	{ { deliver, noop },	    synth_2_down_1 },	/* v3 */
	{ { deliver, noop },	    synth_2_down_1 },	/* ^3 */
	{ { deliver, noop },	    synth_2_down_1 },	/* vo */
	{ { deliver, noop },	    synth_2_down_1 },	/* ^o */
	{ { deliver, noop },	    synth_2_down_1 },	/* <> */
	{ { deliver, noop },	    synth_2_down_1 },	/* <-> */
	{ { noop, noop },	    synth_2_down_1 },	/* k */
	{ { noop, noop },	    synth_2_down_1 },	/* ... */
    },
};

#define EMULATION_WINDOW    10
#define EMULATION_TIMEOUT   100

static int
KdInsideEmulationWindow (KdPointerInfo *pi, int x, int y, int z)
{
    pi->emulationDx = pi->heldEvent.x - x;
    pi->emulationDy = pi->heldEvent.y - y;

    return (abs (pi->emulationDx) < EMULATION_WINDOW &&
	    abs (pi->emulationDy) < EMULATION_WINDOW);
}
				     
static KdInputClass
KdClassifyInput (KdPointerInfo *pi, int type, int x, int y, int z, int b)
{
    switch (type) {
    case ButtonPress:
	switch (b) {
	case 1: return down_1;
	case 2: return down_2;
	case 3: return down_3;
	default: return down_o;
	}
	break;
    case ButtonRelease:
	switch (b) {
	case 1: return up_1;
	case 2: return up_2;
	case 3: return up_3;
	default: return up_o;
	}
	break;
    case MotionNotify:
	if (pi->eventHeld && !KdInsideEmulationWindow(pi, x, y, z))
	    return outside_box;
	else
	    return motion;
    default:
	return keyboard;
    }
    return keyboard;
}

#ifndef NDEBUG
char	*kdStateNames[] = {
    "start",
    "button_1_pend",
    "button_1_down",
    "button_2_down",
    "button_3_pend",
    "button_3_down",
    "synth_2_down_13",
    "synth_2_down_3",
    "synthetic_2_down_1",
    "num_input_states"
};

char	*kdClassNames[] = {
    "down_1", "up_1",
    "down_2", "up_2",
    "down_3", "up_3",
    "motion", "ouside_box",
    "keyboard", "timeout",
    "num_input_class"
};

char *kdActionNames[] = {
    "noop",
    "hold",
    "setto",
    "deliver",
    "release",
    "clearto",
    "gen_down_2",
    "gen_up_2",
};
#endif

static void
KdQueueEvent (DeviceIntPtr pDev, xEvent *ev)
{
    KdAssertSigioBlocked ("KdQueueEvent");
    mieqEnqueue (pDev, ev);
}

/* We return true if we're stealing the event. */
static Bool
KdRunMouseMachine (KdPointerInfo *pi, KdInputClass c, int type, int x, int y,
                   int z, int b, int absrel)
{
    const KdInputTransition *t;
    int	a;

    c = KdClassifyInput(pi, type, x, y, z, b);
    t = &kdInputMachine[pi->mouseState][c];
    for (a = 0; a < MAX_ACTIONS; a++)
    {
	switch (t->actions[a]) {
	case noop:
	    break;
	case hold:
	    pi->eventHeld = TRUE;
	    pi->emulationDx = 0;
	    pi->emulationDy = 0;
	    pi->heldEvent.type = type;
            pi->heldEvent.x = x;
            pi->heldEvent.y = y;
            pi->heldEvent.z = z;
            pi->heldEvent.flags = b;
            pi->heldEvent.absrel = absrel;
            return TRUE;
	    break;
	case setto:
	    pi->emulationTimeout = GetTimeInMillis () + EMULATION_TIMEOUT;
	    pi->timeoutPending = TRUE;
	    break;
	case deliver:
            _KdEnqueuePointerEvent (pi, pi->heldEvent.type, pi->heldEvent.x,
                                    pi->heldEvent.y, pi->heldEvent.z,
                                    pi->heldEvent.flags, pi->heldEvent.absrel,
                                    TRUE);
	    break;
	case release:
	    pi->eventHeld = FALSE;
	    pi->timeoutPending = FALSE;
            _KdEnqueuePointerEvent (pi, pi->heldEvent.type, pi->heldEvent.x,
                                    pi->heldEvent.y, pi->heldEvent.z,
                                    pi->heldEvent.flags, pi->heldEvent.absrel,
                                    TRUE);
            return TRUE;
	    break;
	case clearto:
	    pi->timeoutPending = FALSE;
	    break;
	case gen_down_2:
            _KdEnqueuePointerEvent (pi, ButtonPress, x, y, z, 2, absrel,
                                    TRUE);
	    pi->eventHeld = FALSE;
            return TRUE;
	    break;
	case gen_up_2:
            _KdEnqueuePointerEvent (pi, ButtonRelease, x, y, z, 2, absrel,
                                    TRUE);
            return TRUE;
	    break;
	}
    }
    pi->mouseState = t->nextState;
    return FALSE;
}

static int
KdHandlePointerEvent (KdPointerInfo *pi, int type, int x, int y, int z, int b,
                      int absrel)
{
    if (pi->emulateMiddleButton)
        return KdRunMouseMachine (pi, KdClassifyInput(pi, type, x, y, z, b),
                                  type, x, y, z, b, absrel);
    return FALSE;
}

static void
KdReceiveTimeout (KdPointerInfo *pi)
{
    KdRunMouseMachine (pi, timeout, 0, 0, 0, 0, 0, 0);
}

#define KILL_SEQUENCE     ((1L << KK_CONTROL)|(1L << KK_ALT)|(1L << KK_F8)|(1L << KK_F10))
#define SPECIAL_SEQUENCE  ((1L << KK_CONTROL) | (1L << KK_ALT))
#define SETKILLKEY(b)     (KdSpecialKeys |= (1L << (b)))
#define CLEARKILLKEY(b)   (KdSpecialKeys &= ~(1L << (b)))

CARD32	KdSpecialKeys = 0;

/*
 * kdCheckTermination
 *
 * This function checks for the key sequence that terminates the server.  When
 * detected, it sets the dispatchException flag and returns.  The key sequence
 * is:
 *	Control-Alt
 * It's assumed that the server will be waken up by the caller when this
 * function returns.
 */

extern int nClients;

static void
KdCheckSpecialKeys(KdKeyboardInfo *ki, int type, int sym)
{
    if (!ki)
        return;

    /*
     * Ignore key releases
     */

    if (type == KeyRelease)
        return;

    /* Some iPaq keyboard -> mouse button mapping used to be here, but I
     * refuse to perpetuate this madness. -daniels */

    /*
     * Check for control/alt pressed
     */
    if ((ki->common.dixdev->key->state & (ControlMask|Mod1Mask)) !=
	(ControlMask|Mod1Mask))
	return;

    /*
     * Let OS function see keysym first
     */
    
    if (kdOsFuncs->SpecialKey)
	if ((*kdOsFuncs->SpecialKey) (sym))
	    return;

    /*
     * Now check for backspace or delete; these signal the
     * X server to terminate
     *
     * I can't believe it's not XKB. -daniels
     */
    switch (sym) {
    case XK_BackSpace:
    case XK_Delete:
    case XK_KP_Delete:
	/* 
	 * Set the dispatch exception flag so the server will terminate the
	 * next time through the dispatch loop.
	 */
	if (kdDontZap == FALSE)
	    dispatchException |= DE_TERMINATE;
	break;
    }
    
    if (sym >= XK_F1 && sym <= XK_F10) {
        if (kdOsFuncs->SwitchVT)
            (*kdOsFuncs->SwitchVT) (KD_VT_ABSOLUTE, sym - XK_F1 + 1);
    }
}

/*
 * kdEnqueueKeyboardEvent
 *
 * This function converts hardware keyboard event information into an X event
 * and enqueues it using MI.  It wakes up the server before returning so that
 * the event will be processed normally.
 *
 */

static void
KdHandleKeyboardEvent (KdKeyboardInfo *ki, int type, int key)
{
    int           byte;
    CARD8         bit;
    KdPointerInfo *pi;
    
    byte = key >> 3;
    bit = 1 << (key & 7);

    switch (type) {
    case KeyPress:
	ki->keyState[byte] |= bit;
	break;
    case KeyRelease:
	ki->keyState[byte] &= ~bit;
	break;
    }

    for (pi = kdPointers; pi; pi = pi->next)
	KdRunMouseMachine (pi, keyboard, 0, 0, 0, 0, 0, 0);
}

void
KdReleaseAllKeys (void)
{
    int	key, nEvents, i;
    KdKeyboardInfo *ki;

    KdBlockSigio ();

    for (ki = kdKeyboards; ki; ki = ki->next) {
        for (key = ki->keySyms.minKeyCode; key < ki->keySyms.maxKeyCode;
             key++) {
            if (IsKeyDown(ki, key)) {
                KdHandleKeyboardEvent(ki, KeyRelease, key);
                nEvents = GetKeyboardEvents(kdEvents, ki->common.dixdev,
                                            KeyRelease, key);
                for (i = 0; i < nEvents; i++)
                    KdQueueEvent (ki->common.dixdev, kdEvents + i);
            }
        }
    }

    KdUnblockSigio ();
}

static void
KdCheckLock (void)
{
    KeyClassPtr	    keyc = NULL;
    Bool	    isSet = FALSE, shouldBeSet = FALSE;
    KdKeyboardInfo     *tmp = NULL;

    for (tmp = kdKeyboards; tmp; tmp = tmp->next) {
        if (tmp->LockLed && tmp->common.dixdev && tmp->common.dixdev->key) {
            keyc = tmp->common.dixdev->key;
            isSet = (tmp->leds & (1 << (tmp->LockLed-1))) != 0;
            shouldBeSet = (keyc->state & LockMask) != 0;
            if (isSet != shouldBeSet)
                KdSetLed (tmp, tmp->LockLed, shouldBeSet);
        }
    }
}

void
KdEnqueueKeyboardEvent(KdKeyboardInfo   *ki,
                       unsigned char scan_code,
		       unsigned char is_up)
{
    unsigned char key_code;
    KeyClassPtr	keyc = NULL;
    KeybdCtrl *ctrl = NULL;
    int type, nEvents, i;

    if (!ki || !ki->common.dixdev || !ki->common.dixdev->kbdfeed ||
        !ki->common.dixdev->key)
	return;

    keyc = ki->common.dixdev->key;
    ctrl = &ki->common.dixdev->kbdfeed->ctrl;

    if (scan_code >= ki->minScanCode && scan_code <= ki->maxScanCode)
    {
	key_code = scan_code + KD_MIN_KEYCODE - ki->minScanCode;
	
	/*
	 * Set up this event -- the type may be modified below
	 */
	if (is_up)
	    type = KeyRelease;
	else
	    type = KeyPress;
	
#ifdef XKB
        if (noXkbExtension)
#endif
        {
            KdCheckSpecialKeys(ki, type, key_code);
            KdHandleKeyboardEvent(ki, type, key_code);
	}
	
        DebugF("KdEnqueueKeyboardEvent: posting %s on key %d\n", (type == KeyRelease) ? "release" : "press", key_code);
        nEvents = GetKeyboardEvents(kdEvents, ki->common.dixdev, type,
                                    key_code);
        for (i = 0; i < nEvents; i++)
            KdQueueEvent(ki->common.dixdev, kdEvents + i);
    }
    else {
        ErrorF("driver %s wanted to post scancode %d outside of [%d, %d]!\n",
               ki->common.name, scan_code, ki->minScanCode, ki->maxScanCode);
    }
}

/*
 * kdEnqueuePointerEvent
 *
 * This function converts hardware mouse event information into X event
 * information.  A mouse movement event is passed off to MI to generate
 * a MotionNotify event, if appropriate.  Button events are created and
 * passed off to MI for enqueueing.
 */

/* FIXME do something a little more clever to deal with multiple axes here */
void
KdEnqueuePointerEvent(KdPointerInfo *pi, unsigned long flags, int rx, int ry,
                      int rz)
{
    CARD32        ms;
    unsigned char buttons;
    int           x, y, z;
    int           (*matrix)[3] = kdPointerMatrix.matrix;
    unsigned long button;
    int           n;
    int           dixflags;

    if (!pi)
	return;
    
    ms = GetTimeInMillis();

    /* we don't need to transform z, so we don't. */
    if (pi->transformCoordinates) {
        x = matrix[0][0] * rx + matrix[0][1] * ry;
        y = matrix[1][0] * rx + matrix[1][1] * ry;
    }
    else {
        x = rx;
        y = ry;
    }
    z = rz;

    if (flags & KD_MOUSE_DELTA)
        dixflags = POINTER_RELATIVE | POINTER_ACCELERATE;
    else
        dixflags = POINTER_ABSOLUTE;

    _KdEnqueuePointerEvent(pi, MotionNotify, x, y, z, 0, dixflags, FALSE);

    buttons = flags;

    for (button = KD_BUTTON_1, n = 1; n <= pi->nButtons;
         button <<= 1, n++) {
        if (((pi->buttonState & button) ^ (buttons & button)) &&
           !(buttons & button)) {
            _KdEnqueuePointerEvent(pi, ButtonRelease, x, y, z, n,
                                   dixflags, FALSE);
	}
    }
    for (button = KD_BUTTON_1, n = 1; n <= pi->nButtons;
         button <<= 1, n++) {
	if (((pi->buttonState & button) ^ (buttons & button)) &&
	    (buttons & button)) {
            _KdEnqueuePointerEvent(pi, ButtonPress, x, y, z, n,
                                   dixflags, FALSE);
        }
    }

    pi->buttonState = buttons;
}

void
_KdEnqueuePointerEvent (KdPointerInfo *pi, int type, int x, int y, int z,
                        int b, int absrel, Bool force)
{
    int nEvents = 0, i = 0;
    int valuators[3] = { x, y, z };

    /* TRUE from KdHandlePointerEvent, means 'we swallowed the event'. */
    if (!force && KdHandlePointerEvent(pi, type, x, y, z, b, absrel))
        return;

    nEvents = GetPointerEvents(kdEvents, pi->common.dixdev, type, b, absrel,
                               0, 3, valuators);
    for (i = 0; i < nEvents; i++)
        KdQueueEvent(pi->common.dixdev, kdEvents + i);
}

void
KdBlockHandler (int		screen,
		pointer		blockData,
		pointer		timeout,
		pointer		readmask)
{
    KdPointerInfo		    *pi;
    int myTimeout=0;

    for (pi = kdPointers; pi; pi = pi->next)
    {
	if (pi->timeoutPending)
	{
	    int	ms;
    
	    ms = pi->emulationTimeout - GetTimeInMillis ();
	    if (ms < 1)
		ms = 1;
	    if(ms<myTimeout || myTimeout==0)
		    myTimeout=ms;
	}
    }
    /* if we need to poll for events, do that */
    if(kdOsFuncs->pollEvents)
    {
	    (*kdOsFuncs->pollEvents)();
	    myTimeout=20;
    }
    if(myTimeout>0)
    	AdjustWaitForDelay (timeout, myTimeout);
}

void
KdWakeupHandler (int		screen, 
		 pointer    	data,
		 unsigned long	lresult,
		 pointer	readmask)
{
    int		result = (int) lresult;
    fd_set	*pReadmask = (fd_set *) readmask;
    int		i;
    KdPointerInfo	*pi;
    
    if (kdInputEnabled && result > 0)
    {
	for (i = 0; i < kdNumInputFds; i++)
	    if (FD_ISSET (kdInputFds[i].fd, pReadmask))
	    {
		KdBlockSigio ();
		(*kdInputFds[i].read) (kdInputFds[i].fd, kdInputFds[i].closure);
		KdUnblockSigio ();
	    }
    }
    for (pi = kdPointers; pi; pi = pi->next)
    {
	if (pi->timeoutPending)
	{
	    if ((long) (GetTimeInMillis () - pi->emulationTimeout) >= 0)
	    {
		pi->timeoutPending = FALSE;
		KdBlockSigio ();
		KdReceiveTimeout (pi);
		KdUnblockSigio ();
	    }
	}
    }
    if (kdSwitchPending)
	KdProcessSwitch ();
}

#define KdScreenOrigin(pScreen) (&(KdGetScreenPriv(pScreen)->screen->origin))

static Bool
KdCursorOffScreen(ScreenPtr *ppScreen, int *x, int *y)
{
    ScreenPtr	pScreen  = *ppScreen;
    ScreenPtr	pNewScreen;
    int		n;
    int		dx, dy;
    int		best_x, best_y;
    int		n_best_x, n_best_y;
    CARD32	ms;
    
    if (kdDisableZaphod || screenInfo.numScreens <= 1)
	return FALSE;

    if (0 <= *x && *x < pScreen->width && 0 <= *y && *y < pScreen->height)
	return FALSE;
	
    ms = GetTimeInMillis ();
    if (kdOffScreen && (int) (ms - kdOffScreenTime) < 1000)
	return FALSE;
    kdOffScreen = TRUE;
    kdOffScreenTime = ms;
    n_best_x = -1;
    best_x = 32767;
    n_best_y = -1;
    best_y = 32767;
    for (n = 0; n < screenInfo.numScreens; n++)
    {
	pNewScreen = screenInfo.screens[n];
	if (pNewScreen == pScreen) 
	    continue;
	dx = KdScreenOrigin(pNewScreen)->x - KdScreenOrigin(pScreen)->x;
	dy = KdScreenOrigin(pNewScreen)->y - KdScreenOrigin(pScreen)->y;
	if (*x < 0)
	{
	    if (dx <= 0 && -dx < best_x)
	    {
		best_x = -dx;
		n_best_x = n;
	    }
	}
	else if (*x >= pScreen->width)
	{
	    if (dx >= 0 && dx < best_x)
	    {
		best_x = dx;
		n_best_x = n;
	    }
	}
	if (*y < 0)
	{
	    if (dy <= 0 && -dy < best_y)
	    {
		best_y = -dy;
		n_best_y = n;
	    }
	}
	else if (*y >= pScreen->height)
	{
	    if (dy >= 0 && dy < best_y)
	    {
		best_y = dy;
		n_best_y = n;
	    }
	}
    }
    if (best_y < best_x)
	n_best_x = n_best_y;
    if (n_best_x == -1)
	return FALSE;
    pNewScreen = screenInfo.screens[n_best_x];
    
    if (*x < 0)
	*x += pNewScreen->width;
    if (*y < 0)
	*y += pNewScreen->height;
    
    if (*x >= pScreen->width)
	*x -= pScreen->width;
    if (*y >= pScreen->height)
	*y -= pScreen->height;
    
    *ppScreen = pNewScreen;
    return TRUE;
}

static void
KdCrossScreen(ScreenPtr pScreen, Bool entering)
{
#ifndef XIPAQ
    if (entering)
	KdEnableScreen (pScreen);
    else
	KdDisableScreen (pScreen);
#endif
}

int KdCurScreen;	/* current event screen */

static void
KdWarpCursor (ScreenPtr pScreen, int x, int y)
{
    KdBlockSigio ();
    KdCurScreen = pScreen->myNum;
    miPointerWarpCursor (pScreen, x, y);
    KdUnblockSigio ();
}

miPointerScreenFuncRec kdPointerScreenFuncs = 
{
    KdCursorOffScreen,
    KdCrossScreen,
    KdWarpCursor
};

void
ProcessInputEvents ()
{
    mieqProcessInputEvents();
    miPointerUpdate();
    if (kdSwitchPending)
	KdProcessSwitch ();
    KdCheckLock ();
}

/* FIXME use XSECURITY to work out whether the client should be allowed to
 * open and close. */
void
OpenInputDevice(DeviceIntPtr pDev, ClientPtr client, int *status)
{
    if (!pDev)
        *status = BadDevice;
    else
        *status = Success;
}

void
CloseInputDevice(DeviceIntPtr pDev, ClientPtr client)
{
    return;
}

/* We initialise all input devices at startup. */
void
AddOtherInputDevices(void)
{
    return;
}

/* At the moment, absolute/relative is up to the client. */
int
SetDeviceMode(register ClientPtr client, DeviceIntPtr pDev, int mode)
{
    return BadMatch;
}

int
SetDeviceValuators(register ClientPtr client, DeviceIntPtr pDev,
                   int *valuators, int first_valuator, int num_valuators)
{
    return BadMatch;
}

int
ChangeDeviceControl(register ClientPtr client, DeviceIntPtr pDev,
                        xDeviceCtl *control)
{
    KdPointerInfo *pi = NULL;

    switch (control->control) {
    case DEVICE_RESOLUTION:
        /* FIXME do something more intelligent here */
        return BadMatch;

    case DEVICE_ABS_CALIB:
    case DEVICE_ABS_AREA:
        return Success;

    case DEVICE_CORE:
        return Success;

    case DEVICE_RAWEVENT:
        for (pi = kdPointers; pi; pi = pi->next) {
            if (pi->common.dixdev && pi->common.dixdev->id == pDev->id)
                break;
        }
        if (!pi || !pi->common.dixdev || pi->common.dixdev->id != pDev->id ||
            !pi->driver->Control)
            return BadDevice;

        return (*pi->driver->Control) (pi, DEVICE_RAWEVENT, control);

    default:
        return BadMatch;
    }

    /* NOTREACHED */
    return BadImplementation;
}

int
NewInputDeviceRequest(struct xserver_option *options, DeviceIntPtr *dev)
{
    struct xserver_option *option = NULL;
    KdKeyboardInfo *ki = NULL, *k;
    KdPointerInfo *pi = NULL, *p;

    for (option = options; option; option = option->next) {
        if (strcmp(option->key, "type") == 0) {
            if (strcmp(option->value, "pointer") == 0) {
                /* FIXME Maemo! */
                return BadValue;

                pi = KdNewPointer();
                if (!pi)
                    return BadAlloc;
                merge_option_lists(&pi->common.dixdev->options, options);
            }
            else if (strcmp(option->value, "keyboard") == 0) {
                ki = KdNewKeyboard();
                if (!ki)
                    return BadAlloc;
                merge_option_lists(&ki->common.dixdev->options, options);
            }
            else {
                ErrorF("unrecognised device type!\n");
                return BadValue;
            }
        }
    }

    if (!ki && !pi) {
        ErrorF("unrecognised device identifier!\n");
        return BadValue;
    }

    for (option = options; option; option = option->next) {
        if (strcmp(option->key, "device") == 0 ||
            strcmp(option->key, "path") == 0) {
            if (pi && option->value) {
                pi->common.path = xstrdup(option->value);
                for (p = kdPointers; p; p = p->next) {
                    /* Already there. */
                    if (p != pi &&
                        strcmp(p->common.path, pi->common.path) == 0) {
                        KdRemovePointer(pi);
                        return BadValue;
                    }
                }
            }
            else if (ki && option->value) {
                ki->common.path = xstrdup(option->value);
                for (k = kdKeyboards; k; k = k->next) {
                    /* Already there. */
                    if (k != ki &&
                        strcmp(k->common.path, ki->common.path) == 0) {
                        KdRemoveKeyboard(ki);
                        return BadValue;
                    }
                }
            }
        }
        else if (strcmp(option->key, "driver") == 0) {
            if (pi) {
                pi->driver = KdFindPointerDriver(option->value);
                if (!pi->driver) {
                    ErrorF("couldn't find driver!\n");
                    KdRemovePointer(pi);
                    return BadValue;
                }
            }
            else if (ki) {
                ki->driver = KdFindKeyboardDriver(option->value);
                if (!ki->driver) {
                    ErrorF("couldn't find driver!\n");
                    KdRemoveKeyboard(ki);
                    return BadValue;
                }
            }
        }
        else if (strcmp(option->key, "name") == 0) {
            if (pi)
                pi->common.name = xstrdup(option->value);
            else if (ki)
                ki->common.name = xstrdup(option->value);
        }
    }

    if (pi) {
        if (ActivateDevice(pi->common.dixdev) != Success ||
            EnableDevice(pi->common.dixdev) != TRUE) {
            ErrorF("couldn't add or enable pointer\n");
            return BadImplementation;
        }
    }
    else if (ki) {
        if (ActivateDevice(ki->common.dixdev) != Success ||
            EnableDevice(ki->common.dixdev) != TRUE) {
            ErrorF("couldn't add or enable keyboard\n");
            return BadImplementation;
        }
    }

    if (pi)
        *dev = pi->common.dixdev;
    else if (ki)
        *dev = ki->common.dixdev;

    return Success;
}

void
DeleteInputDeviceRequest(DeviceIntPtr dev)
{
    RemoveDevice(dev);
}
