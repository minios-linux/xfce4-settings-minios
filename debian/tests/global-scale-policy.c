/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "common/global-scale.h"

static void
test_automatic_desktop (void)
{
    g_assert_cmpint (xfce_global_scale_automatic (1920, 1080, 531, 299, FALSE), ==, 100);
    g_assert_cmpint (xfce_global_scale_automatic (2560, 1440, 531, 299, FALSE), ==, 125);
    g_assert_cmpint (xfce_global_scale_automatic (2560, 1440, 597, 336, FALSE), ==, 100);
    g_assert_cmpint (xfce_global_scale_automatic (3840, 2160, 597, 336, FALSE), ==, 175);
    g_assert_cmpint (xfce_global_scale_automatic (3840, 2160, 708, 398, FALSE), ==, 150);
}

static void
test_automatic_laptop (void)
{
    /* MacBook Air 11 (2013), non-Retina: do not shrink its working area. */
    g_assert_cmpint (xfce_global_scale_automatic (1366, 768, 256, 144, TRUE), ==, 100);
    g_assert_cmpint (xfce_global_scale_automatic (1920, 1080, 294, 166, TRUE), ==, 125);
    g_assert_cmpint (xfce_global_scale_automatic (1920, 1080, 345, 194, TRUE), ==, 100);
    g_assert_cmpint (xfce_global_scale_automatic (2560, 1600, 294, 184, TRUE), ==, 175);
    g_assert_cmpint (xfce_global_scale_automatic (3840, 2160, 294, 166, TRUE), ==, 200);
    /* The same physical display has a different desktop viewing-distance target. */
    g_assert_cmpint (xfce_global_scale_automatic (2560, 1600, 294, 184, FALSE), ==, 200);
}

static void
test_workspace_limit (void)
{
    g_assert_cmpint (xfce_global_scale_automatic (1280, 800, 200, 125, TRUE), ==, 100);
    /* Density asks for 200%; 1080/800 only allows the supported 125% step. */
    g_assert_cmpint (xfce_global_scale_automatic (1920, 1080, 200, 113, TRUE), ==, 125);
    /* Rounding 1.35 upwards to 150% must not leave fewer than 800 pixels. */
    g_assert_cmpint (xfce_global_scale_automatic (1920, 1080, 200, 113, FALSE), ==, 125);
    for (gint height = 600; height <= 2400; height += 40)
    {
        gint width = height * 16 / 9;
        gint percent = xfce_global_scale_automatic (width, height, 256, 144, TRUE);
        g_assert_true (xfce_global_scale_valid (percent));
        if (percent > 100)
        {
            g_assert_cmpint (width * 100, >=, 800 * percent);
            g_assert_cmpint (height * 100, >=, 800 * percent);
        }
    }
}

static void
test_large_external (void)
{
    g_assert_cmpint (xfce_global_scale_automatic (3840, 2160, 1439, 809, FALSE), ==, 200);
    /* Width alone must not classify an ultrawide monitor as a television. */
    g_assert_cmpint (xfce_global_scale_automatic (3440, 1440, 797, 333, FALSE), ==, 100);
}

static void
test_invalid_edid (void)
{
    g_assert_cmpint (xfce_global_scale_automatic (3840, 2160, 0, 0, TRUE), ==, 100);
    g_assert_cmpint (xfce_global_scale_automatic (3840, 2160, 16, 9, FALSE), ==, 100);
    g_assert_cmpint (xfce_global_scale_automatic (3840, 2160, 294, 299, TRUE), ==, 100);
    g_assert_cmpint (xfce_global_scale_automatic (0, 0, 294, 166, TRUE), ==, 100);
    g_assert_cmpint (xfce_global_scale_automatic (1920, 1080, 5310, 2990, FALSE), ==, 100);
}

static void
test_cursor_size (void)
{
    g_assert_cmpint (xfce_global_scale_cursor_size (24, 1, 32), ==, 24);
    g_assert_cmpint (xfce_global_scale_cursor_size (24, 2, 32), ==, 48);
    g_assert_cmpint (xfce_global_scale_cursor_size (48, 2, 24), ==, 96);
    g_assert_cmpint (xfce_global_scale_cursor_size (0, 2, 24), ==, 48);
    g_assert_cmpint (xfce_global_scale_cursor_size (0, 0, 32), ==, 32);
    g_assert_cmpint (xfce_global_scale_cursor_size (24, 0, 32), ==, 24);
    g_assert_cmpint (xfce_global_scale_cursor_size (G_MAXINT, 2, 24), ==, G_MAXINT);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/global-scale/automatic/desktop", test_automatic_desktop);
    g_test_add_func ("/global-scale/automatic/laptop", test_automatic_laptop);
    g_test_add_func ("/global-scale/automatic/workspace", test_workspace_limit);
    g_test_add_func ("/global-scale/automatic/large-external", test_large_external);
    g_test_add_func ("/global-scale/automatic/invalid-edid", test_invalid_edid);
    g_test_add_func ("/global-scale/cursor-size", test_cursor_size);
    return g_test_run ();
}
