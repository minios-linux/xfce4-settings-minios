/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XFCE_GLOBAL_SCALE_X11_H
#define XFCE_GLOBAL_SCALE_X11_H

#include "global-scale.h"
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <string.h>

/* Canonical output transforms are not recoverable exactly by dividing a
 * quantized server transform by compensation. Publish their double bits with
 * a proof of the corresponding live CRTC configuration. Readers accept this
 * metadata only while the full transform and output assignment still match;
 * external RandR changes therefore invalidate it rather than being hidden.
 * CARDINAL words and explicit high/low halves work on both ILP32 and LP64. */
#define GLOBAL_SCALE_CRTC_WORDS 18

static inline gdouble
xfce_global_scale_x11_compensation (Display *display, Window root)
{
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    gdouble compensation = 1.0;
    if (XGetWindowProperty (display, root, XInternAtom (display, GLOBAL_SCALE_ATOM, False),
                           0, 1, False, XA_CARDINAL, &type, &format, &count,
                           &remaining, &data) == Success
        && type == XA_CARDINAL && format == 32 && count == 1)
        compensation = XFixedToDouble (*(unsigned long *) data);
    if (data != NULL)
        XFree (data);
    return compensation >= 1.0 && compensation <= 2.0 ? compensation : 1.0;
}

static inline Atom
xfce_global_scale_crtc_atom (Display *display, RRCrtc crtc)
{
    gchar name[64];
    g_snprintf (name, sizeof (name), "_XFCE_GLOBAL_SCALE_CRTC_%lu", crtc);
    return XInternAtom (display, name, False);
}

static inline void
xfce_global_scale_crtc_publish (Display *display, Window root, RRCrtc crtc,
                                RRMode mode, Rotation rotation, gint noutput,
                                const RROutput *outputs, const XTransform *transform,
                                gdouble compensation, gdouble scalex, gdouble scaley)
{
    unsigned long *words = g_new (unsigned long, GLOBAL_SCALE_CRTC_WORDS + noutput);
    guint64 bits;
    words[0] = 1; /* metadata format */
    words[1] = XDoubleToFixed (compensation);
    words[2] = mode;
    words[3] = rotation;
    words[4] = noutput;
    for (gint i = 0; i < 9; i++)
        words[5 + i] = (guint32) transform->matrix[i / 3][i % 3];
    memcpy (&bits, &scalex, sizeof (bits));
    words[14] = bits >> 32; words[15] = (guint32) bits;
    memcpy (&bits, &scaley, sizeof (bits));
    words[16] = bits >> 32; words[17] = (guint32) bits;
    for (gint i = 0; i < noutput; i++)
        words[GLOBAL_SCALE_CRTC_WORDS + i] = outputs[i];
    XChangeProperty (display, root, xfce_global_scale_crtc_atom (display, crtc),
                     XA_CARDINAL, 32, PropModeReplace, (unsigned char *) words,
                     GLOBAL_SCALE_CRTC_WORDS + noutput);
    g_free (words);
}

static inline gboolean
xfce_global_scale_crtc_read (Display *display, Window root, RRCrtc crtc,
                             const XRRCrtcInfo *info, const XTransform *transform,
                             gdouble compensation, gdouble *scalex, gdouble *scaley)
{
    Atom type;
    int format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    unsigned long *words;
    guint64 bits;
    gdouble x, y;
    gboolean valid = FALSE;

    if (XGetWindowProperty (display, root, xfce_global_scale_crtc_atom (display, crtc),
                           0, 4096, False, XA_CARDINAL, &type, &format, &count,
                           &remaining, &data) != Success
        || type != XA_CARDINAL || format != 32 || remaining != 0
        || count != GLOBAL_SCALE_CRTC_WORDS + (gulong) info->noutput)
        goto out;
    words = (unsigned long *) data;
    /* Mode/rotation are diagnostic geometry, not transform identity: an
     * external mode change with an unchanged matrix/output set must not lose
     * canonical scale. Callers always obtain the new mode/rotation live. */
    if (words[0] != 1 || words[1] != (guint32) XDoubleToFixed (compensation)
        || words[4] != (gulong) info->noutput)
        goto out;
    for (gint i = 0; i < 9; i++)
        if ((guint32) words[5 + i] != (guint32) transform->matrix[i / 3][i % 3])
            goto out;
    for (gint i = 0; i < info->noutput; i++)
    {
        gboolean found = FALSE;
        for (gint j = 0; j < info->noutput; j++)
            found |= words[GLOBAL_SCALE_CRTC_WORDS + j] == info->outputs[i];
        if (!found)
            goto out;
    }
    bits = ((guint64) (guint32) words[14] << 32) | (guint32) words[15];
    memcpy (&x, &bits, sizeof (x));
    bits = ((guint64) (guint32) words[16] << 32) | (guint32) words[17];
    memcpy (&y, &bits, sizeof (y));
    if (!isfinite (x) || !isfinite (y) || x <= 0.0 || y <= 0.0)
        goto out;
    *scalex = x; *scaley = y;
    valid = TRUE;
out:
    if (data != NULL)
        XFree (data);
    return valid;
}

#endif
