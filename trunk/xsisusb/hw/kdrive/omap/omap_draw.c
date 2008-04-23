/*
 * Copyright © 2004-2006 Nokia Corporation
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
 */

#ifdef HAVE_KDRIVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include "omap.h"
#include "omapfb.h"
#include <linux/fb.h>
#include <sys/ioctl.h>

#include <X11/Xmd.h>

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>

#include "scrnintstr.h"
#include "pixmapstr.h"
#include "mistruct.h"
#include "damagestr.h"

#include "miscstruct.h"
#include "region.h"
#include "mi.h"

#ifdef XSP
#include "spext.h"
#endif

void
omap_card_sync(struct omap_card_info *omapc)
{
    struct omap_plane_info *plane;

    for (plane = omapc->planes; plane; plane = plane->next) {
        if (plane->type == OMAP_PLANE_BASE) {
            ioctl(plane->fd, OMAPFB_SYNC_GFX);
            return;
        }
    }
}

static void _X_INLINE
reset_damage(struct omap_screen_info *omaps)
{
    omaps->dirty_area.x1 = MAXSHORT;
    omaps->dirty_area.y1 = MAXSHORT;
    omaps->dirty_area.x2 = 0;
    omaps->dirty_area.y2 = 0;
}

static int _X_INLINE
region_is_null(struct omap_screen_info *omaps)
{
    return (omaps->dirty_area.x1 == MAXSHORT && omaps->dirty_area.x2 == 0 &&
            omaps->dirty_area.y1 == MAXSHORT && omaps->dirty_area.y2 == 0);
}

static int _X_INLINE
rects_intersect(xRectangle *box1, xRectangle *box2)
{
    return !(box2->x + box2->height <= box1->x ||
             box2->x >= box1->x + box1->height ||
             box2->y + box2->height <= box1->y ||
             box2->y >= box1->y + box1->height);
}

static void _X_INLINE
push_box(struct omap_screen_info *omaps, BoxPtr box)
{
    xRectangle rect;

    rect.x = box->x1;
    rect.y = box->y1;
    rect.width = box->x2 - box->x1;
    rect.height = box->y2 - box->y1;

#ifdef XSP
    XSPCheckDamage(omaps->screen->pScreen->myNum, &rect);
#endif
    omap_plane_flush(omaps->plane, &rect);
}

static void
update_screen(struct omap_screen_info *omaps)
{
    BoxPtr tmp;
    int i;

    if (region_is_null(omaps))
        return;

    if (!omaps->block_updates) {
        /* Remove the video region from our active area. */
        if (omaps->video_region &&
            REGION_NOTEMPTY(omaps->screen->pScreen, omaps->video_region) &&
            RECT_IN_REGION(omaps->screen->pScreen, omaps->video_region,
                           &omaps->dirty_area)) {
            REGION_INIT(omaps->screen->pScreen, omaps->tmp_region,
                        &omaps->dirty_area, 1);
            REGION_SUBTRACT(omaps->screen->pScreen, omaps->tmp_region,
                            omaps->tmp_region, omaps->video_region);
            tmp = REGION_RECTS(omaps->tmp_region);
            for (i = 0; i < REGION_NUM_RECTS(omaps->tmp_region); i++, tmp++)
                push_box(omaps, tmp);
            REGION_EMPTY(omaps->screen->pScreen, omaps->tmp_region);
        }
        else {
            push_box(omaps, &omaps->dirty_area);
        }

    }

    reset_damage(omaps);
}

/**
 * Update the entire screen.  If force_all is not set, then refuse to update
 * currently-playing video areas.
 */
void
omap_screen_update_all(struct omap_screen_info *omaps, Bool force_all)
{
    if (force_all) {
        omap_plane_flush(omaps->plane, NULL);
        reset_damage(omaps);
    }
    else {
        omaps->dirty_area.x1 = 0;
        omaps->dirty_area.x2 = omaps->screen->width;
        omaps->dirty_area.y1 = 0;
        omaps->dirty_area.y2 = omaps->screen->height;
        update_screen(omaps);
    }
}

static void _X_INLINE
accumulate_damage(struct omap_screen_info *omaps, RegionPtr region)
{
    int i = 0;
    BoxPtr box = REGION_RECTS(region);

    for (i = 0; i < REGION_NUM_RECTS(region); i++) {
        if (box->x1 < omaps->dirty_area.x1)
            omaps->dirty_area.x1 = box->x1;
        if (box->y1 < omaps->dirty_area.y1)
            omaps->dirty_area.y1 = box->y1;
        if (box->x2 > omaps->dirty_area.x2)
            omaps->dirty_area.x2 = box->x2;
        if (box->y2 > omaps->dirty_area.y2)
            omaps->dirty_area.y2 = box->y2;

        if (omaps->individual_updates)
            update_screen(omaps);

        box++;
    }
}

#ifdef PROFILE_ME_HARDER
static void
video_stats(struct omap_screen_info *omaps, CARD32 time)
{
    struct omap_plane_info *plane;

    for (plane = omaps->omapc->planes; plane; plane = plane->next) {
        if (plane->frames) {
            ErrorF("port %d (%d x %d): %d frames in %.4f sec (%.4f FPS)\n",
                   plane->id, plane->dst_area.width, plane->dst_area.height,
                   plane->frames, (float)(time - plane->frames_since) / 1000,
                   plane->frames / ((float)(time - plane->frames_since) / 1000));
            plane->frames = 0;
        }
        plane->frames_since = time;
    }

    omaps->updates = 0;
}
#endif

static CARD32
damage_timer(OsTimerPtr timer, CARD32 time, pointer arg)
{
    struct omap_screen_info *omaps = arg;
    int needUpdate = 0;
    RegionPtr region = NULL;
#ifdef XSP
    xspScrPrivPtr xsp_priv = NULL;
#endif

#ifdef XSP
    if (xspScrPrivateIndex > 0) {
        xsp_priv = xspGetScrPriv(omaps->screen->pScreen);
        if (xsp_priv && xsp_priv->dsp_enabled)
            needUpdate = 1;
    }
#endif

    if (!omaps->damage) {
        omaps->timer_active = 0;
        omaps->empty_updates = 0;
        return 0;
    }

#ifdef PROFILE_ME_HARDER
    omaps->updates++;
    if (omaps->updates > (5000 / OMAP_UPDATE_TIME))
        video_stats(omaps, time);
#endif

    region = DamageRegion(omaps->damage);
    if (REGION_NOTEMPTY(omaps->screen->pScreen, region)) {
        accumulate_damage(omaps, region);
        DamageEmpty(omaps->damage);
    }

    if (!region_is_null(omaps))
        needUpdate = 1;

    if (needUpdate) {
        omaps->empty_updates = 0;
        update_screen(omaps);
    }
    else {
        omaps->empty_updates++;
    }

#if !defined(PROFILE_ME_HARDER) && !defined(DEBUG)
    /* Kill the timer if we've gone more than 500ms without an update. */
    if (omaps->empty_updates >= 500 / OMAP_UPDATE_TIME) {
        omaps->timer_active = 0;
        omaps->empty_updates = 0;
        return 0;
    }
    else
#endif
    {
        return OMAP_UPDATE_TIME;
    }
}

static int
update_pixel_doubling(struct omap_screen_info *omaps, int enable)
{
    DebugF("omapUpdatePixelDoubling: pixel doubling is %d\n", enable);
    omaps->pixel_doubled = enable;
    omap_screen_update_all(omaps, TRUE);

    return Success;
}

#ifdef XSP
static int
xsp_event(int event, int screen, void *closure)
{
    struct omap_screen_info *omaps = closure;

    switch (event) {
    case XSP_EVENT_PIXEL_DOUBLE_EN:
        return update_pixel_doubling(omaps, 1);
    case XSP_EVENT_PIXEL_DOUBLE_DIS:
        return update_pixel_doubling(omaps, 0);
    default:
        return BadMatch;
    }
}
#endif

static void
damage_report_hook(DamagePtr damage, RegionPtr pRegion, void *closure)
{
    struct omap_screen_info *omaps = closure;

    if (!omaps->timer_active) {
        omaps->timer_active = 1;
        TimerSet(NULL, 0, 1, damage_timer, omaps);
    }
}

static void damage_destroy_hook(DamagePtr damage, void *closure)
{
    struct omap_screen_info *omaps = closure;

    omaps->damage = NULL;
    omaps->pixmap = NULL;
}

Bool
omap_screen_resources_create(struct omap_screen_info *omaps)
{
    PixmapPtr pixmap;

    ENTER();

    /* Set up our damage listener to update the window by hand. */
    omaps->timer_active = 0;
    omaps->empty_updates = 0;
    omaps->damage = DamageCreate(damage_report_hook, damage_destroy_hook,
                                 DamageReportNonEmpty, TRUE,
                                 omaps->screen->pScreen, omaps);
    if (!omaps->damage)
        FatalError("omapCreateDrawResources: couldn't create damage\n");
    pixmap = omaps->screen->pScreen->GetScreenPixmap(omaps->screen->pScreen);
    if (!pixmap)
        FatalError("omapCreateDrawResources: couldn't get screen pixmap\n");
    omaps->pixmap = pixmap;
    reset_damage(omaps);
    DamageRegister(&pixmap->drawable, omaps->damage);

    omaps->pixel_doubled = 0;
    omaps->individual_updates = 0;
#ifdef XSP
    XSPSetEventCallback(omaps->screen->pScreen->myNum, xsp_event, omaps);
#endif

    omaps->tmp_region = REGION_CREATE(omaps->screen->pScreen, NullBox, 0);
    if (!omaps->tmp_region)
        FatalError("omapCreateDrawResources: couldn't create temp region\n");

    LEAVE();

    return TRUE;
}

void
omap_screen_resources_destroy(struct omap_screen_info *omaps)
{
    ENTER();

    if (omaps->damage) {
        DamageDestroy(omaps->damage);
        omaps->damage = NULL;
    }

    REGION_DESTROY(omaps->screen->pScreen, omaps->tmp_region);
    omaps->tmp_region = NULL;

#ifdef XSP
    XSPSetEventCallback(omaps->screen->pScreen->myNum, NULL, NULL);
#endif

    LEAVE();
}
