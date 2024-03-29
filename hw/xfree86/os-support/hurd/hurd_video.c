/*
 * Copyright 1997, 1998 by UCHIYAMA Yasushi
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of UCHIYAMA Yasushi not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  UCHIYAMA Yasushi makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * UCHIYAMA YASUSHI DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL UCHIYAMA YASUSHI BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */
/* $XFree86: xc/programs/Xserver/hw/xfree86/os-support/hurd/hurd_video.c,v 1.3 1999/04/29 12:24:52 dawes Exp $ */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <mach.h>
#include <device/device.h>
#include <mach/machine/mach_i386.h>

#include <X11/X.h>
#include "input.h"
#include "scrnintstr.h"

#include "xf86.h"
#include "xf86Priv.h"
#include "xf86_OSlib.h"
#include "xf86OSpriv.h"

/**************************************************************************
 * Video Memory Mapping section                                            
 ***************************************************************************/
pointer 
xf86MapVidMem(int ScreenNum,int Flags, unsigned long Base, unsigned long Size)
{
    mach_port_t device,iopl_dev;
    memory_object_t iopl_mem;
    kern_return_t err;
    vm_address_t addr=(vm_address_t)0;

    err = get_privileged_ports (NULL, &device);
    if( err )
    {
	errno = err;
	FatalError("xf86MapVidMem() can't get_privileged_ports. (%s)\n",strerror(errno));
    }
    err = device_open(device,D_READ|D_WRITE,"iopl",&iopl_dev);
    mach_port_deallocate (mach_task_self(), device);
    if( err )
    {
	errno = err;
	FatalError("xf86MapVidMem() can't device_open. (%s)\n",strerror(errno));
    }

    err = device_map(iopl_dev,VM_PROT_READ|VM_PROT_WRITE, Base , Size ,&iopl_mem,0);
    if( err )
    {
	errno = err;
	FatalError("xf86MapVidMem() can't device_map. (%s)\n",strerror(errno));
    }
    err = vm_map(mach_task_self(),
		 &addr,
		 Size,
		 0,     /* mask */
		 TRUE,  /* anywhere */
		 iopl_mem,
		 (vm_offset_t)Base,
		 FALSE, /* copy on write */
		 VM_PROT_READ|VM_PROT_WRITE,
		 VM_PROT_READ|VM_PROT_WRITE,
		 VM_INHERIT_SHARE);
    mach_port_deallocate(mach_task_self(),iopl_mem);
    if( err )
    {
	errno = err;
	FatalError("xf86MapVidMem() can't vm_map.(iopl_mem) (%s)\n",strerror(errno));
    }
    mach_port_deallocate(mach_task_self(),iopl_dev);
    if( err )
    {
	errno = err;
	FatalError("xf86MapVidMem() can't mach_port_deallocate.(iopl_dev) (%s)\n",strerror(errno));
    }
    return (pointer)addr;
}

void 
xf86UnMapVidMem(int ScreenNum,pointer Base,unsigned long Size)
{
    kern_return_t err = vm_deallocate(mach_task_self(), (int)Base, Size);
    if( err )
    {
	errno = err;
	ErrorF("xf86UnMapVidMem: can't dealloc framebuffer space (%s)\n",strerror(errno));
    }
    return;
}

Bool 
xf86LinearVidMem()
{
    return(TRUE);
}

/**************************************************************************
 * I/O Permissions section                                                 
 ***************************************************************************/
mach_port_t io_port;

Bool
xf86EnableIO()
{
    mach_port_t device;
    kern_return_t err;

    err = get_privileged_ports(NULL, &device);
    if( err )
    {
	errno = err;
	FatalError("xf86EnableIO() can't get_privileged_ports. (%s)\n",strerror(errno));
    }
    err = device_open(device,D_READ|D_WRITE,"io",&io_port);
    mach_port_deallocate(mach_task_self(), device);
    if( err )
    {
	errno = err;
	FatalError("xf86EnableIO() can't device_open. (%s)\n",strerror(errno));
    }

    err = i386_io_port_add(mach_thread_self (), io_port);
    if( err )
    {
	errno = err;
	FatalError("xf86EnableIO() can't i386_io_port_add.(io_port) (%s)\n",strerror(errno));
    }
    return TRUE;
}
	
void
xf86DisableIO()
{
    kern_return_t err;

    err = i386_io_port_remove(mach_thread_self (), io_port);
    if( err )
    {
	errno = err;
	FatalError("xf86DisableIO() can't i386_io_port_remove.(io_port) (%s)\n",strerror(errno));
    }
    mach_port_deallocate(mach_task_self(), io_port);
    return;
}

/**************************************************************************
 * Interrupt Handling section                                              
 **************************************************************************/
Bool 
xf86DisableInterrupts()
{
    return TRUE;
}
void 
xf86EnableInterrupts()
{
    return;
}

void
xf86MapReadSideEffects(int ScreenNum, int Flags, pointer Base,
	unsigned long Size)
{
}

Bool
xf86CheckMTRR(int s)
{
	return FALSE;
}

