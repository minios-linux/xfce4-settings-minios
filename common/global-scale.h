/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XFCE_GLOBAL_SCALE_H
#define XFCE_GLOBAL_SCALE_H

#include <glib.h>
#include <math.h>

#define GLOBAL_SCALE_PROP "/GlobalScale"
#define GLOBAL_SCALE_REQUEST "/GlobalScale/Request"
#define GLOBAL_SCALE_CONFIRM "/GlobalScale/Confirm"
#define GLOBAL_SCALE_CANCEL "/GlobalScale/Cancel"
#define GLOBAL_SCALE_PENDING "/GlobalScale/Pending"
#define GLOBAL_SCALE_EFFECTIVE "/GlobalScale/Effective"
#define GLOBAL_SCALE_TOOLKIT "/GlobalScale/ToolkitScale"
#define GLOBAL_SCALE_ERROR "/GlobalScale/Error"
#define GLOBAL_SCALE_RESTORE_PROFILE "/GlobalScale/RestoreProfile"
#define GLOBAL_SCALE_ATOM "_XFCE_GLOBAL_SCALE_COMPENSATION"
#define GLOBAL_SCALE_GENERATION_ATOM "_XFCE_GLOBAL_SCALE_GENERATION"

static inline gboolean
xfce_global_scale_valid (gint percent)
{
    return percent == 0 || (percent >= 100 && percent <= 200 && percent % 25 == 0);
}

/* A negative UI choice is the preserved legacy (unmanaged) configuration. */
static inline gboolean
xfce_global_scale_choice_changed (gint choice, gboolean managed, gint saved)
{
    return choice >= 0 && (!managed || choice != saved);
}

static inline gint
xfce_global_scale_toolkit (gint percent)
{
    return percent > 100 ? 2 : 1;
}

static inline gdouble
xfce_global_scale_compensation (gint percent)
{
    return 100.0 * xfce_global_scale_toolkit (percent) / percent;
}

/* CursorThemeSize is a logical preference, not the last published Xcursor
 * size. Resolve automatic size before multiplication and saturate overflow. */
static inline gint
xfce_global_scale_cursor_size (gint logical_size, gint toolkit_scale, gint fallback)
{
    gint base = logical_size > 0 ? logical_size : MAX (fallback, 1);
    gint scale = toolkit_scale == 2 ? 2 : 1;
    return (gint) MIN ((gint64) base * scale, G_MAXINT);
}

/* Use physical mode pixels, never the transformed framebuffer dimensions.
 * Implausible EDID sizes, density or non-square pixels fall back to 100%.
 * Virtual connectors are rejected by the caller even if they invent mm sizes.
 * KDE-inspired defaults: laptop panels target 125 DPI, desktop monitors 96 DPI,
 * large external displays 30.5 DPI. Keep at least 800 logical pixels per axis
 * where the native mode allows it. Adapt KWin's policy to our 25% choices. */
static inline gint
xfce_global_scale_automatic (gint width, gint height, gint mm_width, gint mm_height,
                             gboolean internal)
{
    gdouble dx, dy, diagonal, scale, target_dpi, max_scale;
    gint percent, max_percent;

    if (width <= 0 || height <= 0 || mm_width < 100 || mm_height < 60
        || mm_width > 2000 || mm_height > 1500)
        return 100;
    dx = 25.4 * width / mm_width;
    dy = 25.4 * height / mm_height;
    diagonal = hypot (mm_width, mm_height) / 25.4;
    if (diagonal < 7 || diagonal > 85 || dx < 50 || dy < 50
        || dx > 500 || dy > 500 || fabs (dx / dy - 1.0) > 0.15)
        return 100;
    target_dpi = internal ? 125.0 : (mm_height > 500 ? 30.5 : 96.0);
    max_scale = CLAMP (MIN (width, height) / 800.0, 1.0, 2.0);
    scale = CLAMP (MIN (dx, dy) / target_dpi, 1.0, max_scale);
    /* Small fractional increases cost sharpness without much size benefit. */
    if (scale < 1.20)
        return 100;
    percent = (gint) floor (scale * 4.0 + 0.5) * 25;
    /* Nearest-quarter rounding must not violate the workspace limit. */
    max_percent = (gint) floor (max_scale * 4.0) * 25;
    return CLAMP (MIN (percent, max_percent), 100, 200);
}

#endif
