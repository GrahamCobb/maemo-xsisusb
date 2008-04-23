/*
 * Copyright © 2006 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#else
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#endif

#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "xf86.h"
#include "xf86DDC.h"
#include "xf86Crtc.h"
#include "xf86Modes.h"
#include "xf86RandR12.h"
#include "X11/extensions/render.h"
#define DPMS_SERVER
#include "X11/extensions/dpms.h"
#include "X11/Xatom.h"
#ifdef RENDER
#include "picturestr.h"
#endif

/*
 * Initialize xf86CrtcConfig structure
 */

int xf86CrtcConfigPrivateIndex = -1;

void
xf86CrtcConfigInit (ScrnInfoPtr scrn,
		    const xf86CrtcConfigFuncsRec *funcs)
{
    xf86CrtcConfigPtr	config;
    
    if (xf86CrtcConfigPrivateIndex == -1)
	xf86CrtcConfigPrivateIndex = xf86AllocateScrnInfoPrivateIndex();
    config = xnfcalloc (1, sizeof (xf86CrtcConfigRec));

    config->funcs = funcs;

    scrn->privates[xf86CrtcConfigPrivateIndex].ptr = config;
}
 
void
xf86CrtcSetSizeRange (ScrnInfoPtr scrn,
		      int minWidth, int minHeight,
		      int maxWidth, int maxHeight)
{
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(scrn);

    config->minWidth = minWidth;
    config->minHeight = minHeight;
    config->maxWidth = maxWidth;
    config->maxHeight = maxHeight;
}

/*
 * Crtc functions
 */
xf86CrtcPtr
xf86CrtcCreate (ScrnInfoPtr		scrn,
		const xf86CrtcFuncsRec	*funcs)
{
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    xf86CrtcPtr		crtc, *crtcs;

    crtc = xcalloc (sizeof (xf86CrtcRec), 1);
    if (!crtc)
	return NULL;
    crtc->scrn = scrn;
    crtc->funcs = funcs;
#ifdef RANDR_12_INTERFACE
    crtc->randr_crtc = NULL;
#endif
    crtc->rotation = RR_Rotate_0;
    crtc->desiredRotation = RR_Rotate_0;
    if (xf86_config->crtc)
	crtcs = xrealloc (xf86_config->crtc,
			  (xf86_config->num_crtc + 1) * sizeof (xf86CrtcPtr));
    else
	crtcs = xalloc ((xf86_config->num_crtc + 1) * sizeof (xf86CrtcPtr));
    if (!crtcs)
    {
	xfree (crtc);
	return NULL;
    }
    xf86_config->crtc = crtcs;
    xf86_config->crtc[xf86_config->num_crtc++] = crtc;
    return crtc;
}

void
xf86CrtcDestroy (xf86CrtcPtr crtc)
{
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
    int			c;
    
    (*crtc->funcs->destroy) (crtc);
    for (c = 0; c < xf86_config->num_crtc; c++)
	if (xf86_config->crtc[c] == crtc)
	{
	    memmove (&xf86_config->crtc[c],
		     &xf86_config->crtc[c+1],
		     xf86_config->num_crtc - (c + 1));
	    xf86_config->num_crtc--;
	    break;
	}
    xfree (crtc);
}


/**
 * Return whether any outputs are connected to the specified pipe
 */

Bool
xf86CrtcInUse (xf86CrtcPtr crtc)
{
    ScrnInfoPtr		pScrn = crtc->scrn;
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int			o;
    
    for (o = 0; o < xf86_config->num_output; o++)
	if (xf86_config->output[o]->crtc == crtc)
	    return TRUE;
    return FALSE;
}

void
xf86CrtcSetScreenSubpixelOrder (ScreenPtr pScreen)
{
#ifdef RENDER
    int			subpixel_order = SubPixelUnknown;
    Bool		has_none = FALSE;
    ScrnInfoPtr		scrn = xf86Screens[pScreen->myNum];
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    int			c, o;

    for (c = 0; c < xf86_config->num_crtc; c++)
    {
	xf86CrtcPtr crtc = xf86_config->crtc[c];
	
	for (o = 0; o < xf86_config->num_output; o++)
	{
	    xf86OutputPtr   output = xf86_config->output[o];

	    if (output->crtc == crtc)
	    {
		switch (output->subpixel_order) {
		case SubPixelNone:
		    has_none = TRUE;
		    break;
		case SubPixelUnknown:
		    break;
		default:
		    subpixel_order = output->subpixel_order;
		    break;
		}
	    }
	    if (subpixel_order != SubPixelUnknown)
		break;
	}
	if (subpixel_order != SubPixelUnknown)
	{
	    static const int circle[4] = {
		SubPixelHorizontalRGB,
		SubPixelVerticalRGB,
		SubPixelHorizontalBGR,
		SubPixelVerticalBGR,
	    };
	    int	rotate;
	    int c;
	    for (rotate = 0; rotate < 4; rotate++)
		if (crtc->rotation & (1 << rotate))
		    break;
	    for (c = 0; c < 4; c++)
		if (circle[c] == subpixel_order)
		    break;
	    c = (c + rotate) & 0x3;
	    if ((crtc->rotation & RR_Reflect_X) && !(c & 1))
		c ^= 2;
	    if ((crtc->rotation & RR_Reflect_Y) && (c & 1))
		c ^= 2;
	    subpixel_order = circle[c];
	    break;
	}
    }
    if (subpixel_order == SubPixelUnknown && has_none)
	subpixel_order = SubPixelNone;
    PictureSetSubpixelOrder (pScreen, subpixel_order);
#endif
}

/**
 * Sets the given video mode on the given crtc
 */
Bool
xf86CrtcSetMode (xf86CrtcPtr crtc, DisplayModePtr mode, Rotation rotation,
		 int x, int y)
{
    ScrnInfoPtr		scrn = crtc->scrn;
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    int			i;
    Bool		ret = FALSE;
    Bool		didLock = FALSE;
    DisplayModePtr	adjusted_mode;
    DisplayModeRec	saved_mode;
    int			saved_x, saved_y;
    Rotation		saved_rotation;

    adjusted_mode = xf86DuplicateMode(mode);

    crtc->enabled = xf86CrtcInUse (crtc);
    
    if (!crtc->enabled)
    {
	/* XXX disable crtc? */
	return TRUE;
    }

    didLock = crtc->funcs->lock (crtc);

    saved_mode = crtc->mode;
    saved_x = crtc->x;
    saved_y = crtc->y;
    saved_rotation = crtc->rotation;
    /* Update crtc values up front so the driver can rely on them for mode
     * setting.
     */
    crtc->mode = *mode;
    crtc->x = x;
    crtc->y = y;
    crtc->rotation = rotation;

    /* XXX short-circuit changes to base location only */
    
    /* Pass our mode to the outputs and the CRTC to give them a chance to
     * adjust it according to limitations or output properties, and also
     * a chance to reject the mode entirely.
     */
    for (i = 0; i < xf86_config->num_output; i++) {
	xf86OutputPtr output = xf86_config->output[i];

	if (output->crtc != crtc)
	    continue;

	if (!output->funcs->mode_fixup(output, mode, adjusted_mode)) {
	    goto done;
	}
    }

    if (!crtc->funcs->mode_fixup(crtc, mode, adjusted_mode)) {
	goto done;
    }

    if (!xf86CrtcRotate (crtc, mode, rotation)) {
	goto done;
    }

    /* Prepare the outputs and CRTCs before setting the mode. */
    for (i = 0; i < xf86_config->num_output; i++) {
	xf86OutputPtr output = xf86_config->output[i];

	if (output->crtc != crtc)
	    continue;

	/* Disable the output as the first thing we do. */
	output->funcs->prepare(output);
    }

    crtc->funcs->prepare(crtc);

    /* Set up the DPLL and any output state that needs to adjust or depend
     * on the DPLL.
     */
    crtc->funcs->mode_set(crtc, mode, adjusted_mode, x, y);
    for (i = 0; i < xf86_config->num_output; i++) 
    {
	xf86OutputPtr output = xf86_config->output[i];
	if (output->crtc == crtc)
	    output->funcs->mode_set(output, mode, adjusted_mode);
    }

    /* Now, enable the clocks, plane, pipe, and outputs that we set up. */
    crtc->funcs->commit(crtc);
    for (i = 0; i < xf86_config->num_output; i++) 
    {
	xf86OutputPtr output = xf86_config->output[i];
	if (output->crtc == crtc)
	    output->funcs->commit(output);
    }

    /* XXX free adjustedmode */
    ret = TRUE;
    if (scrn->pScreen)
	xf86CrtcSetScreenSubpixelOrder (scrn->pScreen);

done:
    if (!ret) {
	crtc->x = saved_x;
	crtc->y = saved_y;
	crtc->rotation = saved_rotation;
	crtc->mode = saved_mode;
    }

    if (didLock)
	crtc->funcs->unlock (crtc);

    return ret;
}

/*
 * Output functions
 */

extern XF86ConfigPtr xf86configptr;

typedef enum {
    OPTION_PREFERRED_MODE,
    OPTION_POSITION,
    OPTION_BELOW,
    OPTION_RIGHT_OF,
    OPTION_ABOVE,
    OPTION_LEFT_OF,
    OPTION_ENABLE,
    OPTION_DISABLE,
    OPTION_MIN_CLOCK,
    OPTION_MAX_CLOCK,
    OPTION_IGNORE,
    OPTION_ROTATE,
} OutputOpts;

static OptionInfoRec xf86OutputOptions[] = {
    {OPTION_PREFERRED_MODE, "PreferredMode",	OPTV_STRING,  {0}, FALSE },
    {OPTION_POSITION,	    "Position",		OPTV_STRING,  {0}, FALSE },
    {OPTION_BELOW,	    "Below",		OPTV_STRING,  {0}, FALSE },
    {OPTION_RIGHT_OF,	    "RightOf",		OPTV_STRING,  {0}, FALSE },
    {OPTION_ABOVE,	    "Above",		OPTV_STRING,  {0}, FALSE },
    {OPTION_LEFT_OF,	    "LeftOf",		OPTV_STRING,  {0}, FALSE },
    {OPTION_ENABLE,	    "Enable",		OPTV_BOOLEAN, {0}, FALSE },
    {OPTION_DISABLE,	    "Disable",		OPTV_BOOLEAN, {0}, FALSE },
    {OPTION_MIN_CLOCK,	    "MinClock",		OPTV_FREQ,    {0}, FALSE },
    {OPTION_MAX_CLOCK,	    "MaxClock",		OPTV_FREQ,    {0}, FALSE },
    {OPTION_IGNORE,	    "Ignore",		OPTV_BOOLEAN, {0}, FALSE },
    {OPTION_ROTATE,	    "Rotate",		OPTV_STRING,  {0}, FALSE },
    {-1,		    NULL,		OPTV_NONE,    {0}, FALSE },
};

static void
xf86OutputSetMonitor (xf86OutputPtr output)
{
    char    *option_name;
    static const char monitor_prefix[] = "monitor-";
    char    *monitor;

    if (!output->name)
	return;

    if (output->options)
	xfree (output->options);

    output->options = xnfalloc (sizeof (xf86OutputOptions));
    memcpy (output->options, xf86OutputOptions, sizeof (xf86OutputOptions));
    
    option_name = xnfalloc (strlen (monitor_prefix) +
			    strlen (output->name) + 1);
    strcpy (option_name, monitor_prefix);
    strcat (option_name, output->name);
    monitor = xf86findOptionValue (output->scrn->options, option_name);
    if (!monitor)
	monitor = output->name;
    else
	xf86MarkOptionUsedByName (output->scrn->options, option_name);
    xfree (option_name);
    output->conf_monitor = xf86findMonitor (monitor,
					    xf86configptr->conf_monitor_lst);
    if (output->conf_monitor)
	xf86ProcessOptions (output->scrn->scrnIndex,
			    output->conf_monitor->mon_option_lst,
			    output->options);
}

static Bool
xf86OutputEnabled (xf86OutputPtr    output)
{
    /* Check to see if this output was disabled in the config file */
    if (xf86ReturnOptValBool (output->options, OPTION_ENABLE, TRUE) == FALSE ||
	xf86ReturnOptValBool (output->options, OPTION_DISABLE, FALSE) == TRUE)
    {
	return FALSE;
    }
    return TRUE;
}

static Bool
xf86OutputIgnored (xf86OutputPtr    output)
{
    return xf86ReturnOptValBool (output->options, OPTION_IGNORE, FALSE);
}

static char *direction[4] = {
    "normal", 
    "left", 
    "inverted", 
    "right"
};

static Rotation
xf86OutputInitialRotation (xf86OutputPtr output)
{
    char    *rotate_name = xf86GetOptValString (output->options, 
						OPTION_ROTATE);
    int	    i;

    if (!rotate_name)
	return RR_Rotate_0;
    
    for (i = 0; i < 4; i++)
	if (xf86nameCompare (direction[i], rotate_name) == 0)
	    return (1 << i);
    return RR_Rotate_0;
}

xf86OutputPtr
xf86OutputCreate (ScrnInfoPtr		    scrn,
		  const xf86OutputFuncsRec *funcs,
		  const char		    *name)
{
    xf86OutputPtr	output, *outputs;
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    int			len;

    if (name)
	len = strlen (name) + 1;
    else
	len = 0;

    output = xcalloc (sizeof (xf86OutputRec) + len, 1);
    if (!output)
	return NULL;
    output->scrn = scrn;
    output->funcs = funcs;
    if (name)
    {
	output->name = (char *) (output + 1);
	strcpy (output->name, name);
    }
    output->subpixel_order = SubPixelUnknown;
#ifdef RANDR_12_INTERFACE
    output->randr_output = NULL;
#endif
    if (name)
    {
	xf86OutputSetMonitor (output);
	if (xf86OutputIgnored (output))
	{
	    xfree (output);
	    return FALSE;
	}
    }
    
    
    if (xf86_config->output)
	outputs = xrealloc (xf86_config->output,
			  (xf86_config->num_output + 1) * sizeof (xf86OutputPtr));
    else
	outputs = xalloc ((xf86_config->num_output + 1) * sizeof (xf86OutputPtr));
    if (!outputs)
    {
	xfree (output);
	return NULL;
    }
    
    xf86_config->output = outputs;
    xf86_config->output[xf86_config->num_output++] = output;
    
    return output;
}

Bool
xf86OutputRename (xf86OutputPtr output, const char *name)
{
    int	    len = strlen(name) + 1;
    char    *newname = xalloc (len);
    
    if (!newname)
	return FALSE;	/* so sorry... */
    
    strcpy (newname, name);
    if (output->name && output->name != (char *) (output + 1))
	xfree (output->name);
    output->name = newname;
    xf86OutputSetMonitor (output);
    if (xf86OutputIgnored (output))
	return FALSE;
    return TRUE;
}

void
xf86OutputDestroy (xf86OutputPtr output)
{
    ScrnInfoPtr		scrn = output->scrn;
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    int			o;
    
    (*output->funcs->destroy) (output);
    while (output->probed_modes)
	xf86DeleteMode (&output->probed_modes, output->probed_modes);
    for (o = 0; o < xf86_config->num_output; o++)
	if (xf86_config->output[o] == output)
	{
	    memmove (&xf86_config->output[o],
		     &xf86_config->output[o+1],
		     xf86_config->num_output - (o + 1));
	    xf86_config->num_output--;
	    break;
	}
    if (output->name && output->name != (char *) (output + 1))
	xfree (output->name);
    xfree (output);
}

/*
 * Called during CreateScreenResources to hook up RandR
 */
static Bool
xf86CrtcCreateScreenResources (ScreenPtr screen)
{
    ScrnInfoPtr		scrn = xf86Screens[screen->myNum];
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(scrn);

    screen->CreateScreenResources = config->CreateScreenResources;
    
    if (!(*screen->CreateScreenResources)(screen))
	return FALSE;

    if (!xf86RandR12CreateScreenResources (screen))
	return FALSE;

    return TRUE;
}

/*
 * Called at ScreenInit time to set up
 */
Bool
xf86CrtcScreenInit (ScreenPtr screen)
{
    ScrnInfoPtr		scrn = xf86Screens[screen->myNum];
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(scrn);
    int			c;

    /* Rotation */
    xf86DrvMsg(scrn->scrnIndex, X_INFO, "RandR 1.2 enabled, ignore the following RandR disabled message.\n");
    xf86DisableRandR(); /* Disable old RandR extension support */
    xf86RandR12Init (screen);

    /* support all rotations if every crtc has the shadow alloc funcs */
    for (c = 0; c < config->num_crtc; c++)
    {
	xf86CrtcPtr crtc = config->crtc[c];
	if (!crtc->funcs->shadow_allocate || !crtc->funcs->shadow_create)
	    break;
    }
    if (c == config->num_crtc)
	xf86RandR12SetRotations (screen, RR_Rotate_0 | RR_Rotate_90 |
				 RR_Rotate_180 | RR_Rotate_270);
    else
	xf86RandR12SetRotations (screen, RR_Rotate_0);
    
    /* Wrap CreateScreenResources so we can initialize the RandR code */
    config->CreateScreenResources = screen->CreateScreenResources;
    screen->CreateScreenResources = xf86CrtcCreateScreenResources;
    return TRUE;
}

static DisplayModePtr
xf86DefaultMode (xf86OutputPtr output, int width, int height)
{
    DisplayModePtr  target_mode = NULL;
    DisplayModePtr  mode;
    int		    target_diff = 0;
    int		    target_preferred = 0;
    int		    mm_height;
    
    mm_height = output->mm_height;
    if (!mm_height)
	mm_height = 203;	/* 768 pixels at 96dpi */
    /*
     * Pick a mode closest to 96dpi 
     */
    for (mode = output->probed_modes; mode; mode = mode->next)
    {
	int	    dpi;
	int	    preferred = (mode->type & M_T_PREFERRED) != 0;
	int	    diff;

	if (xf86ModeWidth (mode, output->initial_rotation) > width ||
	    xf86ModeHeight (mode, output->initial_rotation) > height)
	    continue;
	
	/* yes, use VDisplay here, not xf86ModeHeight */
	dpi = (mode->VDisplay * 254) / (mm_height * 10);
	diff = dpi - 96;
	diff = diff < 0 ? -diff : diff;
	if (target_mode == NULL || (preferred > target_preferred) ||
	    (preferred == target_preferred && diff < target_diff))
	{
	    target_mode = mode;
	    target_diff = diff;
	    target_preferred = preferred;
	}
    }
    return target_mode;
}

static DisplayModePtr
xf86ClosestMode (xf86OutputPtr output, 
		 DisplayModePtr match, Rotation match_rotation,
		 int width, int height)
{
    DisplayModePtr  target_mode = NULL;
    DisplayModePtr  mode;
    int		    target_diff = 0;
    
    /*
     * Pick a mode closest to the specified mode
     */
    for (mode = output->probed_modes; mode; mode = mode->next)
    {
	int	    dx, dy;
	int	    diff;

	if (xf86ModeWidth (mode, output->initial_rotation) > width ||
	    xf86ModeHeight (mode, output->initial_rotation) > height)
	    continue;
	
	/* exact matches are preferred */
	if (output->initial_rotation == match_rotation &&
	    xf86ModesEqual (mode, match))
	    return mode;
	
	dx = xf86ModeWidth (match, match_rotation) - xf86ModeWidth (mode, output->initial_rotation);
	dy = xf86ModeHeight (match, match_rotation) - xf86ModeHeight (mode, output->initial_rotation);
	diff = dx * dx + dy * dy;
	if (target_mode == NULL || diff < target_diff)
	{
	    target_mode = mode;
	    target_diff = diff;
	}
    }
    return target_mode;
}

static Bool
xf86OutputHasPreferredMode (xf86OutputPtr output, int width, int height)
{
    DisplayModePtr  mode;

    for (mode = output->probed_modes; mode; mode = mode->next)
    {
	if (xf86ModeWidth (mode, output->initial_rotation) > width ||
	    xf86ModeHeight (mode, output->initial_rotation) > height)
	    continue;

	if (mode->type & M_T_PREFERRED)
	    return TRUE;
    }
    return FALSE;
}

static int
xf86PickCrtcs (ScrnInfoPtr	scrn,
	       xf86CrtcPtr	*best_crtcs,
	       DisplayModePtr	*modes,
	       int		n,
	       int		width,
	       int		height)
{
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(scrn);
    int		    c, o;
    xf86OutputPtr   output;
    xf86CrtcPtr	    crtc;
    xf86CrtcPtr	    *crtcs;
    xf86CrtcPtr	    best_crtc;
    int		    best_score;
    int		    score;
    int		    my_score;
    
    if (n == config->num_output)
	return 0;
    output = config->output[n];
    
    /*
     * Compute score with this output disabled
     */
    best_crtcs[n] = NULL;
    best_crtc = NULL;
    best_score = xf86PickCrtcs (scrn, best_crtcs, modes, n+1, width, height);
    if (modes[n] == NULL)
	return best_score;
    
    crtcs = xalloc (config->num_output * sizeof (xf86CrtcPtr));
    if (!crtcs)
	return best_score;

    my_score = 1;
    /* Score outputs that are known to be connected higher */
    if (output->status == XF86OutputStatusConnected)
	my_score++;
    /* Score outputs with preferred modes higher */
    if (xf86OutputHasPreferredMode (output, width, height))
	my_score++;
    /*
     * Select a crtc for this output and
     * then attempt to configure the remaining
     * outputs
     */
    for (c = 0; c < config->num_crtc; c++)
    {
	if ((output->possible_crtcs & (1 << c)) == 0)
	    continue;
	
	crtc = config->crtc[c];
	/*
	 * Check to see if some other output is
	 * using this crtc
	 */
	for (o = 0; o < n; o++)
	    if (best_crtcs[o] == crtc)
		break;
	if (o < n)
	{
	    /*
	     * If the two outputs desire the same mode,
	     * see if they can be cloned
	     */
	    if (xf86ModesEqual (modes[o], modes[n]) &&
		config->output[0]->initial_rotation == config->output[n]->initial_rotation &&
		config->output[o]->initial_x == config->output[n]->initial_x &&
		config->output[o]->initial_y == config->output[n]->initial_y)
	    {
		if ((output->possible_clones & (1 << o)) == 0)
		    continue;		/* nope, try next CRTC */
	    }
	    else
		continue;		/* different modes, can't clone */
	}
	crtcs[n] = crtc;
	memcpy (crtcs, best_crtcs, n * sizeof (xf86CrtcPtr));
	score = my_score + xf86PickCrtcs (scrn, crtcs, modes, n+1, width, height);
	if (score > best_score)
	{
	    best_crtc = crtc;
	    best_score = score;
	    memcpy (best_crtcs, crtcs, config->num_output * sizeof (xf86CrtcPtr));
	}
    }
    xfree (crtcs);
    return best_score;
}


/*
 * Compute the virtual size necessary to place all of the available
 * crtcs in the specified configuration.
 *
 * canGrow indicates that the driver can make the screen larger than its initial
 * configuration.  If FALSE, this function will enlarge the screen to include
 * the largest available mode.
 */

static void
xf86DefaultScreenLimits (ScrnInfoPtr scrn, int *widthp, int *heightp,
			 Bool canGrow)
{
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(scrn);
    int	    width = 0, height = 0;
    int	    o;
    int	    c;
    int	    s;

    for (c = 0; c < config->num_crtc; c++)
    {
	int	    crtc_width = 0, crtc_height = 0;
	xf86CrtcPtr crtc = config->crtc[c];

	if (crtc->enabled)
	{
	    crtc_width = crtc->x + xf86ModeWidth (&crtc->desiredMode, crtc->desiredRotation);
	    crtc_height = crtc->y + xf86ModeHeight (&crtc->desiredMode, crtc->desiredRotation);
	}
	if (!canGrow) {
	    for (o = 0; o < config->num_output; o++)
	    {
		xf86OutputPtr   output = config->output[o];

		for (s = 0; s < config->num_crtc; s++)
		    if (output->possible_crtcs & (1 << s))
		    {
			DisplayModePtr  mode;
			for (mode = output->probed_modes; mode; mode = mode->next)
			{
			    if (mode->HDisplay > crtc_width)
				crtc_width = mode->HDisplay;
			    if (mode->VDisplay > crtc_width)
				crtc_width = mode->VDisplay;
			    if (mode->VDisplay > crtc_height)
				crtc_height = mode->VDisplay;
			    if (mode->HDisplay > crtc_height)
				crtc_height = mode->HDisplay;
			}
		    }
	    }
	}
	if (crtc_width > width)
	    width = crtc_width;
	if (crtc_height > height)
	    height = crtc_height;
    }
    if (config->maxWidth && width > config->maxWidth) width = config->maxWidth;
    if (config->maxHeight && height > config->maxHeight) height = config->maxHeight;
    if (config->minWidth && width < config->minWidth) width = config->minWidth;
    if (config->minHeight && height < config->minHeight) height = config->minHeight;
    *widthp = width;
    *heightp = height;
}

#define POSITION_UNSET	-100000

static Bool
xf86InitialOutputPositions (ScrnInfoPtr scrn, DisplayModePtr *modes)
{
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(scrn);
    int			o;
    int			min_x, min_y;
    
    for (o = 0; o < config->num_output; o++)
    {
	xf86OutputPtr	output = config->output[o];

	output->initial_x = output->initial_y = POSITION_UNSET;
    }
    
    /*
     * Loop until all outputs are set
     */
    for (;;)
    {
	Bool	any_set = FALSE;
	Bool	keep_going = FALSE;

	for (o = 0; o < config->num_output; o++)	
	{
	    static const OutputOpts	relations[] = {
		OPTION_BELOW, OPTION_RIGHT_OF, OPTION_ABOVE, OPTION_LEFT_OF
	    };
	    xf86OutputPtr   output = config->output[o];
	    xf86OutputPtr   relative;
	    char	    *relative_name;
	    char	    *position;
	    OutputOpts	    relation;
	    int		    r;

	    if (output->initial_x != POSITION_UNSET)
		continue;
	    position = xf86GetOptValString (output->options,
					    OPTION_POSITION);
	    /*
	     * Absolute position wins
	     */
	    if (position)
	    {
		int		    x, y;
		if (sscanf (position, "%d %d", &x, &y) == 2)
		{
		    output->initial_x = x;
		    output->initial_y = y;
		}
		else
		{
		    xf86DrvMsg (scrn->scrnIndex, X_ERROR,
				"Output %s position not of form \"x y\"\n",
				output->name);
		    output->initial_x = output->initial_y = 0;
		}
		any_set = TRUE;
		continue;
	    }
	    /*
	     * Next comes relative positions
	     */
	    relation = 0;
	    relative_name = NULL;
	    for (r = 0; r < 4; r++)
	    {
		relation = relations[r];
		relative_name = xf86GetOptValString (output->options,
						     relation);
		if (relative_name)
		    break;
	    }
	    if (relative_name)
	    {
		int or;
		relative = NULL;
		for (or = 0; or < config->num_output; or++)
		{
		    xf86OutputPtr	out_rel = config->output[or];
		    XF86ConfMonitorPtr	rel_mon = out_rel->conf_monitor;

		    if (rel_mon)
		    {
			if (xf86nameCompare (rel_mon->mon_identifier,
					      relative_name) == 0)
			{
			    relative = config->output[or];
			    break;
			}
		    }
		    if (strcmp (out_rel->name, relative_name) == 0)
		    {
			relative = config->output[or];
			break;
		    }
		}
		if (!relative)
		{
		    xf86DrvMsg (scrn->scrnIndex, X_ERROR,
				"Cannot position output %s relative to unknown output %s\n",
				output->name, relative_name);
		    output->initial_x = 0;
		    output->initial_y = 0;
		    any_set = TRUE;
		    continue;
		}
		if (relative->initial_x == POSITION_UNSET)
		{
		    keep_going = TRUE;
		    continue;
		}
		output->initial_x = relative->initial_x;
		output->initial_y = relative->initial_y;
		switch (relation) {
		case OPTION_BELOW:
		    output->initial_y += xf86ModeHeight (modes[or], relative->initial_rotation);
		    break;
		case OPTION_RIGHT_OF:
		    output->initial_x += xf86ModeWidth (modes[or], relative->initial_rotation);
		    break;
		case OPTION_ABOVE:
		    output->initial_y -= xf86ModeHeight (modes[or], relative->initial_rotation);
		    break;
		case OPTION_LEFT_OF:
		    output->initial_x -= xf86ModeWidth (modes[or], relative->initial_rotation);
		    break;
		default:
		    break;
		}
		any_set = TRUE;
		continue;
	    }
	    
	    /* Nothing set, just stick them at 0,0 */
	    output->initial_x = 0;
	    output->initial_y = 0;
	    any_set = TRUE;
	}
	if (!keep_going)
	    break;
	if (!any_set) 
	{
	    for (o = 0; o < config->num_output; o++)
	    {
		xf86OutputPtr   output = config->output[o];
		if (output->initial_x == POSITION_UNSET)
		{
		    xf86DrvMsg (scrn->scrnIndex, X_ERROR,
				"Output position loop. Moving %s to 0,0\n",
				output->name);
		    output->initial_x = output->initial_y = 0;
		    break;
		}
	    }
	}
    }

    /*
     * normalize positions
     */
    min_x = 1000000;
    min_y = 1000000;
    for (o = 0; o < config->num_output; o++)
    {
	xf86OutputPtr	output = config->output[o];

	if (output->initial_x < min_x)
	    min_x = output->initial_x;
	if (output->initial_y < min_y)
	    min_y = output->initial_y;
    }
    
    for (o = 0; o < config->num_output; o++)
    {
	xf86OutputPtr	output = config->output[o];

	output->initial_x -= min_x;
	output->initial_y -= min_y;
    }
    return TRUE;
}

/*
 * XXX walk the monitor mode list and prune out duplicates that
 * are inserted by xf86DDCMonitorSet. In an ideal world, that
 * function would do this work by itself.
 */

static void
xf86PruneDuplicateMonitorModes (MonPtr Monitor)
{
    DisplayModePtr  master, clone, next;

    for (master = Monitor->Modes; 
	 master && master != Monitor->Last; 
	 master = master->next)
    {
	for (clone = master->next; clone && clone != Monitor->Modes; clone = next)
	{
	    next = clone->next;
	    if (xf86ModesEqual (master, clone))
	    {
		if (Monitor->Last == clone)
		    Monitor->Last = clone->prev;
		xf86DeleteMode (&Monitor->Modes, clone);
	    }
	}
    }
}

/** Return - 0 + if a should be earlier, same or later than b in list
 */
static int
xf86ModeCompare (DisplayModePtr a, DisplayModePtr b)
{
    int	diff;

    diff = ((b->type & M_T_PREFERRED) != 0) - ((a->type & M_T_PREFERRED) != 0);
    if (diff)
	return diff;
    diff = b->HDisplay * b->VDisplay - a->HDisplay * a->VDisplay;
    if (diff)
	return diff;
    diff = b->Clock - a->Clock;
    return diff;
}

/**
 * Insertion sort input in-place and return the resulting head
 */
static DisplayModePtr
xf86SortModes (DisplayModePtr input)
{
    DisplayModePtr  output = NULL, i, o, n, *op, prev;

    /* sort by preferred status and pixel area */
    while (input)
    {
	i = input;
	input = input->next;
	for (op = &output; (o = *op); op = &o->next)
	    if (xf86ModeCompare (o, i) > 0)
		break;
	i->next = *op;
	*op = i;
    }
    /* prune identical modes */
    for (o = output; o && (n = o->next); o = n)
    {
	if (!strcmp (o->name, n->name) && xf86ModesEqual (o, n))
	{
	    o->next = n->next;
	    xfree (n->name);
	    xfree (n);
	    n = o;
	}
    }
    /* hook up backward links */
    prev = NULL;
    for (o = output; o; o = o->next)
    {
	o->prev = prev;
	prev = o;
    }
    return output;
}

#define DEBUG_REPROBE 1

void
xf86ProbeOutputModes (ScrnInfoPtr scrn, int maxX, int maxY)
{
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(scrn);
    int			o;

    if (maxX == 0 || maxY == 0)
	xf86RandR12GetOriginalVirtualSize (scrn, &maxX, &maxY);

    /* Elide duplicate modes before defaulting code uses them */
    xf86PruneDuplicateMonitorModes (scrn->monitor);
    
    /* Probe the list of modes for each output. */
    for (o = 0; o < config->num_output; o++) 
    {
	xf86OutputPtr	    output = config->output[o];
	DisplayModePtr	    mode;
	DisplayModePtr	    config_modes = NULL, output_modes, default_modes;
	char		    *preferred_mode;
	xf86MonPtr	    edid_monitor;
	XF86ConfMonitorPtr  conf_monitor;
	MonRec		    mon_rec;
	int		    min_clock = 0;
	int		    max_clock = 0;
	double		    clock;
	enum { sync_config, sync_edid, sync_default } sync_source = sync_default;
	
	while (output->probed_modes != NULL)
	    xf86DeleteMode(&output->probed_modes, output->probed_modes);

	/*
	 * Check connection status
	 */
	output->status = (*output->funcs->detect)(output);

	if (output->status == XF86OutputStatusDisconnected)
	{
	    xf86OutputSetEDID (output, NULL);
	    continue;
	}

	memset (&mon_rec, '\0', sizeof (mon_rec));
	
	conf_monitor = output->conf_monitor;
	
	if (conf_monitor)
	{
	    int	i;
	    
	    for (i = 0; i < conf_monitor->mon_n_hsync; i++)
	    {
		mon_rec.hsync[mon_rec.nHsync].lo = conf_monitor->mon_hsync[i].lo;
		mon_rec.hsync[mon_rec.nHsync].hi = conf_monitor->mon_hsync[i].hi;
		mon_rec.nHsync++;
		sync_source = sync_config;
	    }
	    for (i = 0; i < conf_monitor->mon_n_vrefresh; i++)
	    {
		mon_rec.vrefresh[mon_rec.nVrefresh].lo = conf_monitor->mon_vrefresh[i].lo;
		mon_rec.vrefresh[mon_rec.nVrefresh].hi = conf_monitor->mon_vrefresh[i].hi;
		mon_rec.nVrefresh++;
		sync_source = sync_config;
	    }
	    config_modes = xf86GetMonitorModes (scrn, conf_monitor);
	}
	
	output_modes = (*output->funcs->get_modes) (output);
	
	edid_monitor = output->MonInfo;
	
	if (edid_monitor)
	{
	    int			    i;
	    Bool		    set_hsync = mon_rec.nHsync == 0;
	    Bool		    set_vrefresh = mon_rec.nVrefresh == 0;

	    for (i = 0; i < sizeof (edid_monitor->det_mon) / sizeof (edid_monitor->det_mon[0]); i++)
	    {
		if (edid_monitor->det_mon[i].type == DS_RANGES)
		{
		    struct monitor_ranges   *ranges = &edid_monitor->det_mon[i].section.ranges;
		    if (set_hsync && ranges->max_h)
		    {
			mon_rec.hsync[mon_rec.nHsync].lo = ranges->min_h;
			mon_rec.hsync[mon_rec.nHsync].hi = ranges->max_h;
			mon_rec.nHsync++;
			if (sync_source == sync_default)
			    sync_source = sync_edid;
		    }
		    if (set_vrefresh && ranges->max_v)
		    {
			mon_rec.vrefresh[mon_rec.nVrefresh].lo = ranges->min_v;
			mon_rec.vrefresh[mon_rec.nVrefresh].hi = ranges->max_v;
			mon_rec.nVrefresh++;
			if (sync_source == sync_default)
			    sync_source = sync_edid;
		    }
		    if (ranges->max_clock > max_clock)
			max_clock = ranges->max_clock;
		}
	    }
	}

	if (xf86GetOptValFreq (output->options, OPTION_MIN_CLOCK,
			       OPTUNITS_KHZ, &clock))
	    min_clock = (int) clock;
	if (xf86GetOptValFreq (output->options, OPTION_MAX_CLOCK,
			       OPTUNITS_KHZ, &clock))
	    max_clock = (int) clock;

	/*
	 * These limits will end up setting a 1024x768@60Hz mode by default,
	 * which seems like a fairly good mode to use when nothing else is
	 * specified
	 */
	if (mon_rec.nHsync == 0)
	{
	    mon_rec.hsync[0].lo = 31.0;
	    mon_rec.hsync[0].hi = 55.0;
	    mon_rec.nHsync = 1;
	}
	if (mon_rec.nVrefresh == 0)
	{
	    mon_rec.vrefresh[0].lo = 58.0;
	    mon_rec.vrefresh[0].hi = 62.0;
	    mon_rec.nVrefresh = 1;
	}
	default_modes = xf86GetDefaultModes (output->interlaceAllowed,
					     output->doubleScanAllowed);
	
	if (sync_source == sync_config)
	{
	    /* 
	     * Check output and config modes against sync range from config file
	     */
	    xf86ValidateModesSync (scrn, output_modes, &mon_rec);
	    xf86ValidateModesSync (scrn, config_modes, &mon_rec);
	}
	/*
	 * Check default modes against sync range
	 */
        xf86ValidateModesSync (scrn, default_modes, &mon_rec);
	/*
	 * Check default modes against monitor max clock
	 */
	if (max_clock)
	    xf86ValidateModesClocks(scrn, default_modes,
				    &min_clock, &max_clock, 1);
	
	output->probed_modes = NULL;
	output->probed_modes = xf86ModesAdd (output->probed_modes, config_modes);
	output->probed_modes = xf86ModesAdd (output->probed_modes, output_modes);
	output->probed_modes = xf86ModesAdd (output->probed_modes, default_modes);
	
	/*
	 * Check all modes against max size
	 */
	if (maxX && maxY)
	    xf86ValidateModesSize (scrn, output->probed_modes,
				       maxX, maxY, 0);
	 
	/*
	 * Check all modes against output
	 */
	for (mode = output->probed_modes; mode != NULL; mode = mode->next) 
	    if (mode->status == MODE_OK)
		mode->status = (*output->funcs->mode_valid)(output, mode);
	
	xf86PruneInvalidModes(scrn, &output->probed_modes, TRUE);
	
	output->probed_modes = xf86SortModes (output->probed_modes);
	
	/* Check for a configured preference for a particular mode */
	preferred_mode = xf86GetOptValString (output->options,
					      OPTION_PREFERRED_MODE);

	if (preferred_mode)
	{
	    for (mode = output->probed_modes; mode; mode = mode->next)
	    {
		if (!strcmp (preferred_mode, mode->name))
		{
		    if (mode != output->probed_modes)
		    {
			if (mode->prev)
			    mode->prev->next = mode->next;
			if (mode->next)
			    mode->next->prev = mode->prev;
			mode->next = output->probed_modes;
			output->probed_modes->prev = mode;
			mode->prev = NULL;
			output->probed_modes = mode;
		    }
		    mode->type |= M_T_PREFERRED;
		    break;
		}
	    }
	}
	
	output->initial_rotation = xf86OutputInitialRotation (output);

#ifdef DEBUG_REPROBE
	if (output->probed_modes != NULL) {
	    xf86DrvMsg(scrn->scrnIndex, X_INFO,
		       "Printing probed modes for output %s\n",
		       output->name);
	} else {
	    xf86DrvMsg(scrn->scrnIndex, X_INFO,
		       "No remaining probed modes for output %s\n",
		       output->name);
	}
#endif
	for (mode = output->probed_modes; mode != NULL; mode = mode->next)
	{
	    /* The code to choose the best mode per pipe later on will require
	     * VRefresh to be set.
	     */
	    mode->VRefresh = xf86ModeVRefresh(mode);
	    xf86SetModeCrtc(mode, INTERLACE_HALVE_V);

#ifdef DEBUG_REPROBE
	    xf86PrintModeline(scrn->scrnIndex, mode);
#endif
	}
    }
}


/**
 * Copy one of the output mode lists to the ScrnInfo record
 */

/* XXX where does this function belong? Here? */
void
xf86RandR12GetOriginalVirtualSize(ScrnInfoPtr scrn, int *x, int *y);

void
xf86SetScrnInfoModes (ScrnInfoPtr scrn)
{
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(scrn);
    xf86OutputPtr	output;
    xf86CrtcPtr		crtc;
    DisplayModePtr	last, mode;

    output = config->output[config->compat_output];
    if (!output->crtc)
    {
	int o;

	output = NULL;
	for (o = 0; o < config->num_output; o++)
	    if (config->output[o]->crtc)
	    {
		config->compat_output = o;
		output = config->output[o];
		break;
	    }
	/* no outputs are active, punt and leave things as they are */
	if (!output)
	    return;
    }
    crtc = output->crtc;

    /* Clear any existing modes from scrn->modes */
    while (scrn->modes != NULL)
	xf86DeleteMode(&scrn->modes, scrn->modes);

    /* Set scrn->modes to the mode list for the 'compat' output */
    scrn->modes = xf86DuplicateModes(scrn, output->probed_modes);

    for (mode = scrn->modes; mode; mode = mode->next)
	if (xf86ModesEqual (mode, &crtc->desiredMode))
	    break;

    if (scrn->modes != NULL) {
	/* For some reason, scrn->modes is circular, unlike the other mode
	 * lists.  How great is that?
	 */
	for (last = scrn->modes; last && last->next; last = last->next)
	    ;
	last->next = scrn->modes;
	scrn->modes->prev = last;
	if (mode) {
	    while (scrn->modes != mode)
		scrn->modes = scrn->modes->next;
	}
    }
    scrn->currentMode = scrn->modes;
}

/**
 * Construct default screen configuration
 *
 * Given auto-detected (and, eventually, configured) values,
 * construct a usable configuration for the system
 *
 * canGrow indicates that the driver can resize the screen to larger than its
 * initially configured size via the config->funcs->resize hook.  If TRUE, this
 * function will set virtualX and virtualY to match the initial configuration
 * and leave config->max{Width,Height} alone.  If FALSE, it will bloat
 * virtual[XY] to include the largest modes and set config->max{Width,Height}
 * accordingly.
 */

Bool
xf86InitialConfiguration (ScrnInfoPtr scrn, Bool canGrow)
{
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(scrn);
    int			o, c;
    DisplayModePtr	target_mode = NULL;
    Rotation		target_rotation = RR_Rotate_0;
    xf86CrtcPtr		*crtcs;
    DisplayModePtr	*modes;
    Bool		*enabled;
    int			width;
    int			height;

    if (scrn->display->virtualX)
	width = scrn->display->virtualX;
    else
	width = config->maxWidth;
    if (scrn->display->virtualY)
	height = scrn->display->virtualY;
    else
	height = config->maxHeight;

    xf86ProbeOutputModes (scrn, width, height);

    crtcs = xnfcalloc (config->num_output, sizeof (xf86CrtcPtr));
    modes = xnfcalloc (config->num_output, sizeof (DisplayModePtr));
    enabled = xnfcalloc (config->num_output, sizeof (Bool));
    
    for (o = 0; o < config->num_output; o++)
    {
	xf86OutputPtr output = config->output[o];
	
	modes[o] = NULL;
	enabled[o] = (xf86OutputEnabled (output) &&
		      output->status != XF86OutputStatusDisconnected);
    }
    
    /*
     * Let outputs with preferred modes drive screen size
     */
    for (o = 0; o < config->num_output; o++)
    {
	xf86OutputPtr output = config->output[o];

	if (enabled[o] &&
	    xf86OutputHasPreferredMode (output, width, height))
	{
	    target_mode = xf86DefaultMode (output, width, height);
	    target_rotation = output->initial_rotation;
	    if (target_mode)
	    {
		modes[o] = target_mode;
		config->compat_output = o;
		break;
	    }
	}
    }
    if (!target_mode)
    {
	for (o = 0; o < config->num_output; o++)
	{
	    xf86OutputPtr output = config->output[o];
	    if (enabled[o])
	    {
		target_mode = xf86DefaultMode (output, width, height);
		target_rotation = output->initial_rotation;
		if (target_mode)
		{
		    modes[o] = target_mode;
		    config->compat_output = o;
		    break;
		}
	    }
	}
    }
    for (o = 0; o < config->num_output; o++)
    {
	xf86OutputPtr output = config->output[o];
	
	if (enabled[o] && !modes[o])
	    modes[o] = xf86ClosestMode (output, target_mode, target_rotation, width, height);
    }

    /*
     * Set the position of each output
     */
    if (!xf86InitialOutputPositions (scrn, modes))
    {
	xfree (crtcs);
	xfree (modes);
	return FALSE;
    }
	
    /*
     * Assign CRTCs to fit output configuration
     */
    if (!xf86PickCrtcs (scrn, crtcs, modes, 0, width, height))
    {
	xfree (crtcs);
	xfree (modes);
	return FALSE;
    }
    
    /* XXX override xf86 common frame computation code */
    
    scrn->display->frameX0 = 0;
    scrn->display->frameY0 = 0;
    
    for (c = 0; c < config->num_crtc; c++)
    {
	xf86CrtcPtr	crtc = config->crtc[c];

	crtc->enabled = FALSE;
	memset (&crtc->desiredMode, '\0', sizeof (crtc->desiredMode));
    }
    
    /*
     * Set initial configuration
     */
    for (o = 0; o < config->num_output; o++)
    {
	xf86OutputPtr	output = config->output[o];
	DisplayModePtr	mode = modes[o];
        xf86CrtcPtr	crtc = crtcs[o];

	if (mode && crtc)
	{
	    crtc->desiredMode = *mode;
	    crtc->desiredRotation = output->initial_rotation;
	    crtc->enabled = TRUE;
	    crtc->x = output->initial_x;
	    crtc->y = output->initial_y;
	    output->crtc = crtc;
	}
    }
    
    if (scrn->display->virtualX == 0)
    {
	/*
	 * Expand virtual size to cover the current config and potential mode
	 * switches, if the driver can't enlarge the screen later.
	 */
	xf86DefaultScreenLimits (scrn, &width, &height, canGrow);
    
	scrn->display->virtualX = width;
	scrn->display->virtualY = height;
    }

    if (width > scrn->virtualX)
	scrn->virtualX = width;
    if (height > scrn->virtualY)
	scrn->virtualY = height;

    /*
     * Make sure the configuration isn't too small.
     */
    if (width < config->minWidth || height < config->minHeight)
	return FALSE;

    /*
     * Limit the crtc config to virtual[XY] if the driver can't grow the
     * desktop.
     */
    if (!canGrow)
    {
	xf86CrtcSetSizeRange (scrn, config->minWidth, config->minHeight,
			      width, height);
    }

    /* Mirror output modes to scrn mode list */
    xf86SetScrnInfoModes (scrn);
    
    xfree (crtcs);
    xfree (modes);
    return TRUE;
}

/*
 * Using the desired mode information in each crtc, set
 * modes (used in EnterVT functions, or at server startup)
 */

Bool
xf86SetDesiredModes (ScrnInfoPtr scrn)
{
    xf86CrtcConfigPtr   config = XF86_CRTC_CONFIG_PTR(scrn);
    int			c;

    for (c = 0; c < config->num_crtc; c++)
    {
	xf86CrtcPtr	crtc = config->crtc[c];
	xf86OutputPtr	output = NULL;
	int		o;

	if (config->output[config->compat_output]->crtc == crtc)
	    output = config->output[config->compat_output];
	else
	{
	    for (o = 0; o < config->num_output; o++)
		if (config->output[o]->crtc == crtc)
		{
		    output = config->output[o];
		    break;
		}
	}
	/*
	 * Skip disabled crtcs
	 */
	if (!output)
	    continue;

	/* Mark that we'll need to re-set the mode for sure */
	memset(&crtc->mode, 0, sizeof(crtc->mode));
	if (!crtc->desiredMode.CrtcHDisplay)
	{
	    DisplayModePtr  mode = xf86OutputFindClosestMode (output, scrn->currentMode);

	    if (!mode)
		return FALSE;
	    crtc->desiredMode = *mode;
	    crtc->desiredRotation = RR_Rotate_0;
	    crtc->desiredX = 0;
	    crtc->desiredY = 0;
	}

	if (!xf86CrtcSetMode (crtc, &crtc->desiredMode, crtc->desiredRotation,
			      crtc->desiredX, crtc->desiredY))
	    return FALSE;
    }

    xf86DisableUnusedFunctions(scrn);
    return TRUE;
}

/**
 * In the current world order, there are lists of modes per output, which may
 * or may not include the mode that was asked to be set by XFree86's mode
 * selection.  Find the closest one, in the following preference order:
 *
 * - Equality
 * - Closer in size to the requested mode, but no larger
 * - Closer in refresh rate to the requested mode.
 */

DisplayModePtr
xf86OutputFindClosestMode (xf86OutputPtr output, DisplayModePtr desired)
{
    DisplayModePtr	best = NULL, scan = NULL;

    for (scan = output->probed_modes; scan != NULL; scan = scan->next) 
    {
	/* If there's an exact match, we're done. */
	if (xf86ModesEqual(scan, desired)) {
	    best = desired;
	    break;
	}

	/* Reject if it's larger than the desired mode. */
	if (scan->HDisplay > desired->HDisplay || 
	    scan->VDisplay > desired->VDisplay)
	{
	    continue;
	}

	/*
	 * If we haven't picked a best mode yet, use the first
	 * one in the size range
	 */
	if (best == NULL) 
	{
	    best = scan;
	    continue;
	}

	/* Find if it's closer to the right size than the current best
	 * option.
	 */
	if ((scan->HDisplay > best->HDisplay &&
	     scan->VDisplay >= best->VDisplay) ||
	    (scan->HDisplay >= best->HDisplay &&
	     scan->VDisplay > best->VDisplay))
	{
	    best = scan;
	    continue;
	}

	/* Find if it's still closer to the right refresh than the current
	 * best resolution.
	 */
	if (scan->HDisplay == best->HDisplay &&
	    scan->VDisplay == best->VDisplay &&
	    (fabs(scan->VRefresh - desired->VRefresh) <
	     fabs(best->VRefresh - desired->VRefresh))) {
	    best = scan;
	}
    }
    return best;
}

/**
 * When setting a mode through XFree86-VidModeExtension or XFree86-DGA,
 * take the specified mode and apply it to the crtc connected to the compat
 * output. Then, find similar modes for the other outputs, as with the
 * InitialConfiguration code above. The goal is to clone the desired
 * mode across all outputs that are currently active.
 */

Bool
xf86SetSingleMode (ScrnInfoPtr pScrn, DisplayModePtr desired, Rotation rotation)
{
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(pScrn);
    Bool		ok = TRUE;
    xf86OutputPtr	compat_output = config->output[config->compat_output];
    DisplayModePtr	compat_mode;
    int			c;

    /*
     * Let the compat output drive the final mode selection
     */
    compat_mode = xf86OutputFindClosestMode (compat_output, desired);
    if (compat_mode)
	desired = compat_mode;
    
    for (c = 0; c < config->num_crtc; c++)
    {
	xf86CrtcPtr	crtc = config->crtc[c];
	DisplayModePtr	crtc_mode = NULL;
	int		o;

	if (!crtc->enabled)
	    continue;
	
	for (o = 0; o < config->num_output; o++)
	{
	    xf86OutputPtr   output = config->output[o];
	    DisplayModePtr  output_mode;

	    /* skip outputs not on this crtc */
	    if (output->crtc != crtc)
		continue;
	    
	    if (crtc_mode)
	    {
		output_mode = xf86OutputFindClosestMode (output, crtc_mode);
		if (output_mode != crtc_mode)
		    output->crtc = NULL;
	    }
	    else
		crtc_mode = xf86OutputFindClosestMode (output, desired);
	}
	if (!xf86CrtcSetMode (crtc, crtc_mode, rotation, 0, 0))
	    ok = FALSE;
	else
	    crtc->desiredMode = *crtc_mode;
    }
    xf86DisableUnusedFunctions(pScrn);
    return ok;
}


/**
 * Set the DPMS power mode of all outputs and CRTCs.
 *
 * If the new mode is off, it will turn off outputs and then CRTCs.
 * Otherwise, it will affect CRTCs before outputs.
 */
void
xf86DPMSSet(ScrnInfoPtr scrn, int mode, int flags)
{
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(scrn);
    int			i;

    if (!scrn->vtSema)
	return;

    if (mode == DPMSModeOff) {
	for (i = 0; i < config->num_output; i++) {
	    xf86OutputPtr output = config->output[i];
	    if (output->crtc != NULL)
		(*output->funcs->dpms) (output, mode);
	}
    }

    for (i = 0; i < config->num_crtc; i++) {
	xf86CrtcPtr crtc = config->crtc[i];
	if (crtc->enabled)
	    (*crtc->funcs->dpms) (crtc, mode);
    }

    if (mode != DPMSModeOff) {
	for (i = 0; i < config->num_output; i++) {
	    xf86OutputPtr output = config->output[i];
	    if (output->crtc != NULL)
		(*output->funcs->dpms) (output, mode);
	}
    }
}

/**
 * Implement the screensaver by just calling down into the driver DPMS hooks.
 *
 * Even for monitors with no DPMS support, by the definition of our DPMS hooks,
 * the outputs will still get disabled (blanked).
 */
Bool
xf86SaveScreen(ScreenPtr pScreen, int mode)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

    if (xf86IsUnblank(mode))
	xf86DPMSSet(pScrn, DPMSModeOn, 0);
    else
	xf86DPMSSet(pScrn, DPMSModeOff, 0);

    return TRUE;
}

/**
 * Disable all inactive crtcs and outputs
 */
void
xf86DisableUnusedFunctions(ScrnInfoPtr pScrn)
{
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    int			o, c;

    for (o = 0; o < xf86_config->num_output; o++) 
    {
	xf86OutputPtr  output = xf86_config->output[o];
	if (!output->crtc) 
	    (*output->funcs->dpms)(output, DPMSModeOff);
    }

    for (c = 0; c < xf86_config->num_crtc; c++) 
    {
	xf86CrtcPtr crtc = xf86_config->crtc[c];

	if (!crtc->enabled) 
	{
	    crtc->funcs->dpms(crtc, DPMSModeOff);
	    memset(&crtc->mode, 0, sizeof(crtc->mode));
	}
    }
}

#ifdef RANDR_12_INTERFACE

#define EDID_ATOM_NAME		"EDID_DATA"

/**
 * Set the RandR EDID property
 */
static void
xf86OutputSetEDIDProperty (xf86OutputPtr output, void *data, int data_len)
{
    Atom edid_atom = MakeAtom(EDID_ATOM_NAME, sizeof(EDID_ATOM_NAME), TRUE);

    /* This may get called before the RandR resources have been created */
    if (output->randr_output == NULL)
	return;

    if (data_len != 0) {
	RRChangeOutputProperty(output->randr_output, edid_atom, XA_INTEGER, 8,
			       PropModeReplace, data_len, data, FALSE);
    } else {
	RRDeleteOutputProperty(output->randr_output, edid_atom);
    }
}

#endif

/**
 * Set the EDID information for the specified output
 */
void
xf86OutputSetEDID (xf86OutputPtr output, xf86MonPtr edid_mon)
{
    ScrnInfoPtr		scrn = output->scrn;
    xf86CrtcConfigPtr	config = XF86_CRTC_CONFIG_PTR(scrn);
    int			i;
#ifdef RANDR_12_INTERFACE
    int			size;
#endif
    
    if (output->MonInfo != NULL)
	xfree(output->MonInfo);
    
    output->MonInfo = edid_mon;

    /* Debug info for now, at least */
    xf86DrvMsg(scrn->scrnIndex, X_INFO, "EDID for output %s\n", output->name);
    xf86PrintEDID(edid_mon);
    
    /* Set the DDC properties for the 'compat' output */
    if (output == config->output[config->compat_output])
        xf86SetDDCproperties(scrn, edid_mon);

#ifdef RANDR_12_INTERFACE
    /* Set the RandR output properties */
    size = 0;
    if (edid_mon)
    {
	if (edid_mon->ver.version == 1)
	    size = 128;
	else if (edid_mon->ver.version == 2)
	    size = 256;
    }
    xf86OutputSetEDIDProperty (output, edid_mon ? edid_mon->rawData : NULL, size);
#endif

    if (edid_mon)
    {
	/* Pull out a phyiscal size from a detailed timing if available. */
	for (i = 0; i < 4; i++) {
	    if (edid_mon->det_mon[i].type == DT &&
		edid_mon->det_mon[i].section.d_timings.h_size != 0 &&
		edid_mon->det_mon[i].section.d_timings.v_size != 0)
	    {
		output->mm_width = edid_mon->det_mon[i].section.d_timings.h_size;
		output->mm_height = edid_mon->det_mon[i].section.d_timings.v_size;
		break;
	    }
	}
    
	/* if no mm size is available from a detailed timing, check the max size field */
	if ((!output->mm_width || !output->mm_height) &&
	    (edid_mon->features.hsize && edid_mon->features.vsize))
	{
	    output->mm_width = edid_mon->features.hsize * 10;
	    output->mm_height = edid_mon->features.vsize * 10;
	}
    }
}

/**
 * Return the list of modes supported by the EDID information
 * stored in 'output'
 */
DisplayModePtr
xf86OutputGetEDIDModes (xf86OutputPtr output)
{
    ScrnInfoPtr	scrn = output->scrn;
    xf86MonPtr	edid_mon = output->MonInfo;

    if (!edid_mon)
	return NULL;
    return xf86DDCGetModes(scrn->scrnIndex, edid_mon);
}

xf86MonPtr
xf86OutputGetEDID (xf86OutputPtr output, I2CBusPtr pDDCBus)
{
    ScrnInfoPtr	scrn = output->scrn;

    return xf86DoEDID_DDC2 (scrn->scrnIndex, pDDCBus);
}

static char *_xf86ConnectorNames[] = { "None", "VGA", "DVI-I", "DVI-D",
				      "DVI-A", "Composite", "S-Video",
				      "Component", "LFP", "Proprietary" };
char *
xf86ConnectorGetName(xf86ConnectorType connector)
{
    return _xf86ConnectorNames[connector];
}
