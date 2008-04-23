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
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "kdrive.h"
#include "omap.h"

struct omap_plane_info *
omap_plane_create(struct omap_card_info *omapc, char *name,
                  enum omap_plane_type type)
{
    struct omap_plane_info *plane = NULL;
    struct omap_plane_info **prev = NULL;
    struct fb_var_screeninfo var;
    struct omapfb_caps caps;
    struct omapfb_mem_info mem_info;
    int fd;

    fd = open(name, O_RDWR);
    if (fd < 0) {
        DebugF("omapSetupPlane: couldn't open %s\n", name);
        return NULL;
    }

    /* Not omapfb. */
    if (ioctl(fd, OMAPFB_GET_CAPS, &caps) != 0) {
        DebugF("omapSetupPlane: called on non-omapfb\n");
        return NULL;
    }

    plane = xcalloc(1, sizeof(*plane));
    if (!plane) {
        ErrorF("omapSetupPlane: couldn't allocate plane\n");
        return NULL;
    }

    plane->type = type;

    if (type == OMAP_PLANE_BASE) {
        if (ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0) {
            ErrorF("omapSetupPlane: couldn't get fb info\n");
            goto bail;
        }

        omapc->orig_width = var.xres;
        omapc->orig_height = var.yres;
        omapc->orig_bpp = var.bits_per_pixel;
    }
    else {
        /* Planes start with their memory fully allocated, so free it when
         * we take control. */
        if (ioctl(fd, OMAPFB_QUERY_MEM, &mem_info) != 0) {
            ErrorF("omapSetupPlane: couldn't get mem info\n");
            goto bail;
        }
        if (mem_info.size) {
            mem_info.size = 0;
            if (ioctl(fd, OMAPFB_SETUP_MEM, &mem_info) != 0) {
                ErrorF("omapSetupPlane: couldn't free mem\n");
                goto bail;
            }
        }
    }

    if (caps.ctrl & OMAPFB_CAPS_TEARSYNC)
        plane->vsync = OMAP_VSYNC_TEAR;
    else
        plane->vsync = OMAP_VSYNC_NONE;

    plane->fd = fd;
    plane->filename = name;
    plane->max_width = omapc->orig_width;
    plane->max_height = omapc->orig_height;
    plane->state = OMAP_STATE_STOPPED;
    plane->caps = caps.ctrl;
    plane->colors = caps.plane_color | caps.wnd_color;
    plane->next = NULL;
    plane->id = 0;

    for (prev = &omapc->planes; *prev; prev = &(*prev)->next) {
        if (plane->type == OMAP_PLANE_BASE && (*prev)->type == OMAP_PLANE_BASE)
            FatalError("omapSetupPlane: attempting to add two base planes\n");
        plane->id++;
    }

    *prev = plane;

    return plane;

bail:
    xfree(plane);
    return NULL;
}

void
omap_plane_destroy(struct omap_plane_info *plane)
{
    if (plane->state >= OMAP_STATE_ACTIVE)
        omap_plane_disable(plane);

    close(plane->fd);
    xfree(plane);
}

void
omap_plane_flush(struct omap_plane_info *plane, xRectangle *area)
{
    struct omapfb_update_window update_window = { 0, };
    int is_base = (plane->type == OMAP_PLANE_BASE);
    int is_migrated = (!is_base && OMAP_GET_EXT(plane) == OMAP_EXT_MIGRATED);

    if (is_base) {
        update_window.format = plane->format;

        if (!area) {
            area = &plane->src_area;
            DebugF("forcing update on (%d, %d) to (%d, %d)\n", area->x,
                   area->y, area->width, area->height);
        }

        /* FIXME Support different formats in the x/width alignment. */
        update_window.x = area->x & ~1;
        update_window.y = area->y & ~1;
        update_window.out_x = update_window.x;
        update_window.out_y = update_window.y;
        update_window.width = (area->width + 1) & ~1;
        if (area->x & 1)
            update_window.width += 2;
        update_window.height = (area->height + 1) & ~1;
        if (area->y & 1)
            update_window.height += 2;
        update_window.out_width = update_window.width;
        update_window.out_height = update_window.height;

        /* If we're pixel-doubling, clip to the first half of the screen. */
        if (plane->omaps->pixel_doubled) {
            if (update_window.x >= plane->omaps->screen->width / 2 ||
                update_window.y >= plane->omaps->screen->height / 2)
                return;

            if (update_window.x + update_window.width >
                plane->omaps->screen->width / 2)
                update_window.width = (plane->omaps->screen->width / 2) -
                                      update_window.x;
            if (update_window.y + update_window.height >
                plane->omaps->screen->height / 2)
                update_window.height = (plane->omaps->screen->height / 2) -
                                       update_window.y;

            update_window.format |= OMAPFB_FORMAT_FLAG_DOUBLE;
        }
    }
    else {
        update_window.x = 0;
        update_window.y = 0;
        update_window.out_x = 0;
        update_window.out_y = 0;
        update_window.out_width = plane->dst_area.width;
        update_window.out_height = plane->dst_area.height;

        if (is_migrated) {
            update_window.format = OMAPFB_COLOR_YUV420;
            update_window.width = plane->src_area.width;
            update_window.height = plane->src_area.height;
        }
        else {
            update_window.format = plane->omaps->plane->format;
            update_window.width = plane->dst_area.width;
            update_window.height = plane->dst_area.height;
        }
    }

    if (plane->vsync == OMAP_VSYNC_TEAR)
        update_window.format |= OMAPFB_FORMAT_FLAG_TEARSYNC;
    else if (plane->vsync == OMAP_VSYNC_FORCE)
        update_window.format |= OMAPFB_FORMAT_FLAG_FORCE_VSYNC;

#ifdef PROFILE_ME_HARDER
    plane->frames++;
#endif

#if 0
    DebugF("omapFlushDamage (plane %d): updating (%d, %d, %d, %d), to (%d, %d, %d, %d) %s\n",
           plane->id, update_window.x, update_window.y,
           update_window.width, update_window.height,
           update_window.out_x, update_window.out_y,
           update_window.out_width, update_window.out_height,
           (update_window.format == OMAPFB_COLOR_YUV420) ? "yuv420" :
            ((update_window.format == OMAPFB_COLOR_RGB565) ? "rgb565" :
             "yuy2?"));
#endif

    if (ioctl(plane->fd, OMAPFB_UPDATE_WINDOW, &update_window) != 0) {
        DebugF("omapFlushDamage: window update failed from (%d, %d) to "
               "(%d, %d), at (%d, %d), offset (%d, %d)\n",
               update_window.width, update_window.height,
               update_window.out_width, update_window.out_height,
               update_window.x, update_window.y,
               update_window.out_x, update_window.out_y);
    }
}

static int
calc_mem_size(struct omap_plane_info *plane_info)
{
    if (plane_info->type == OMAP_PLANE_OVERLAY &&
        OMAP_GET_EXT(plane_info) == OMAP_EXT_MIGRATED)
        return plane_info->src_area.width * plane_info->src_area.height * 2;
    else
        return plane_info->dst_area.width * plane_info->dst_area.height * 2;
}

static int
realloc_plane(struct omap_plane_info *plane_info)
{
    struct omapfb_mem_info mem_info;

#if 0
    /* First try to allocate in SRAM, then fall back to SDRAM. */
    mem_info.type = OMAPFB_MEMTYPE_SRAM;
#else
    /* Joyous kernel bug doesn't let us ... */
    mem_info.type = OMAPFB_MEMTYPE_SDRAM;
#endif
    mem_info.size = calc_mem_size(plane_info);

    if (ioctl(plane_info->fd, OMAPFB_SETUP_MEM, &mem_info) != 0) {
#if 0
        mem_info.type = OMAPFB_MEMTYPE_SDRAM;

        if (ioctl(plane_info->fd, OMAPFB_SETUP_MEM, &mem_info) != 0)
#endif
            return 0;
    }

    return 1;
}

int
omap_plane_enable(struct omap_plane_info *plane_info)
{
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    struct omapfb_color_key colorkey;
    struct omapfb_plane_info kplane_info;
    struct omapfb_mem_info mem_info;

    const int bpp = plane_info->omaps->screen->fb[0].bitsPerPixel;
    const int is_base = (plane_info->type == OMAP_PLANE_BASE);
    const int is_migrated = (OMAP_GET_EXT(plane_info) == OMAP_EXT_MIGRATED);
    int mem_allocated = 0;

    omap_card_sync(plane_info->omaps->omapc);

    if (!is_base) {
        if (ioctl(plane_info->fd, OMAPFB_QUERY_MEM, &mem_info) != 0) {
            ErrorF("omapPlaneEnable: couldn't get mem info\n");
            return 0;
        }

        if (calc_mem_size(plane_info) > mem_info.size) {
            if (!realloc_plane(plane_info)) {
                ErrorF("omapPlaneEnable: couldn't grow mem\n");
                return 0;
            }
            mem_allocated = 1;
        }
    }

    if (ioctl(plane_info->fd, FBIOGET_VSCREENINFO, &var) != 0) {
        ErrorF("omapPlaneEnable: couldn't get var info\n");
        return 0;
    }

    var.xres = plane_info->src_area.width;
    var.yres = plane_info->src_area.height;
    var.xres_virtual = 0;
    var.yres_virtual = 0;
    var.xoffset = 0;
    var.yoffset = 0;
    var.rotate = 0;
    var.grayscale = 0;
    var.activate = FB_ACTIVATE_NOW;

    if (is_migrated) {
        var.xres &= ~3;
        var.height &= ~1;
    }

    if (is_base || is_migrated) {
        var.bits_per_pixel = bpp;
        var.nonstd = plane_info->omaps->plane->format;
    }
    else {
        var.bits_per_pixel = 0;
        var.nonstd = plane_info->format;
    }

    if (ioctl(plane_info->fd, FBIOPUT_VSCREENINFO, &var) != 0) {
        ErrorF("omapPlaneEnable: couldn't update var info\n");
        return 0;
    }

    if (!is_base) {
        if (!mem_allocated) {
            if (!realloc_plane(plane_info)) {
                ErrorF("omapPlaneEnable: couldn't shrink mem\n");
                return 0;
            }
            mem_allocated = 1;
        }

        if (ioctl(plane_info->fd, OMAPFB_GET_COLOR_KEY, &colorkey) != 0) {
            ErrorF("omapPlaneEnable: couldn't get colorkey\n");
            goto bail;
        }
        colorkey.trans_key = plane_info->colorkey;
        colorkey.key_type = OMAPFB_COLOR_KEY_GFX_DST;
        colorkey.channel_out = OMAPFB_CHANNEL_OUT_LCD;
        colorkey.background = OMAP_DEFAULT_BG;
        if (ioctl(plane_info->fd, OMAPFB_SET_COLOR_KEY, &colorkey) != 0) {
            ErrorF("omapPlaneEnable: couldn't set colorkey\n");
            goto bail;
        }

        if (ioctl(plane_info->fd, OMAPFB_QUERY_PLANE, &kplane_info) != 0) {
            ErrorF("omapPlaneEnable: couldn't get plane info\n");
            return 0;
        }

        kplane_info.pos_x = plane_info->dst_area.x;
        kplane_info.pos_y = plane_info->dst_area.y;
        kplane_info.enabled = 1;
        if (is_migrated) {
            kplane_info.out_width = plane_info->src_area.width;
            kplane_info.out_height = plane_info->src_area.height;
        }
        else {
            /* We don't need to align, since we can just send larger regions
             * to compensate, given dispc converts to the same format as the
             * base plane. */
            kplane_info.out_width = plane_info->dst_area.width;
            kplane_info.out_height = plane_info->dst_area.height;
        }

        if (ioctl(plane_info->fd, OMAPFB_SETUP_PLANE, &kplane_info) != 0) {
            ErrorF("omapPlaneEnable: couldn't setup plane\n");
            return 0;
        }
    }

    if (ioctl(plane_info->fd, FBIOGET_FSCREENINFO, &fix) != 0) {
        ErrorF("omapPlaneEnable: couldn't get fix info\n");
        goto bail;
    }
    plane_info->fb = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE,
                          MAP_SHARED, plane_info->fd, 0L);
    if (!plane_info->fb) {
        ErrorF("omapPlaneEnable: mmaping overlay failed\n");
        goto bail;
    }
    plane_info->fb += fix.smem_start % getpagesize();
    plane_info->fb_size = fix.smem_len;
    plane_info->pitch = fix.line_length;
    plane_info->fb_size = fix.smem_len;

    DebugF("omapPlaneEnable: enabled plane %d\n", plane_info->id);
    DebugF("    (%d, %d) to (%d, %d) at (%d, %d)\n", var.xres, var.yres,
           kplane_info.out_width, kplane_info.out_height,
           kplane_info.pos_x, kplane_info.pos_y);

    plane_info->dirty = 0;

    omap_card_sync(plane_info->omaps->omapc);

    return 1;

bail:
    kplane_info.enabled = 0;
    ioctl(plane_info->fd, OMAPFB_SETUP_PLANE, &kplane_info);
    return 0;
}

void
omap_plane_disable(struct omap_plane_info *plane_info)
{
    struct omapfb_plane_info kplane_info;
    struct omapfb_mem_info mem_info;

    if (plane_info->type == OMAP_PLANE_BASE)
        FatalError("attempting to disable base plane\n");

    omap_card_sync(plane_info->omaps->omapc);

    if (plane_info->fb)
        munmap(plane_info->fb, plane_info->fb_size);

    plane_info->fb = NULL;
    plane_info->fb_size = 0;
    plane_info->pitch = 0;

    if (ioctl(plane_info->fd, OMAPFB_QUERY_PLANE, &kplane_info) != 0) {
        ErrorF("omapPlaneDisable: couldn't get plane info\n");
        goto bail;
    }
    kplane_info.enabled = 0;
    if (ioctl(plane_info->fd, OMAPFB_SETUP_PLANE, &kplane_info) != 0) {
        ErrorF("omapPlaneDisable: couldn't disable plane\n");
        goto bail;
    }

    if (plane_info->type == OMAP_PLANE_OVERLAY) {
        /* Deallocate our plane. */
        if (ioctl(plane_info->fd, OMAPFB_QUERY_MEM, &mem_info) != 0) {
            ErrorF("omapPlaneDisable: couldn't get mem info\n");
            goto bail;
        }
        mem_info.size = 0;
        if (ioctl(plane_info->fd, OMAPFB_SETUP_MEM, &mem_info) != 0) {
            ErrorF("omapPlaneDisable: couldn't set mem info\n");
            goto bail;
        }
    }

    DebugF("omapPlaneDisable: disabled plane %d\n", plane_info->id);

bail:
    /* Give it a chance to take. */
    omap_card_sync(plane_info->omaps->omapc);
}
