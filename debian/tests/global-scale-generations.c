/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real two-connection X11 reader test, run only on a container-local Xvfb.
 * The deterministic RandR backend test covers writer ordering and failures. */
#include "config.h"
#include "common/global-scale.h"
#include "common/global-scale-x11.h"
#include "common/xfce-randr.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <sys/wait.h>
#include <unistd.h>

static void publish (Display *display, const gchar *name, unsigned long value)
{
    XChangeProperty (display, DefaultRootWindow (display), XInternAtom (display, name, False),
                     XA_CARDINAL, 32, PropModeReplace, (unsigned char *) &value, 1);
}

static void test_incomplete_generation (void)
{
    Display *display = gdk_x11_get_default_xdisplay ();
    GError *error = NULL;
    publish (display, GLOBAL_SCALE_ATOM, XDoubleToFixed (1.0));
    publish (display, GLOBAL_SCALE_GENERATION_ATOM, 2);
    XSync (display, False);
    XfceRandr *randr = xfce_randr_new (gdk_display_get_default (), &error);
    g_assert_no_error (error); g_assert_nonnull (randr);
    g_assert_cmpint (randr->noutput, >, 0);
    gdouble previous = randr->scalex[0];
    publish (display, GLOBAL_SCALE_GENERATION_ATOM, 3);
    publish (display, GLOBAL_SCALE_ATOM, XDoubleToFixed (1.6));
    XSync (display, False);
    g_assert_false (xfce_randr_reload (randr));
    g_assert_cmpfloat (randr->scalex[0], ==, previous);
    XfceRandr *incomplete = xfce_randr_new (gdk_display_get_default (), &error);
    g_assert_null (incomplete); g_assert_nonnull (error); g_clear_error (&error);
    publish (display, GLOBAL_SCALE_GENERATION_ATOM, 4);
    XSync (display, False);
    g_assert_true (xfce_randr_reload (randr));
    g_assert_cmpfloat_with_epsilon (randr->scalex[0], previous / XFixedToDouble (XDoubleToFixed (1.6)), 0.000001);
    xfce_randr_free (randr);
}

static void test_concurrent_snapshot (void)
{
    Display *display = gdk_x11_get_default_xdisplay ();
    GError *error = NULL;
    publish (display, GLOBAL_SCALE_ATOM, XDoubleToFixed (1.0));
    publish (display, GLOBAL_SCALE_GENERATION_ATOM, 6);
    XSync (display, False);
    XfceRandr *randr = xfce_randr_new (gdk_display_get_default (), &error);
    g_assert_no_error (error);
    gdouble previous = randr->scalex[0];
    int ready[2]; g_assert_cmpint (pipe (ready), ==, 0);
    pid_t child = fork (); g_assert_cmpint (child, >=, 0);
    if (child == 0)
    {
        /* Use only a fresh Xlib connection in the forked child, never GDK. */
        close (ready[0]);
        Display *writer = XOpenDisplay (NULL);
        if (writer == NULL) _exit (2);
        XGrabServer (writer);
        publish (writer, GLOBAL_SCALE_GENERATION_ATOM, 7);
        publish (writer, GLOBAL_SCALE_ATOM, XDoubleToFixed (1.6));
        XSync (writer, False);
        if (write (ready[1], "x", 1) != 1) _exit (3);
        usleep (100000);
        publish (writer, GLOBAL_SCALE_GENERATION_ATOM, 8);
        XUngrabServer (writer);
        XFlush (writer);
        XCloseDisplay (writer);
        _exit (0);
    }
    close (ready[1]); char byte;
    g_assert_cmpint (read (ready[0], &byte, 1), ==, 1); close (ready[0]);
    /* The reader must wait for the server grab, not consume half publication. */
    g_assert_true (xfce_randr_reload (randr));
    g_assert_cmpfloat_with_epsilon (randr->scalex[0], previous / XFixedToDouble (XDoubleToFixed (1.6)), 0.000001);
    int status; g_assert_cmpint (waitpid (child, &status, 0), ==, child);
    g_assert_true (WIFEXITED (status)); g_assert_cmpint (WEXITSTATUS (status), ==, 0);
    xfce_randr_free (randr);
}

static void test_canonical_reader_and_profile (void)
{
    Display *display = gdk_x11_get_default_xdisplay ();
    Window root = DefaultRootWindow (display);
    GError *error = NULL;
    publish (display, GLOBAL_SCALE_ATOM, XDoubleToFixed (1.0));
    publish (display, GLOBAL_SCALE_GENERATION_ATOM, 10);
    XSync (display, False);
    XfceRandr *randr = xfce_randr_new (gdk_display_get_default (), &error);
    g_assert_no_error (error); g_assert_nonnull (randr);
    XRRScreenResources *resources = XRRGetScreenResourcesCurrent (display, root);
    XRROutputInfo *output = XRRGetOutputInfo (display, resources, resources->outputs[0]);
    g_assert_cmpint (output->crtc, !=, None);
    XRRCrtcInfo *info = XRRGetCrtcInfo (display, resources, output->crtc);
    XRRCrtcTransformAttributes *attributes = NULL;
    g_assert_true (XRRGetCrtcTransform (display, output->crtc, &attributes));
    XfconfChannel *channel = xfconf_channel_new ("global-scale-regression");
    const gint choices[] = { 125, 150, 175, 200, 100 };
    /* Xvfb cannot apply fractional transforms. Exercise the real shared reader
     * and real xfconf serialization with proof of its observed live matrix;
     * the backend test covers actual quantized transform/reload sequences. */
    for (guint n = 0; n < G_N_ELEMENTS (choices); n++)
    {
        gdouble compensation = XFixedToDouble (XDoubleToFixed (xfce_global_scale_compensation (choices[n])));
        XGrabServer (display);
        xfce_global_scale_crtc_publish (display, root, output->crtc, info->mode, info->rotation,
                                        info->noutput, info->outputs, &attributes->currentTransform,
                                        compensation, 1.25, 1.234567890123);
        publish (display, GLOBAL_SCALE_ATOM, XDoubleToFixed (compensation));
        publish (display, GLOBAL_SCALE_GENERATION_ATOM, 12 + 2 * n);
        XUngrabServer (display); XFlush (display);
        g_assert_true (xfce_randr_reload (randr));
        g_assert_cmpfloat (randr->scalex[0], ==, 1.25);
        g_assert_cmpfloat (randr->scaley[0], ==, 1.234567890123);
        xfce_randr_save_output (randr, "Default", channel, 0);
        gchar *key = g_strdup_printf ("/Default/%s/Scale", xfce_randr_get_output_info_name (randr, 0));
        g_assert_cmpfloat (xfconf_channel_get_double (channel, key, -1), ==, 1.25);
        g_free (key);
    }
    /* A mismatching full transform proof must not override the live query. */
    attributes->currentTransform.matrix[0][2] = XDoubleToFixed (3.0);
    xfce_global_scale_crtc_publish (display, root, output->crtc, info->mode, info->rotation,
                                    info->noutput, info->outputs, &attributes->currentTransform,
                                    1.0, 1.25, 1.25);
    XSync (display, False);
    g_assert_true (xfce_randr_reload (randr));
    g_assert_cmpfloat (randr->scalex[0], ==, 1.0);
    g_object_unref (channel); XFree (attributes);
    XRRFreeCrtcInfo (info); XRRFreeOutputInfo (output); XRRFreeScreenResources (resources);
    xfce_randr_free (randr);
}

int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_assert_true (gtk_init_check (&argc, &argv));
    GError *error = NULL;
    g_assert_true (xfconf_init (&error)); g_assert_no_error (error);
    g_test_add_func ("/generations/retain-cache-until-complete", test_incomplete_generation);
    g_test_add_func ("/generations/concurrent-server-snapshot", test_concurrent_snapshot);
    g_test_add_func ("/generations/canonical-reader-profile-commit", test_canonical_reader_and_profile);
    int result = g_test_run ();
    xfconf_shutdown ();
    return result;
}
