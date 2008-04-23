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

#ifndef _OMAP_H_
#define _OMAP_H_

#ifdef HAVE_KDRIVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include <stdint.h>
#include <scrnintstr.h>

#include "cursor.h"
#include "cursorstr.h"
#include "picturestr.h"
#include "scrnintstr.h"
#include "kdrive.h"

#ifdef XV
#include "kxv.h"
#endif

#include "omapfb.h"

#define ENTER() DebugF("Enter %s\n", __FUNCTION__)
#define LEAVE() DebugF("Leave %s\n", __FUNCTION__)

/* Minimum time between framebuffer updates, in ms. */
#define OMAP_UPDATE_TIME 50

/* Is our video dispc or external? */
#define OMAP_EXT_NONE      0
#define OMAP_EXT_CANDIDATE (1 << 0)
#define OMAP_EXT_MIGRATED  (1 << 1) 
#define OMAP_EXT_BASE_MASK (OMAP_EXT_CANDIDATE | OMAP_EXT_MIGRATED)
#define OMAP_EXT_PENDING   (1 << 15)

#define OMAP_GET_EXT(x)    ((x)->ext_state & OMAP_EXT_BASE_MASK)

#define OMAP_DEFAULT_CKEY (0x007f << 5)
#define OMAP_DEFAULT_BG (0x0000)

struct omap_card_info;
struct omap_screen_info;
struct omap_plane_info;
struct omap_video_info;

#define get_omap_card_info(kd)        ((struct omap_card_info *) ((kd)->card->driver))
#define get_omap_screen_info(kd)      ((struct omap_screen_info *) ((kd)->screen->driver))
#define get_omap_video_info(omaps, n) ((struct omap_video_info *) ((omaps)->xv_adaptors->pPortPrivates[n].ptr))

enum omap_plane_type {
    OMAP_PLANE_BASE,
    OMAP_PLANE_OVERLAY,
};

enum omap_plane_state {
    OMAP_STATE_STOPPED,
    OMAP_STATE_GRABBED,
    OMAP_STATE_ACTIVE,
};

struct omap_plane_info {
    /* Unique identifier. */
    int id;

    enum omap_plane_type type;

    /* Intrinsic properties of the plane. */
    char *filename;
    int fd;
    CARD8 *fb;
    int fb_size;
    int pitch, bpp;
    int max_width, max_height;
    unsigned long colors;

    /* Whether or not the plane is enabled.  If a plane is not active and
     * not dirty, it can be restarted without being reconfigured. */
    enum omap_plane_state state;

    /* Do we need to set the plane up again? */
    int dirty;

    /* Changeable; changing any of these requires setting the
     * dirty flag. */
    xRectangle src_area;
    xRectangle dst_area;
    enum omapfb_color_format format;

    /* OMAP_EXT_* flags. */
    int ext_state;

    /* Plane capabilities, from omapfb.h. */
    unsigned long caps;

    /* Colour key, in RGB565. */
    int colorkey;

    enum {
        OMAP_VSYNC_NONE,
        OMAP_VSYNC_TEAR,
        OMAP_VSYNC_FORCE,
    } vsync;

    /* Number of frames we've drawn, for profiling. */
    int frames;
    int frames_since;

    /* Pointer back to our base screen. */
    struct omap_screen_info *omaps;

    struct omap_plane_info *next;
};

struct omap_card_info {
    struct omap_plane_info *planes;

    /* The mode which was set at startup. */
    int orig_width, orig_height, orig_bpp;
};

struct omap_screen_info {
    struct omap_card_info *omapc;
    KdScreenInfo *screen;

    /* Number of updates kicked from the timer. */
    int updates;

    /* Timer gets disabled after 500ms of inactivity. */
    int empty_updates, timer_active;

    /* Should we avoid aggregating updates? */
    int individual_updates;

    /* Pointer to our base plane. */
    struct omap_plane_info *plane;

    /* The current damaged area. */
    BoxRec dirty_area;

    /* Have we enabled pixel doubling? */
    int pixel_doubled;

    /* Do we need to block UI updates? */
    int block_updates;

    /* Are we using a shadow framebuffer? */
    int shadowfb;

    /* Use for tracking damage for window updates. */
    PixmapPtr pixmap;
    DamagePtr damage;
    RegionPtr tmp_region;
    RegionPtr video_region;

    /* Watch drawable destruction for video planes. */
    DestroyPixmapProcPtr destroy_pixmap;
    DestroyWindowProcPtr destroy_window;

    int num_video_ports;
#ifdef XV
    KdVideoAdaptorPtr xv_adaptors;
#else
    void *xv_adaptors;
#endif
};

struct omap_video_info {
    /* General overlay information */
    RegionRec clip;
    struct omap_screen_info *omaps;

    /* Plane currently in use. */
    struct omap_plane_info *plane;

    /* Are we downscaling? */
    Bool hscale, vscale;

    /* Current FourCC being sent from the client. */
    int fourcc;

    /* Backing drawable. */
    DrawablePtr drawable;

    /* Visibility of the backing drawable. */
    unsigned int visibility;

    /* Timer to make sure we don't hang with updates blocked. */
    OsTimerPtr migration_timer;

    /* Timer to make sure we push at least one frame every 30sec. */
    OsTimerPtr frame_timer;

    int src_w, src_h;
    int dst_x, dst_y, dst_w, dst_h;
};


int omap_screen_resources_create(struct omap_screen_info *omaps);
void omap_screen_resources_destroy(struct omap_screen_info *omaps);
void omap_screen_update_all(struct omap_screen_info *omaps, int force_all);

struct omap_plane_info *omap_plane_create(struct omap_card_info *omapc,
                                          char *filename,
                                          enum omap_plane_type type);
void omap_plane_destroy(struct omap_plane_info *plane);
int omap_plane_enable(struct omap_plane_info *plane);
void omap_plane_disable(struct omap_plane_info *plane);
void omap_plane_flush(struct omap_plane_info *plane, xRectangle *area);

void omap_card_sync(struct omap_card_info *omapc);

int omap_video_init(struct omap_screen_info *omaps);
void omap_video_fini(struct omap_screen_info *omaps);

/* Video format copying functions: *_packed will copy packed-to-packed;
 * *_planar will copy planar-to-packed, and *_yuv420 will copy
 * planar-to-YUV420 for Hailstorm.  *_copy_* will do a straight copy,
 * whereas *_copy_scale_* will apply software 'downscaling' (i.e.
 * point sampling). */
void omap_copy_packed(KdScreenInfo *screen, CARD8 *src, CARD8 *dst, int randr,
                 int srcPitch, int dstPitch, int srcW, int srcH, int top,
                 int left, int h, int w);
void omap_copy_planar(KdScreenInfo *screen, CARD8 *src, CARD8 *dst, int randr,
                 int srcPitch, int srcPitch2, int dstPitch, int srcW,
                 int srcH, int height, int top, int left, int h, int w,
                 int id);
void omap_copy_yuv420(KdScreenInfo *screen, struct omap_video_info *video_info,
                 CARD8 *srcb, CARD8 *dstb, int randr, int srcPitch,
                 int srcPitch2, int dstPitch, int srcW, int srcH, int top,
                 int left, int h, int w, int id);
void omap_copy_scale_planar(KdScreenInfo *screen,
                       struct omap_video_info *video_info, CARD8 *src,
                       CARD8 *dstb, int randr, int srcPitch, int srcPitch2,
                       int dstPitch, int srcW, int srcH, int height, int top,
                       int left, int h, int w, int id, int dstW, int dstH);
void omap_copy_scale_packed(KdScreenInfo *screen,
                       struct omap_video_info *video_info, CARD8 *src,
                       CARD8 *dst, int randr, int srcPitch, int dstPitch,
                       const int srcW, const int srcH, int top, int left,
                       int h, int w, const int dstW, const int dstH);

void omap_paint_ckey(DrawablePtr drawable, RegionPtr region, Pixel fg);


extern KdCardFuncs omap_funcs;

#endif /* _OMAP_H_ */
