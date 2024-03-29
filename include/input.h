/************************************************************

Copyright 1987, 1998  The Open Group

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


Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

********************************************************/

#ifndef INPUT_H
#define INPUT_H

#include "misc.h"
#include "screenint.h"
#include <X11/Xmd.h>
#include <X11/Xproto.h>
#include "window.h"     /* for WindowPtr */

struct xserver_option; /* dixstruct.h is too hard to include */

#define DEVICE_INIT	0
#define DEVICE_ON	1
#define DEVICE_OFF	2
#define DEVICE_CLOSE	3
#define DEVICE_RAWEVENT 16

#define POINTER_RELATIVE (1 << 1)
#define POINTER_ABSOLUTE (1 << 2)
#define POINTER_ACCELERATE (1 << 3)

#define MAP_LENGTH	256
#define DOWN_LENGTH	32	/* 256/8 => number of bytes to hold 256 bits */
#define NullGrab ((GrabPtr)NULL)
#define PointerRootWin ((WindowPtr)PointerRoot)
#define NoneWin ((WindowPtr)None)
#define NullDevice ((DevicePtr)NULL)

#ifndef FollowKeyboard
#define FollowKeyboard 		3
#endif
#ifndef FollowKeyboardWin
#define FollowKeyboardWin  ((WindowPtr) FollowKeyboard)
#endif
#ifndef RevertToFollowKeyboard
#define RevertToFollowKeyboard	3
#endif

typedef unsigned long Leds;
typedef struct _OtherClients *OtherClientsPtr;
typedef struct _InputClients *InputClientsPtr;
typedef struct _DeviceIntRec *DeviceIntPtr;

typedef int (*DeviceProc)(
    DeviceIntPtr /*device*/,
    int /*what*/);

typedef void (*ProcessInputProc)(
    xEventPtr /*events*/,
    DeviceIntPtr /*device*/,
    int /*count*/);

typedef Bool (*DeviceHandleProc)(
    DeviceIntPtr /*device*/,
    void* /*data*/
    );

typedef void (*DeviceUnwrapProc)(
    DeviceIntPtr /*device*/,
    DeviceHandleProc /*proc*/,
    void* /*data*/
    );

typedef struct _DeviceRec {
    pointer	devicePrivate;
    ProcessInputProc processInputProc;	/* current */
    ProcessInputProc realInputProc;	/* deliver */
    ProcessInputProc enqueueInputProc;	/* enqueue */
    Bool	on;			/* used by DDX to keep state */
} DeviceRec, *DevicePtr;

typedef struct {
    int			click, bell, bell_pitch, bell_duration;
    Bool		autoRepeat;
    unsigned char	autoRepeats[32];
    Leds		leds;
    unsigned char	id;
} KeybdCtrl;

typedef struct {
    KeySym  *map;
    KeyCode minKeyCode,
	    maxKeyCode;
    int     mapWidth;
} KeySymsRec, *KeySymsPtr;

typedef struct {
    int		num, den, threshold;
    unsigned char id;
} PtrCtrl;

typedef struct {
    int         resolution, min_value, max_value;
    int         integer_displayed;
    unsigned char id;
} IntegerCtrl;

typedef struct {
    int         max_symbols, num_symbols_supported;
    int         num_symbols_displayed;
    KeySym      *symbols_supported;
    KeySym      *symbols_displayed;
    unsigned char id;
} StringCtrl;

typedef struct {
    int         percent, pitch, duration;
    unsigned char id;
} BellCtrl;

typedef struct {
    Leds        led_values;
    Mask        led_mask;
    unsigned char id;
} LedCtrl;

extern int AllocateDevicePrivateIndex(void);
extern Bool AllocateDevicePrivate(DeviceIntPtr device, int index);
extern void ResetDevicePrivateIndex(void);

extern KeybdCtrl	defaultKeyboardControl;
extern PtrCtrl		defaultPointerControl;

extern void InitCoreDevices(void);

extern DeviceIntPtr AddInputDevice(
    DeviceProc /*deviceProc*/,
    Bool /*autoStart*/);

extern Bool EnableDevice(
    DeviceIntPtr /*device*/);

extern Bool ActivateDevice(
    DeviceIntPtr /*device*/);

extern Bool DisableDevice(
    DeviceIntPtr /*device*/);

extern int InitAndStartDevices(void);

extern void CloseDownDevices(void);

extern int RemoveDevice(
    DeviceIntPtr /*dev*/);

extern int NumMotionEvents(void);

extern void RegisterPointerDevice(
    DeviceIntPtr /*device*/);

extern void RegisterKeyboardDevice(
    DeviceIntPtr /*device*/);

extern DevicePtr LookupKeyboardDevice(void);

extern DevicePtr LookupPointerDevice(void);

extern DevicePtr LookupDevice(
    int /* id */);

extern void QueryMinMaxKeyCodes(
    KeyCode* /*minCode*/,
    KeyCode* /*maxCode*/);

extern Bool SetKeySymsMap(
    KeySymsPtr /*dst*/,
    KeySymsPtr /*src*/);

extern Bool InitKeyClassDeviceStruct(
    DeviceIntPtr /*device*/,
    KeySymsPtr /*pKeySyms*/,
    CARD8 /*pModifiers*/[]);

extern Bool InitButtonClassDeviceStruct(
    DeviceIntPtr /*device*/,
    int /*numButtons*/,
    CARD8* /*map*/);

typedef int (*ValuatorMotionProcPtr)(
		DeviceIntPtr /*pdevice*/,
		xTimecoord * /*coords*/,
		unsigned long /*start*/,
		unsigned long /*stop*/,
		ScreenPtr /*pScreen*/);

extern Bool InitValuatorClassDeviceStruct(
    DeviceIntPtr /*device*/,
    int /*numAxes*/,
    ValuatorMotionProcPtr /* motionProc */,
    int /*numMotionEvents*/,
    int /*mode*/);

extern Bool InitAbsoluteClassDeviceStruct(
    DeviceIntPtr /*device*/);

extern Bool InitFocusClassDeviceStruct(
    DeviceIntPtr /*device*/);

typedef void (*BellProcPtr)(
    int /*percent*/,
    DeviceIntPtr /*device*/,
    pointer /*ctrl*/,
    int);

typedef void (*KbdCtrlProcPtr)(
    DeviceIntPtr /*device*/,
    KeybdCtrl * /*ctrl*/);

extern Bool InitKbdFeedbackClassDeviceStruct(
    DeviceIntPtr /*device*/,
    BellProcPtr /*bellProc*/,
    KbdCtrlProcPtr /*controlProc*/);

typedef void (*PtrCtrlProcPtr)(
    DeviceIntPtr /*device*/,
    PtrCtrl * /*ctrl*/);

extern Bool InitPtrFeedbackClassDeviceStruct(
    DeviceIntPtr /*device*/,
    PtrCtrlProcPtr /*controlProc*/);

typedef void (*StringCtrlProcPtr)(
    DeviceIntPtr /*device*/,
    StringCtrl * /*ctrl*/);

extern Bool InitStringFeedbackClassDeviceStruct(
    DeviceIntPtr /*device*/,
    StringCtrlProcPtr /*controlProc*/,
    int /*max_symbols*/,
    int /*num_symbols_supported*/,
    KeySym* /*symbols*/);

typedef void (*BellCtrlProcPtr)(
    DeviceIntPtr /*device*/,
    BellCtrl * /*ctrl*/);

extern Bool InitBellFeedbackClassDeviceStruct(
    DeviceIntPtr /*device*/,
    BellProcPtr /*bellProc*/,
    BellCtrlProcPtr /*controlProc*/);

typedef void (*LedCtrlProcPtr)(
    DeviceIntPtr /*device*/,
    LedCtrl * /*ctrl*/);

extern Bool InitLedFeedbackClassDeviceStruct(
    DeviceIntPtr /*device*/,
    LedCtrlProcPtr /*controlProc*/);

typedef void (*IntegerCtrlProcPtr)(
    DeviceIntPtr /*device*/,
    IntegerCtrl * /*ctrl*/);


extern Bool InitIntegerFeedbackClassDeviceStruct(
    DeviceIntPtr /*device*/,
    IntegerCtrlProcPtr /*controlProc*/);

extern Bool InitPointerDeviceStruct(
    DevicePtr /*device*/,
    CARD8* /*map*/,
    int /*numButtons*/,
    ValuatorMotionProcPtr /*motionProc*/,
    PtrCtrlProcPtr /*controlProc*/,
    int /*numMotionEvents*/,
    int /*numAxes*/);

extern Bool InitKeyboardDeviceStruct(
    DevicePtr /*device*/,
    KeySymsPtr /*pKeySyms*/,
    CARD8 /*pModifiers*/[],
    BellProcPtr /*bellProc*/,
    KbdCtrlProcPtr /*controlProc*/);

extern void SendMappingNotify(
    unsigned int /*request*/,
    unsigned int /*firstKeyCode*/,
    unsigned int /*count*/,
    ClientPtr	/* client */);

extern Bool BadDeviceMap(
    BYTE* /*buff*/,
    int /*length*/,
    unsigned /*low*/,
    unsigned /*high*/,
    XID* /*errval*/);

extern Bool AllModifierKeysAreUp(
    DeviceIntPtr /*device*/,
    CARD8* /*map1*/,
    int /*per1*/,
    CARD8* /*map2*/,
    int /*per2*/);

extern void NoteLedState(
    DeviceIntPtr /*keybd*/,
    int /*led*/,
    Bool /*on*/);

extern void MaybeStopHint(
    DeviceIntPtr /*device*/,
    ClientPtr /*client*/);

extern void ProcessPointerEvent(
    xEventPtr /*xE*/,
    DeviceIntPtr /*mouse*/,
    int /*count*/);

extern void ProcessKeyboardEvent(
    xEventPtr /*xE*/,
    DeviceIntPtr /*keybd*/,
    int /*count*/);

#ifdef XKB
extern void CoreProcessPointerEvent(
    xEventPtr /*xE*/,
    DeviceIntPtr /*mouse*/,
    int /*count*/);

extern void CoreProcessKeyboardEvent(
    xEventPtr /*xE*/,
    DeviceIntPtr /*keybd*/,
    int /*count*/);
#endif

extern Bool LegalModifier(
    unsigned int /*key*/, 
    DeviceIntPtr /*pDev*/);

extern void ProcessInputEvents(void);

extern void InitInput(
    int  /*argc*/,
    char ** /*argv*/);

extern int GetMaximumEventsNum(void);

extern int GetPointerEvents(
    xEvent *events,
    DeviceIntPtr pDev,
    int type,
    int buttons,
    int flags,
    int first_valuator,
    int num_valuators,
    int *valuators);

extern int GetKeyboardEvents(
    xEvent *events,
    DeviceIntPtr pDev,
    int type,
    int key_code);

extern int GetKeyboardValuatorEvents(
    xEvent *events,
    DeviceIntPtr pDev,
    int type,
    int key_code,
    int first_valuator,
    int num_valuator,
    int *valuators);

extern int GetProximityEvents(
    xEvent *events,
    DeviceIntPtr pDev,
    int type,
    int first_valuator,
    int num_valuators,
    int *valuators);

extern void PostSyntheticMotion(
    int x,
    int y,
    int screen,
    unsigned long time);

extern int GetMotionHistorySize(
    void);

extern void AllocateMotionHistory(
    DeviceIntPtr pDev);

extern int GetMotionHistory(
    DeviceIntPtr pDev,
    xTimecoord *buff,
    unsigned long start,
    unsigned long stop,
    ScreenPtr pScreen);

extern void SwitchCoreKeyboard(DeviceIntPtr pDev);
extern DeviceIntPtr GetCoreKeyboard(void);
extern void SwitchCorePointer(DeviceIntPtr pDev);
extern DeviceIntPtr GetCorePointer(void);

extern DeviceIntPtr LookupDeviceIntRec(
    CARD8 deviceid);

/* Implemented by the DDX. */
extern int NewInputDeviceRequest(
    struct xserver_option *options,
    DeviceIntPtr *dev);
extern void DeleteInputDeviceRequest(
    DeviceIntPtr dev);

extern void DDXRingBell(
    int volume,
    int pitch,
    int duration);

#endif /* INPUT_H */
