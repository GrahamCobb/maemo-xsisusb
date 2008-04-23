/*
 * Copyright © 2006 Nokia Corporation
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
 *
 * Portions based on hw/kdrive/fbdev/fbdev.c:
 * Copyright © 1999 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_KDRIVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include <sys/ioctl.h>
#include <linux/fb.h>

#include "shadow.h"
#include "scrnintstr.h"
#include "kdrive.h"
#include "omap.h"

static Bool
omap_card_init(KdCardInfo *card)
{
    struct omap_card_info *omapc;
    char fbpath[10];
    int i;

    ENTER();

    omapc = (struct omap_card_info *) xcalloc(1, sizeof(*omapc));
    if (!omapc)
        return FALSE;

    if (!omap_plane_create(omapc, "/dev/fb0", OMAP_PLANE_BASE))
        FatalError("omapCardInit: couldn't open dispc gfx plane\n");

    for (i = 1; i < 100; i++) {
        snprintf(fbpath, sizeof(fbpath), "/dev/fb%d", i);
        if (omap_plane_create(omapc, fbpath, OMAP_PLANE_OVERLAY))
            DebugF("omapCardInit: added plane %d\n", i);
        else
            break;
    }

    card->driver = omapc;
    LEAVE();

    return TRUE;
}

static void
omap_card_fini(KdCardInfo *card)
{
    struct omap_plane_info *plane, *next;
    struct omap_card_info *omapc = card->driver;

    ENTER();

    plane = omapc->planes;
    while (plane) {
        next = plane->next;
        omap_plane_destroy(plane);
        plane = next;
    }
    omapc->planes = NULL;

    xfree(omapc);
    card->driver = NULL;

    LEAVE();
}

static Pixel
make_config(Pixel orig, Pixel others)
{
    Pixel low;

    low = lowbit(orig) >> 1;
    while (low && (others & low) == 0) {
        orig |= low;
        low >>= 1;
    }

    return orig;
}

static void
*omap_shadow_window_linear(ScreenPtr screen, CARD32 row, CARD32 offset,
                           int mode, CARD32 *size, void *closure)
{
    KdScreenPriv(screen);
    struct omap_screen_info *omaps = pScreenPriv->screen->driver;

    *size = omaps->plane->pitch;
    return omaps->plane->fb + row * omaps->plane->pitch + offset;
}

static Bool
omap_screen_init(KdScreenInfo *screen)
{
    struct omap_screen_info *omaps;
    struct omap_card_info *omapc = screen->card->driver;
    struct omap_plane_info *plane = NULL, *tmp;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    int depth = 32;
    Pixel allbits;

    ENTER();

    omaps = (struct omap_screen_info *) xcalloc(1, sizeof(*omaps));
    if (!omaps)
        FatalError("omapScreenInit: couldn't allocate omaps\n");

    omaps->omapc = omapc;
    omaps->screen = screen;
    screen->driver = omaps;	
    screen->softCursor = FALSE;

    omaps->shadowfb = 0;

    if (!screen->width)
        screen->width = omapc->orig_width;
    if (!screen->height)
        screen->height = omapc->orig_height;
    if (screen->fb[0].bitsPerPixel == 0)
        screen->fb[0].bitsPerPixel = omapc->orig_bpp;
    if (screen->fb[0].depth == 0)
        screen->fb[0].depth = omapc->orig_bpp;

    for (tmp = omapc->planes; tmp; tmp = tmp->next) {
        if (tmp->type == OMAP_PLANE_BASE)
            plane = tmp;
        tmp->omaps = omaps;
    }

    if (!plane)
        FatalError("omapScreenInit: no base plane found\n");

    plane->src_area.x = 0;
    plane->src_area.y = 0;
    plane->src_area.width = screen->width;
    plane->src_area.height = screen->height;
    memcpy(&plane->dst_area, &plane->src_area, sizeof(plane->dst_area));

    switch (screen->fb[0].bitsPerPixel) {
    case 16:
        plane->format = OMAPFB_COLOR_RGB565;
        break;
    default:
        FatalError("omapScreenInit: unsupported depth\n");
        break;
    }

    if (ioctl(plane->fd, FBIOGET_VSCREENINFO, &var) != 0)
        FatalError("omapScreenInit: couldn't get var info\n");

#define Mask(o, l) (((1 << l) - 1) << o)
    screen->fb[0].redMask = Mask(var.red.offset, var.red.length);
    screen->fb[0].greenMask = Mask(var.green.offset, var.green.length);
    screen->fb[0].blueMask = Mask(var.blue.offset, var.blue.length);
#undef Mask

    screen->fb[0].visuals = (1 << TrueColor);
    screen->fb[0].redMask = make_config(screen->fb[0].redMask,
                                        (screen->fb[0].greenMask |
                                         screen->fb[0].blueMask)); 
    screen->fb[0].greenMask = make_config(screen->fb[0].greenMask,
                                          (screen->fb[0].redMask |
                                           screen->fb[0].blueMask));
    screen->fb[0].blueMask = make_config(screen->fb[0].blueMask,
                                         (screen->fb[0].redMask |
                                          screen->fb[0].greenMask));

    allbits = screen->fb[0].redMask | screen->fb[0].greenMask |
              screen->fb[0].blueMask;
    while (depth && !(allbits & (1 << (depth - 1))))
        depth--;
    screen->fb[0].depth = depth;

    /* We don't want to sync UI updates: tearing isn't an issue, and it would
     * just slow everything down, so. */
    plane->vsync = OMAP_VSYNC_NONE;

    omaps->plane = plane;

    if (ioctl(plane->fd, FBIOGET_FSCREENINFO, &fix) != 0)
        FatalError("omapScreenInit: couldn't get fix info\n");

    if (!omap_plane_enable(omaps->plane))
        FatalError("omapScreenInit: couldn't enable base plane\n");

    screen->memory_base = plane->fb;
    screen->memory_size = plane->fb_size;
    screen->off_screen_base = screen->memory_size;

    if (omaps->shadowfb) {
        if (!KdShadowFbAlloc(screen, 0, 0))
            FatalError("omapScreenInit: couldn't set up shadow framebuffer\n");
    }
    else {
        screen->fb[0].byteStride = plane->pitch;
        screen->fb[0].pixelStride = (plane->pitch * 8 / var.bits_per_pixel);
        screen->fb[0].frameBuffer = screen->memory_base;
    }

    LEAVE();

    return TRUE;
}


static Bool
omap_init_screen(ScreenPtr screen)
{
    KdScreenPriv(screen);
    struct omap_screen_info *omaps = pScreenPriv->screen->driver;

    ENTER();

    omap_video_init(omaps);
    
    LEAVE();

    return TRUE;
}

static Bool
omap_create_resources(ScreenPtr screen)
{
    KdScreenPriv(screen);
    struct omap_screen_info *omaps = pScreenPriv->screen->driver;

    ENTER();

    if (!omap_screen_resources_create(omaps)) {
        ErrorF("omapCreateResources: couldn't create draw resources\n");
        LEAVE();
        return FALSE;
    }

    if (omaps->shadowfb) {
        if (!KdShadowSet(screen, RR_Rotate_0, shadowUpdatePacked,
                         omap_shadow_window_linear, NULL)) {
            ErrorF("omapCreateResources: couldn't enable shadow\n");
            return FALSE;
        }
    }

    LEAVE();

    return TRUE;
}

static Bool
omap_finish_init_screen(ScreenPtr screen)
{
    KdScreenPriv(screen);
    struct omap_screen_info *omaps = pScreenPriv->screen->driver;

    if (omaps->shadowfb) {
        if (!shadowSetup(screen)) {
            ErrorF("omapFinishInitScreen: couldn't finish shadow init\n");
            return FALSE;
        }
    }

    return TRUE;
}

/* FIXME: Implement real DPMS, taking it away from DSME. */
static Bool
omap_dpms(ScreenPtr screen, int mode)
{
    return TRUE;
}

static void
omap_screen_fini(KdScreenInfo *screen)
{
    struct omap_screen_info *omaps = screen->driver;

    ENTER();

    if (omaps->shadowfb)
        KdShadowFbFree(screen, 0);

    omap_video_fini(omaps);
    omap_screen_resources_destroy(omaps);

    xfree(omaps);
    screen->driver = NULL;

    LEAVE();
}

KdCardFuncs omap_funcs = {
    .cardinit         = omap_card_init,
    .cardfini         = omap_card_fini,
    .scrinit          = omap_screen_init,
    .scrfini          = omap_screen_fini,
    .initScreen       = omap_init_screen,
    .finishInitScreen = omap_finish_init_screen,
    .createRes        = omap_create_resources,
    .dpms             = omap_dpms,
};
