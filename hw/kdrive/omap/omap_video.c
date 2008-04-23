/*
 * Copyright © 2004 Keith Packard
 * Copyright © 2005 Eric Anholt
 * Copyright © 2006-2007 Nokia Corporation
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
 * Based on ati_video.c by Eric Anholt, which was based on mach64video.c by
 * Keith Packard.
 */

#ifdef HAVE_KDRIVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include "omap.h"
#include "omapfb.h"
#include <linux/fb.h>
#include <sys/ioctl.h>

#include <X11/Xatom.h>
#include <X11/extensions/Xv.h>
#include "fourcc.h"

#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#ifndef max
#define max(x, y) (((x) >= (y)) ? (x) : (y))
#endif

static KdVideoEncodingRec dummy_encoding[] = {
    /* Max width and height are filled in later. */
    { 0, "XV_IMAGE", -1, -1, { 1, 1 } },
};

static KdImageRec xv_images[] = {
    XVIMAGE_YUY2, /* OMAPFB_COLOR_YUY422 */
    XVIMAGE_UYVY, /* OMAPFB_COLOR_YUV422 */
    XVIMAGE_I420, /* OMAPFB_COLOR_YUV420 */
    XVIMAGE_YV12, /* OMAPFB_COLOR_YUV420 */
};

static KdVideoFormatRec xv_formats[] = {
    { 16, TrueColor },
};

static KdAttributeRec xv_attributes[] = {
    { XvSettable | XvGettable, OMAP_VSYNC_NONE, OMAP_VSYNC_FORCE,
      "XV_OMAP_VSYNC" },
    { XvSettable | XvGettable, 0, 0xffff, "XV_COLORKEY" },
};

static Atom xv_colorkey, xv_vsync, _omap_video_overlay;

static void omap_video_stop(KdScreenInfo *screen, pointer data, Bool exit);

/**
 * Check if plane attributes have changed.
 */
static _X_INLINE int
is_dirty(struct omap_video_info *video_info, int id, int src_w, int src_h,
         int dst_x, int dst_y, int dst_w, int dst_h)
{
    const struct omap_plane_info *plane = video_info->plane;

    if (plane->dirty || video_info->fourcc != id || video_info->src_w != src_w ||
        video_info->src_h != src_h || video_info->dst_x != dst_x ||
        video_info->dst_y != dst_y || video_info->dst_w != dst_w ||
        video_info->dst_h != dst_h || plane->ext_state & OMAP_EXT_PENDING)
        return 1;
    else
        return 0;
}

static void
drawable_destroyed(struct omap_screen_info *omaps, DrawablePtr drawable)
{
    int i;
    struct omap_video_info *video_info;

    for (i = 0; i < omaps->num_video_ports; i++) {
        video_info = get_omap_video_info(omaps, i);
        if (video_info->drawable == drawable)
            video_info->drawable = NULL;
    }
}

static Bool
destroy_pixmap_hook(PixmapPtr pixmap)
{
    Bool ret;
    ScreenPtr screen = pixmap->drawable.pScreen;
    KdScreenPriv(screen);
    struct omap_screen_info *omaps = get_omap_screen_info(pScreenPriv);

    drawable_destroyed(omaps, (DrawablePtr) pixmap);

    screen->DestroyPixmap = omaps->destroy_pixmap;
    ret = screen->DestroyPixmap(pixmap);
    screen->DestroyPixmap = destroy_pixmap_hook;

    return ret;
}

static Bool
destroy_window_hook(WindowPtr window)
{
    Bool ret;
    ScreenPtr screen = window->drawable.pScreen;
    KdScreenPriv(screen);
    struct omap_screen_info *omaps = get_omap_screen_info(pScreenPriv);

    drawable_destroyed(omaps, (DrawablePtr) window);

    screen->DestroyWindow = omaps->destroy_window;
    ret = screen->DestroyWindow(window);
    screen->DestroyWindow = destroy_window_hook;

    return ret;
}

static void
change_overlay_property(struct omap_video_info *video_info, int val)
{
    WindowPtr window;
    int err;

    if (video_info->drawable &&
        video_info->drawable->type == DRAWABLE_WINDOW) {
        /* Walk the tree to get the top-level window. */
        for (window = (WindowPtr) video_info->drawable;
             window && window->parent; window = window->parent) {
            err = ChangeWindowProperty(window, _omap_video_overlay,
                                       XA_INTEGER, 8, PropModeReplace, 1,
                                       &val, FALSE);
            if (err != Success)
                ErrorF("change_overlay_property: failed to change property\n");
        }
    }
    else {
        if (video_info->drawable)
            DebugF("change_overlay_property: not changing property: type is %d\n",
                   video_info->drawable->type);
        else
            DebugF("change_overlay_property: not changing property: no overlay\n");
    }
}

static void
cancel_frame_timer(struct omap_video_info *video_info)
{
    if (video_info->frame_timer) {
        TimerCancel(video_info->frame_timer);
        video_info->frame_timer = NULL;
    }
}

/**
 * Since we can have differing formats in the framebuffer, we want to
 * block updates for a couple of frames.
 */
static void
unblock_updates(struct omap_video_info *video_info)
{
    if (video_info->migration_timer) {
        TimerCancel(video_info->migration_timer);
        video_info->migration_timer = NULL;
    }

    video_info->omaps->block_updates &= ~(1 << video_info->plane->id);
}

static void
empty_clip(struct omap_video_info *video_info)
{
    change_overlay_property(video_info, 0);

    if (REGION_NOTEMPTY(video_info->omaps->screen->pScreen, &video_info->clip)) {
        if (video_info->drawable)
            omap_paint_ckey(video_info->drawable, &video_info->clip,
                            video_info->omaps->screen->pScreen->blackPixel);
        video_info->drawable = NULL;
        REGION_SUBTRACT(video_info->omaps->screen->pScreen,
                        video_info->omaps->video_region,
                        video_info->omaps->video_region, &video_info->clip);
        REGION_EMPTY(video_info->omaps->screen->pScreen, &video_info->clip);
        omap_card_sync(video_info->omaps->omapc);
    }
}

/**
 * Stop the video overlay, optionally clearing both the memory, and the clip
 * region.  Don't set either to FALSE unless you know what you're doing.
 */
static void
stop_video(struct omap_video_info *video_info, Bool clearmem)
{
    int is_migrated;

    if ((OMAP_GET_EXT(video_info->plane) == OMAP_EXT_MIGRATED ||
        video_info->plane->ext_state == (OMAP_EXT_CANDIDATE | OMAP_EXT_PENDING)))
        is_migrated = 1;
    else
        is_migrated = 0;

    omap_card_sync(video_info->omaps->omapc);
    change_overlay_property(video_info, 0);

    if (is_migrated || video_info->visibility >= VisibilityFullyObscured)
        empty_clip(video_info);

    if (clearmem)
        bzero(video_info->plane->fb, video_info->plane->fb_size);

    if (video_info->plane->state >= OMAP_STATE_ACTIVE)
        omap_plane_disable(video_info->plane);

    video_info->omaps->individual_updates &= ~(1 << video_info->plane->id);
    video_info->plane->state = OMAP_STATE_GRABBED;

    cancel_frame_timer(video_info);

    if (is_migrated) {
        DebugF("omap stop_video: forcing full-screen update on %d (non-exit)\n",
               video_info->plane->id);
        omap_screen_update_all(video_info->omaps, TRUE);
    }

    DebugF("omap stop_video: stopped plane %d\n", video_info->plane->id);
}

/**
 * We need to push at least one frame every 30 seconds, else Hailstorm gets
 * extremely confused.
 */
static CARD32
frame_update_timer(OsTimerPtr timer, CARD32 now, pointer data)
{
    struct omap_video_info *video_info = data;

    DebugF("omap/video: frame_update_timer: timeout on plane %d\n",
           video_info->plane->id);
    video_info->frame_timer = NULL;

    stop_video(video_info, TRUE);
    empty_clip(video_info);
    omap_screen_update_all(video_info->omaps, TRUE);

    return 0;
}

static void
set_frame_timer(struct omap_video_info *video_info)
{
    if (OMAP_GET_EXT(video_info->plane) != OMAP_EXT_MIGRATED)
        return;

    if (video_info->frame_timer)
        TimerCancel(video_info->frame_timer);
    video_info->frame_timer = TimerSet(NULL, 0, 30000, frame_update_timer,
                                       video_info);
}

static void
push_frame(KdScreenInfo *screen, struct omap_video_info *video_info)
{

    /* If updates have been blocked because of migration, we need to
     * ensure that the colourkey has been correctly painted, and
     * the migration has been completed, before we force an
     * update. */
    if (video_info->plane->ext_state & OMAP_EXT_PENDING) {
        DebugF("omap/push_frame: plane %d pending migration\n",
               video_info->plane->id);
        omap_card_sync(video_info->omaps->omapc);
    }
    else if (video_info->omaps->block_updates & (1 << video_info->plane->id)) {
        omap_card_sync(video_info->omaps->omapc);
        DebugF("omap/push_frame: unblocking updates for %d\n",
               video_info->plane->id);
        unblock_updates(video_info);

        if (!video_info->omaps->block_updates)
            omap_screen_update_all(video_info->omaps, FALSE);
    }
    else {
        /* Due to an omapfb bug, we can't push _anything_ while a migration
         * is pending. */
        if (!video_info->omaps->block_updates)
            omap_plane_flush(video_info->plane, NULL);
        set_frame_timer(video_info);
    }
}

static CARD32
block_updates_timer(OsTimerPtr timer, CARD32 now, pointer data)
{
    struct omap_video_info *video_info = data;

    DebugF("omap/block_updates_timer: timeout on plane %d, ext state %x\n",
           video_info->plane->id, video_info->plane->ext_state);
    video_info->migration_timer = NULL;

    unblock_updates(video_info);

    switch (video_info->plane->ext_state) {
    case OMAP_EXT_NONE:
        break;

    case OMAP_EXT_MIGRATED:
        push_frame(video_info->omaps->screen, video_info);
        omap_screen_update_all(video_info->omaps, FALSE);
        break;

    case (OMAP_EXT_CANDIDATE | OMAP_EXT_PENDING):
    case (OMAP_EXT_MIGRATED | OMAP_EXT_PENDING):
        empty_clip(video_info);
        stop_video(video_info, TRUE);
    default:
        omap_screen_update_all(video_info->omaps, TRUE);
        break;
    }

    return 0;
}

static void
block_updates(struct omap_video_info *video_info)
{
    if (video_info->migration_timer)
        TimerCancel(video_info->migration_timer);
    video_info->omaps->block_updates |= (1 << video_info->plane->id);
    video_info->migration_timer = TimerSet(NULL, 0, 2000, block_updates_timer,
                                (pointer) video_info);
}

static void
migrate_back(struct omap_video_info *video_info)
{
    DebugF("omap/migrate_back: migrating clipped video %d\n",
           video_info->plane->id);
    video_info->plane->ext_state = OMAP_EXT_CANDIDATE | OMAP_EXT_PENDING;
    if (video_info->visibility == VisibilityPartiallyObscured) {
        DebugF("omap/migrate_back: blocking UI updates for %d\n",
               video_info->plane->id);
        block_updates(video_info);
    }
    else {
        DebugF("omap/migrate_back: not blocking updates for fully "
               "obscured video %d\n", video_info->plane->id);
        unblock_updates(video_info);
        
        empty_clip(video_info);
    }
    video_info->plane->dirty = TRUE;
}

static void
check_clip(struct omap_video_info *video_info)
{
    struct omap_video_info *tmp;
    int already_scaled = 0;
    int is_migrated = (OMAP_GET_EXT(video_info->plane) == OMAP_EXT_MIGRATED);
    int is_candidate = (OMAP_GET_EXT(video_info->plane) == OMAP_EXT_CANDIDATE);
    int i;

    /* Check for an already active, scaled, plane. */
    for (i = 0; i < video_info->omaps->num_video_ports; i++) {
        tmp = get_omap_video_info(video_info->omaps, i);
        if (tmp != video_info && tmp->plane->type == OMAP_PLANE_OVERLAY &&
            tmp->plane->state == OMAP_STATE_ACTIVE) {
            if (tmp->dst_w > tmp->src_w || tmp->dst_h > tmp->src_h)
                already_scaled = 1;
        }
    }

    /* If we've got a candidate, we're unobscured, and there's nothing
     * standing in our way, then migrate to Hailstorm. */
    if (video_info->visibility == VisibilityUnobscured &&
        is_candidate) {
        if (already_scaled) {
            DebugF("omap/check_clip: not migrating %d due to scaling\n",
                   video_info->plane->id);
            return;
        }

        DebugF("omap/check_clip: migrating unclipped candidate %d\n",
               video_info->plane->id);
        video_info->plane->ext_state = OMAP_EXT_MIGRATED | OMAP_EXT_PENDING;
        video_info->plane->dirty = TRUE;
        DebugF("omap/check_clip: blocking UI updates for %d\n",
               video_info->plane->id);
        block_updates(video_info);

        return;
    }

    /* If there's another video playing, migrate our Hailstorm stream back
     * to dispc. */
    if (already_scaled && is_migrated) {
        DebugF("omap/check_clip: migrating %d back due to new scaled stream\n",
               video_info->plane->id);
        migrate_back(video_info);
        return;
    }

    /* If we've been clipped away, migrate our Hailstorm stream back to
     * dispc. */
    if (video_info->visibility != VisibilityUnobscured && is_migrated) {
        DebugF("omap/check_clip: migrating %d back due to new stream\n",
               video_info->plane->id);
        migrate_back(video_info);
        return;
    }

    /* If we're completely invisible, then just destroy our clip. */
    if (video_info->visibility >= VisibilityFullyObscured &&
        is_migrated) {
        empty_clip(video_info);
        return;
    }
}

/**
 * When the clip on a window changes, check it and stash it away, so we
 * don't end up with any clipped windows on the external controller.
 */
static void
omap_video_clip_notify(KdScreenInfo *screen, void *data, WindowPtr window, int dx,
                       int dy)
{
    struct omap_video_info *video_info = data;

    video_info->visibility = window->visibility;
    check_clip(video_info);
}

/**
 * Xv attributes get/set support.
 */
static int
omap_video_get_attribute(KdScreenInfo *screen, Atom attribute, int *value,
                         pointer data)
{
    struct omap_video_info *video_info = data;

    ENTER();

    if (attribute == xv_vsync) {
        *value = video_info->plane->vsync;
        LEAVE();
        return Success;
    }
    else if (attribute == xv_colorkey) {
        *value = video_info->plane->colorkey;
        LEAVE();
        return Success;
    }

    LEAVE();
    return BadMatch;
}

static int
omap_video_set_attribute(KdScreenInfo *screen, Atom attribute, int value,
                         pointer data)
{
    struct omap_video_info *video_info = data;

    ENTER();

    if (attribute == xv_vsync) {
        if (value < OMAP_VSYNC_NONE || value > OMAP_VSYNC_FORCE) {
            LEAVE();
            return BadValue;
        }

        if (!(video_info->plane->caps & OMAPFB_CAPS_TEARSYNC) && value) {
            ErrorF("omap_video_set_attribute: requested vsync on a non-sync "
                   "capable port\n");
            LEAVE();
            return BadValue;
        }

        video_info->plane->vsync = value;
        LEAVE();
        return Success;
    }
    else if (attribute == xv_colorkey) {
        if (value < 0 || value > 0xffff) {
            LEAVE();
            return BadValue;
        }

        video_info->plane->colorkey = value;
        video_info->plane->dirty = TRUE;
        LEAVE();
        return Success;
    }

    LEAVE();
    return BadMatch;
}

/**
 * Clip the image size to the visible screen.
 */
static void
omap_video_query_best_size(KdScreenInfo *screen, Bool motion, short vid_w,
                           short vid_h, short dst_w, short dst_h,
                           unsigned int *p_w, unsigned int *p_h, pointer data)
{
    if (dst_w < screen->width)
        *p_w = dst_w;
    else
        *p_w = screen->width;

    if (dst_h < screen->width)
        *p_h = dst_h;
    else
        *p_h = screen->width;
}


/**
 * Start the video overlay; relies on data in video_info being sensible for
 * the current frame.
 */
static int
start_video(struct omap_video_info *video_info)
{
    struct omap_video_info *tmp;
    int i;

    if (video_info->plane->state >= OMAP_STATE_ACTIVE) {
        DebugF("omap/start_video: plane %d still active!\n",
               video_info->plane->id);
        stop_video(video_info, TRUE);
        usleep(5000);
    }

    /* Hilariously, this will actually trigger upscaling for odd widths,
     * since we'll output n-1 pixels, and have the engine scale to n;
     * dispc hangs if you try to feed it sub-macroblocks.
     *
     * When feeding YUV420 to the external controller, we have 4x2
     * macroblocks in essence, so lop off up to the last three lines, and
     * let the hardware scale.
     */
    video_info->plane->src_area.x = 0;
    video_info->plane->dst_area.x = video_info->dst_x;
    video_info->plane->dst_area.width = video_info->dst_w;

    if (video_info->hscale)
        video_info->plane->src_area.width = video_info->dst_w & ~1;
    else
        video_info->plane->src_area.width = video_info->src_w;

    video_info->plane->src_area.y = 0;
    video_info->plane->dst_area.y = video_info->dst_y;
    video_info->plane->dst_area.height = video_info->dst_h;

    if (video_info->vscale)
        video_info->plane->src_area.height = video_info->dst_h & ~1;
    else
        video_info->plane->src_area.height = video_info->src_h;

    if (!omap_plane_enable(video_info->plane)) {
        DebugF("omap/start_video: couldn't enable plane %d\n",
               video_info->plane->id);
        return 0;
    }

    video_info->omaps->individual_updates |= (1 << video_info->plane->id);
    video_info->plane->state = OMAP_STATE_ACTIVE;

    /* Force a migration sanity check on any migrated video: this will
     * force it to drop back to dispc, as we can't have a dispc and a
     * Hailstorm video simultaneously. */
    for (i = 0; i < video_info->omaps->num_video_ports; i++) {
        tmp = get_omap_video_info(video_info->omaps, i);

        if (tmp != video_info && OMAP_GET_EXT(tmp->plane) == OMAP_EXT_MIGRATED)
            check_clip(tmp);
    }

    set_frame_timer(video_info);

    change_overlay_property(video_info, 1);

    DebugF("omap/start_video: enabled plane %d\n", video_info->plane->id);

    return 1;
}

/**
 * Stop an overlay.  exit is whether or not the client's exiting.
 */
void
omap_video_stop(KdScreenInfo *screen, pointer data, Bool exit)
{
    struct omap_video_info *video_info = data;
    struct omap_screen_info *omaps = video_info->omaps;

    ENTER();

    stop_video(video_info, TRUE);

    if (video_info->migration_timer) {
        TimerCancel(video_info->migration_timer);
        video_info->migration_timer = NULL;
    }

    video_info->plane->dirty = TRUE;

    if (exit) {
        empty_clip(video_info);

        if (omaps->block_updates & (1 << video_info->plane->id)) {
            DebugF("omap_video_stop: unblocking updates for %d\n",
                   video_info->plane->id);
            unblock_updates(video_info);
        }

        if (video_info->plane->caps & OMAPFB_CAPS_TEARSYNC)
            video_info->plane->vsync = OMAP_VSYNC_TEAR;
        else
            video_info->plane->vsync = OMAP_VSYNC_NONE;

        video_info->visibility = VisibilityPartiallyObscured;
        video_info->plane->ext_state = OMAP_EXT_NONE;
        video_info->plane->state = OMAP_STATE_STOPPED;
        video_info->plane->colorkey = OMAP_DEFAULT_CKEY;
    }

    video_info->drawable = NULL;

    LEAVE();
}


/**
 * Set up video_info with the specified parameters, and start the overlay.
 */
static Bool
setup_overlay(KdScreenInfo *screen, struct omap_video_info *video_info, int id,
              int src_w, int src_h, int dst_x, int dst_y, int dst_w,
              int dst_h, DrawablePtr drawable)
{
    WindowPtr window;
    ENTER();

    if (video_info->plane->state >= OMAP_STATE_ACTIVE) {
        DebugF("omap/setup_overlay: restarting overlay %d\n",
               video_info->plane->id);
        stop_video(video_info, TRUE);
    }

    if (dst_w >= src_w)
        video_info->hscale = FALSE;
    else
        video_info->hscale = TRUE;
    if (dst_h >= src_h)
        video_info->vscale = FALSE;
    else
        video_info->vscale = TRUE;

    video_info->src_w = src_w;
    video_info->src_h = src_h;
    video_info->dst_w = dst_w;
    video_info->dst_h = dst_h;
    video_info->dst_x = dst_x;
    video_info->dst_y = dst_y;
    video_info->fourcc = id;
    video_info->drawable = drawable;
    video_info->plane->ext_state = OMAP_EXT_NONE;

    switch (id) {
    case FOURCC_YV12:
    case FOURCC_I420:
        if ((video_info->plane->colors & OMAPFB_COLOR_YUV420) &&
            !video_info->hscale && !video_info->vscale &&
            !(dst_x & 3) && !(dst_w & 3) && !(dst_y & 1) && !(dst_h & 1)) {
            video_info->plane->ext_state = OMAP_EXT_CANDIDATE;
        }
    case FOURCC_YUY2:
        video_info->plane->format = OMAPFB_COLOR_YUY422;
        video_info->plane->bpp = 2;
        break;
    case FOURCC_UYVY:
        video_info->plane->format = OMAPFB_COLOR_YUV422;
        video_info->plane->bpp = 2;
        break;
    default:
        ErrorF("omap_setup_overlay: bad FourCC %d!\n", video_info->fourcc);
        LEAVE();
        return FALSE;
    }

    if (drawable->type == DRAWABLE_WINDOW) {
        window = (WindowPtr) drawable;
        video_info->visibility = window->visibility;
    }
    else {
        video_info->visibility = VisibilityPartiallyObscured;
    }
    check_clip(video_info);

    video_info->plane->dirty = TRUE;

    LEAVE();

    return start_video(video_info);
}

/**
 * ReputImage hook.  We always fail here if we're mid-migration or
 * on Hailstorm; we want stopped video to actually be _stopped_, due
 * to Hailstorm limitations.
 */
static int
omap_video_reput(KdScreenInfo *screen, DrawablePtr drawable, short drw_x,
                 short drw_y, RegionPtr clipBoxes, pointer data)
{
    struct omap_video_info *video_info = data;
    int ret;

    ENTER();

    switch (video_info->plane->ext_state) {
    case OMAP_EXT_CANDIDATE:
    case OMAP_EXT_NONE:
        if (!REGION_EQUAL(screen->pScreen, clipBoxes, &video_info->clip)) {
            REGION_SUBTRACT(screen->pScreen, video_info->omaps->video_region,
                            video_info->omaps->video_region, &video_info->clip);
            REGION_COPY(screen->pScreen, &video_info->clip, clipBoxes);
            REGION_UNION(screen->pScreen, video_info->omaps->video_region,
                         video_info->omaps->video_region, &video_info->clip);
        }

        omap_paint_ckey(drawable, &video_info->clip,
                        video_info->plane->colorkey);
        omap_card_sync(video_info->omaps->omapc);
        push_frame(screen, video_info);
        omap_screen_update_all(video_info->omaps, FALSE);
        ret = Success;
        break;

    default:
        omap_screen_update_all(video_info->omaps, FALSE);
        ret = BadValue;
    }

    return ret;
}

/**
 * XvPutImage hook.  This does not deal with rotation or partial updates.
 *
 * Calls out to omapCopyPlanarData (unobscured planar video),
 * omapExpandPlanarData (downscaled planar),
 * omapCopyPackedData (downscaled packed), KdXVCopyPlanarData (obscured planar),
 * or KdXVCopyPackedData (packed).
 */
static int
omap_video_put(KdScreenInfo *screen, DrawablePtr drawable,
               short src_x, short src_y,
               short dst_x, short dst_y,
               short src_w, short src_h,
               short dst_w, short dst_h,
               int id,
               unsigned char *buf,
               short width,
               short height,
               Bool sync,
               RegionPtr clip_boxes,
               pointer data)
{
    struct omap_video_info *video_info = (struct omap_video_info *) data;
    int updates_blocked = 0;
    int need_ckey = 0;

    if (dst_x + dst_w > screen->width || dst_y + dst_h > screen->height) {
        ErrorF("omap/put_image: specified dimensions (%d, %d) at (%d, %d) are "
               "larger than the screen (%d, %d)\n", dst_w, dst_h, dst_x,
               dst_y, screen->width, screen->height);
        return BadValue;
    }
    if (width != src_w || height != src_h) {
        ErrorF("omap/put_image: can't put partial images\n");
        return BadValue;
    }

    if (is_dirty(video_info, id, src_w, src_h, dst_x, dst_y, dst_w, dst_h) ||
        !video_info->plane->fb) {
        if (!(video_info->omaps->block_updates & (1 << video_info->plane->id))) {
            DebugF("omap/put_image: blocking updates for %d\n",
                   video_info->plane->id);
            block_updates(video_info);
            updates_blocked = 1;
        }

        if (!setup_overlay(screen, video_info, id, src_w, src_h, dst_x,
                           dst_y, dst_w, dst_h, drawable)) {
            ErrorF("omap/put_image: failed to set up overlay: from (%d, %d) "
                   "to (%d, %d) at (%d, %d) on plane %d\n", src_w, src_h,
                   dst_w, dst_h, dst_x, dst_y, video_info->plane->id);
            if (updates_blocked) {
                DebugF("omap/put_image: unblocking updates for %d\n",
                       video_info->plane->id);
                unblock_updates(video_info);
            }
            return BadAlloc;
        }
    }

    if (!REGION_EQUAL(screen->pScreen, &video_info->clip, clip_boxes)) {
        REGION_SUBTRACT(screen->pScreen, video_info->omaps->video_region,
                        video_info->omaps->video_region, &video_info->clip);
        REGION_COPY(screen->pScreen, &video_info->clip, clip_boxes);
        REGION_UNION(screen->pScreen, video_info->omaps->video_region,
                     video_info->omaps->video_region, &video_info->clip);
        need_ckey = 1;
    }

#if 0
    DebugF("omap/put_image: putting image from (%d, %d, %d, %d) to "
           "(%d, %d, %d, %d)\n", src_x, src_y, src_w, src_h, dst_x, dst_y,
           dst_w, dst_h);
#endif

    /* Sync the engine first, so we don't draw over something that's still
     * being scanned out. */
    if (video_info->plane->vsync != OMAP_VSYNC_NONE)
        omap_card_sync(video_info->omaps->omapc);

    /* dispc locks up when we try downscaling, so we do it in software
     * instead.  The external controller can scale just fine, though. */
    switch (id) {
    case FOURCC_UYVY:
    case FOURCC_YUY2:
        if (video_info->hscale || video_info->vscale)
            omap_copy_scale_packed(screen, video_info, buf, video_info->plane->fb,
                                   RR_Rotate_0, width << 1, video_info->plane->pitch,
                                   src_w, src_h, src_x, src_y, height,
                                   width, dst_w, dst_h);
        else
            omap_copy_packed(screen, buf, video_info->plane->fb, RR_Rotate_0,
                             width << 1, video_info->plane->pitch, src_w, src_h, src_x,
                             src_y, height, width);
        break;

    case FOURCC_YV12:
    case FOURCC_I420:
        if (OMAP_GET_EXT(video_info->plane) == OMAP_EXT_MIGRATED) {
            omap_copy_yuv420(screen, video_info, buf, video_info->plane->fb,
                             RR_Rotate_0, width, width >> 1,
                             video_info->plane->pitch, src_w, src_h, src_x,
                             src_y, height, width, id);
        }
        else {
            if (video_info->hscale || video_info->vscale) {
                omap_copy_scale_planar(screen, video_info, buf,
                                       video_info->plane->fb, RR_Rotate_0,
                                       width, (width >> 1),
                                       video_info->plane->pitch, src_w, src_h,
                                       height, src_x, src_y, height,
                                       width, id, dst_w, dst_h);
            }
            else {
                omap_copy_planar(screen, buf, video_info->plane->fb, RR_Rotate_0,
                                 width, (width >> 1), video_info->plane->pitch,
                                 src_w, src_h, height, src_x, src_y, height,
                                 width, id);
            }
        }
        break;
    }

    push_frame(screen, video_info);
    video_info->plane->ext_state &= ~(OMAP_EXT_PENDING);

    if (need_ckey)
        omap_paint_ckey(drawable, &video_info->clip,
                        video_info->plane->colorkey);

    return Success;    
}

/**
 * Give image size and pitches.
 */
static int
omap_video_query_attributes(KdScreenInfo *screen, int id, unsigned short *w_out,
                            unsigned short *h_out, int *pitches, int *offsets)
{
    int size = 0, tmp = 0;
    int w, h;

    if (*w_out > screen->width)
        *w_out = screen->width;
    if (*h_out > screen->width)
        *h_out = screen->width;

    w = *w_out;
    h = *h_out;

    if (offsets)
        offsets[0] = 0;

    switch (id)
    {
    case FOURCC_I420:
    case FOURCC_YV12:
        w = (w + 3) & ~3;
        h = (h + 1) & ~1;
        size = w;
        if (pitches)
            pitches[0] = size;
        size *= h;
        if (offsets)
            offsets[1] = size;
        tmp = w >> 1;
        tmp = (tmp + 3) & ~3;
        if (pitches)
            pitches[1] = pitches[2] = tmp;
        tmp *= h >> 1;
        size += tmp;
        if (offsets)
            offsets[2] = size;
        size += tmp;
        break;
    case FOURCC_UYVY:
    case FOURCC_YUY2:
    default:
        w = (w + 1) & ~1;
        size = w << 1;
        if (pitches)
            pitches[0] = size;
        size *= h;
        break;
    }

    return size;
}

/**
 * Set up all our internal structures.
 */
static KdVideoAdaptorPtr
omap_video_setup_adaptors(struct omap_screen_info *omaps)
{
    KdVideoAdaptorPtr adapt;
    struct omap_video_info *video_info;
    struct omap_plane_info *plane;
    int i;

    omaps->num_video_ports = 0;
    for (plane = omaps->omapc->planes; plane; plane = plane->next) {
        if (plane->type == OMAP_PLANE_OVERLAY &&
            (plane->colors & (OMAPFB_COLOR_YUV422 | OMAPFB_COLOR_YUY422)))
            omaps->num_video_ports++;
    }

    /* No usable video overlays. */
    if (omaps->num_video_ports == 0)
        return NULL;

    adapt = xcalloc(1, sizeof(KdVideoAdaptorRec) +
                       omaps->num_video_ports *
                        (sizeof(*video_info) + sizeof(DevUnion)));
    if (!adapt)
        return NULL;

    dummy_encoding[0].width = omaps->screen->width;
    dummy_encoding[0].height = omaps->screen->height;

    adapt->type = XvWindowMask | XvInputMask | XvImageMask;
    adapt->flags = (VIDEO_CLIP_TO_VIEWPORT | VIDEO_OVERLAID_IMAGES);
    adapt->name = "OMAP Video Overlay";
    adapt->nEncodings = 1;
    adapt->pEncodings = dummy_encoding;
    adapt->nFormats = ARRAY_SIZE(xv_formats);
    adapt->pFormats = xv_formats;
    adapt->nPorts = omaps->num_video_ports;
    adapt->pPortPrivates = (DevUnion *)(&adapt[1]);
    
    video_info = (struct omap_video_info *)
                  (&adapt->pPortPrivates[omaps->num_video_ports]);

    plane = omaps->omapc->planes;
    for (i = 0; i < omaps->num_video_ports; i++) {
        while (plane->type != OMAP_PLANE_OVERLAY ||
               !(plane->colors & (OMAPFB_COLOR_YUV422 | OMAPFB_COLOR_YUY422)))
            plane = plane->next;

        video_info[i].plane = plane;
        video_info[i].omaps = omaps;
        video_info[i].visibility = VisibilityPartiallyObscured;
        video_info[i].drawable = NULL;
        REGION_INIT(pScreen, &video_info[i].clip, NullBox, 0);

        adapt->pPortPrivates[i].ptr = &video_info[i];

        plane = plane->next;
    }

    adapt->nAttributes = ARRAY_SIZE(xv_attributes);
    adapt->pAttributes = xv_attributes;
    adapt->nImages = ARRAY_SIZE(xv_images);
    adapt->pImages = xv_images;

    adapt->PutImage = omap_video_put;
    adapt->ReputImage = omap_video_reput;
    adapt->StopVideo = omap_video_stop;
    adapt->GetPortAttribute = omap_video_get_attribute;
    adapt->SetPortAttribute = omap_video_set_attribute;
    adapt->QueryBestSize = omap_video_query_best_size;
    adapt->QueryImageAttributes = omap_video_query_attributes;
    adapt->ClipNotify = omap_video_clip_notify;

    omaps->xv_adaptors = adapt;

    xv_colorkey = MAKE_ATOM("XV_COLORKEY");
    xv_vsync = MAKE_ATOM("XV_OMAP_VSYNC");
    _omap_video_overlay = MAKE_ATOM("_OMAP_VIDEO_OVERLAY");

    return adapt;
}

#ifdef XV
/**
 * Set up everything we need for Xv.
 */
Bool omap_video_init(struct omap_screen_info *omaps)
{
    KdVideoAdaptorPtr *adaptors, *new_adaptors = NULL;
    KdVideoAdaptorPtr new_adaptor = NULL;
    int num_adaptors = 0;

    omaps->video_region = REGION_CREATE(omaps->screen->pScreen, NullBox, 0);
    if (!omaps->video_region)
        FatalError("omapVideoInit: couldn't create video region\n");

    omaps->xv_adaptors = NULL;

    num_adaptors = KdXVListGenericAdaptors(omaps->screen, &adaptors);
    new_adaptor = omap_video_setup_adaptors(omaps);

    if (!new_adaptor)
        return FALSE;

    if (!num_adaptors) {
        num_adaptors = 1;
        adaptors = &new_adaptor;
    }
    else {
        new_adaptors = xcalloc(num_adaptors + 1, sizeof(KdVideoAdaptorPtr *));
        if (!new_adaptors)
            return FALSE;

        memcpy(new_adaptors, adaptors, num_adaptors * sizeof(KdVideoAdaptorPtr));
        new_adaptors[num_adaptors] = new_adaptor;
        adaptors = new_adaptors;
        num_adaptors++;
    }

    KdXVScreenInit(omaps->screen->pScreen, adaptors, num_adaptors);

    /* Hook drawable destruction, so we can ignore them if they go away. */
    omaps->destroy_pixmap = omaps->screen->pScreen->DestroyPixmap;
    omaps->screen->pScreen->DestroyPixmap = destroy_pixmap_hook;
    omaps->destroy_window = omaps->screen->pScreen->DestroyWindow;
    omaps->screen->pScreen->DestroyWindow = destroy_window_hook;

    if (new_adaptors)
        xfree(new_adaptors);

    return TRUE;
}

/**
 * Shut down Xv, used on regeneration.
 */
void omap_video_fini(struct omap_screen_info *omaps)
{
    REGION_DESTROY(omaps->screen->pScreen, omaps->video_region);

    omaps->screen->pScreen->DestroyPixmap = omaps->destroy_pixmap;
    omaps->screen->pScreen->DestroyWindow = omaps->destroy_window;
}

#else

Bool omap_video_init(struct omap_screen_info *omaps)
{
    omaps->video_region = NULL;
    return TRUE;
}

void omap_video_init(struct omap_screen_info *omaps)
{
}

#endif
