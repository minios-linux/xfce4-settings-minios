/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

static GMainLoop *loop;
static GtkWidget *window, *button;
static gboolean stay;
static guint attempts;

static gboolean
measure (gpointer data)
{
    if (gtk_widget_get_width (button) == 0 && ++attempts < 50)
        return G_SOURCE_CONTINUE;
    printf ("{\"scale\":%d,\"logical_width\":%d,\"logical_height\":%d}\n",
            gtk_widget_get_scale_factor (window),
            gtk_widget_get_width (button), gtk_widget_get_height (button));
    fflush (stdout);
    if (!stay)
        g_main_loop_quit (loop);
    return G_SOURCE_REMOVE;
}

static gboolean
quit (gpointer data)
{
    g_main_loop_quit (loop);
    return G_SOURCE_REMOVE;
}

int
main (int argc, char **argv)
{
    gtk_init ();
    stay = argc > 1 && strcmp (argv[1], "--stay") == 0;
    window = gtk_window_new ();
    gtk_window_set_title (GTK_WINDOW (window), "HiDPI GTK4 probe");
    gtk_window_set_default_size (GTK_WINDOW (window), 220, 80);
    button = gtk_button_new_with_label ("Interface scale");
    gtk_window_set_child (GTK_WINDOW (window), button);
    gtk_window_present (GTK_WINDOW (window));
    loop = g_main_loop_new (NULL, FALSE);
    g_timeout_add (100, measure, NULL);
    g_timeout_add_seconds (90, quit, NULL);
    g_main_loop_run (loop);
    gtk_window_destroy (GTK_WINDOW (window));
    g_main_loop_unref (loop);
    return 0;
}
