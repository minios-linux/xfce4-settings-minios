/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the real backend with a deterministic two-output RandR server.
 * No X session, hardware or xfconf daemon is needed. Only I/O is substituted;
 * transaction, profile, geometry and publication code is compiled unchanged. */
#include "config.h"
/* g_test_expect_message intercepts the classic GLib logging entry point. */
#undef G_LOG_USE_STRUCTURED
#include "xfsettingsd/displays-x11.h"
#include "common/debug.h"
#include "common/display-profiles.h"
#include "common/xfce-randr.h"
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/dpms.h>
#include <libxfce4ui/libxfce4ui.h>

static gpointer xfce_displays_helper_x11_parent_class;
#undef G_DEFINE_TYPE
#define G_DEFINE_TYPE(a,b,c)
#define XFCE_IS_DISPLAYS_HELPER(p) TRUE
#define XFCE_IS_DISPLAYS_HELPER_X11(p) TRUE
#define XFCE_DISPLAYS_HELPER(p) ((XfceDisplaysHelper *) (p))
#define XFCE_DISPLAYS_HELPER_X11(p) ((XfceDisplaysHelperX11 *) (p))
#undef GDK_WINDOW_XID
#define GDK_WINDOW_XID(p) ((Window) 1)
#define xfsettings_dbg(...) ((void) 0)

static int grab_depth, configurations, fail_config, server_width, server_height;
static int max_width = 8192;
static unsigned long published_compensation, published_generation;
static RROutput server_primary;
static GHashTable *properties;
static XfceDisplaysHelperX11 *fixture;
static XfceRandr fixture_randr;
static int commits;
static gboolean fail_commit;
static GArray *canonical_properties[2];
static XTransform server_transforms[2];
static XRRModeInfo server_modes[2];
static RROutput server_outputs[2];
static RROutput possible_outputs[] = { 10, 11 };

static Bool fake_range (Display *d, Window w, int *a, int *b, int *c, int *e);
static XRRCrtcInfo *fake_info (Display *d, XRRScreenResources *r, RRCrtc c);
static Status fake_config (Display *d, XRRScreenResources *r, RRCrtc c, Time t,
                           int x, int y, RRMode mode, Rotation rotation, RROutput *o, int n);
static void fake_size (Display *d, Window w, int x, int y, int mmx, int mmy);
static void fake_transform (Display *d, RRCrtc c, XTransform *t, const char *f, XFixed *p, int n);
static void fake_primary (Display *d, Window w, RROutput o);
static Atom fake_atom (Display *d, const char *name, Bool only);
static int fake_property (Display *d, Window w, Atom a, Atom type, int fmt, int mode,
                          const unsigned char *data, int n);
static int fake_sync (Display *d, Bool discard);
static void fake_grab (GdkDisplay *d);
static void fake_ungrab (GdkDisplay *d);
static void fake_gdk_sync (GdkDisplay *d);
static void fake_void (GdkDisplay *d);
static gint fake_trap_pop (GdkDisplay *d);
static gint fake_dimension (void);
static XfconfChannel *fake_channel (XfceDisplaysHelper *h);
static gint fake_get_int (XfconfChannel *c, const gchar *p, gint fallback);
static gboolean fake_get_bool (XfconfChannel *c, const gchar *p, gboolean fallback);
static gchar *fake_get_string (XfconfChannel *c, const gchar *p, const gchar *fallback);
static gboolean fake_set_int (XfconfChannel *c, const gchar *p, gint v);
static gboolean fake_set_bool (XfconfChannel *c, const gchar *p, gboolean v);
static gboolean fake_set_string (XfconfChannel *c, const gchar *p, const gchar *v);
static gboolean fake_reset (XfconfChannel *c, const gchar *p, gboolean recursive);
static GHashTable *fake_properties (XfconfChannel *c, const gchar *prefix);
static gboolean fake_randr_reload (XfceRandr *r);
static void fake_randr_save (XfceRandr *r, const gchar *scheme, XfconfChannel *c, guint n);
static Status fake_get_transform (Display *d, RRCrtc c, XRRCrtcTransformAttributes **a);
static int fake_get_property (Display *d, Window w, Atom a, long offset, long length,
                              Bool del, Atom type, Atom *actual, int *format,
                              unsigned long *count, unsigned long *remaining, unsigned char **data);
static XRRScreenResources *fake_resources (Display *d, Window w);
static void fake_free_resources (XRRScreenResources *r);
static XRROutputInfo *fake_output (Display *d, XRRScreenResources *r, RROutput o);
static void fake_free_output (XRROutputInfo *o);
static RROutput fake_get_primary (Display *d, Window w);

#define XRRGetScreenSizeRange fake_range
#define XRRGetCrtcInfo fake_info
#define XRRSetCrtcConfig fake_config
#define XRRSetScreenSize fake_size
#define XRRSetCrtcTransform fake_transform
#define XRRSetOutputPrimary fake_primary
#define XRRGetOutputPrimary fake_get_primary
#define XRRGetCrtcTransform fake_get_transform
#define XRRGetScreenResourcesCurrent fake_resources
#define XRRFreeScreenResources fake_free_resources
#define XRRGetOutputInfo fake_output
#define XRRFreeOutputInfo fake_free_output
#define XInternAtom fake_atom
#define XChangeProperty fake_property
#define XGetWindowProperty fake_get_property
#define XSync fake_sync
#define gdk_x11_display_grab fake_grab
#define gdk_x11_display_ungrab fake_ungrab
#define gdk_display_sync fake_gdk_sync
#define gdk_display_flush fake_void
#define gdk_x11_display_error_trap_push fake_void
#define gdk_x11_display_error_trap_pop fake_trap_pop
#define gdk_screen_width fake_dimension
#define gdk_screen_height fake_dimension
#define gdk_screen_width_mm fake_dimension
#define gdk_screen_height_mm fake_dimension
#define xfce_displays_helper_get_channel fake_channel
#define xfconf_channel_get_int fake_get_int
#define xfconf_channel_get_bool fake_get_bool
#define xfconf_channel_get_string fake_get_string
#define xfconf_channel_set_int fake_set_int
#define xfconf_channel_set_bool fake_set_bool
#define xfconf_channel_set_string fake_set_string
#define xfconf_channel_reset_property fake_reset
#define xfconf_channel_get_properties fake_properties
#define xfce_randr_reload fake_randr_reload
#define xfce_randr_save_output fake_randr_save
#include "xfsettingsd/displays-x11.c"

static XfceRRCrtc server[2];

static Bool fake_range (Display *d, Window w, int *a, int *b, int *c, int *e)
{ *a = *b = 1; *c = max_width; *e = 8192; return True; }
static XRRCrtcInfo *fake_info (Display *d, XRRScreenResources *r, RRCrtc c)
{
    XfceRRCrtc *s = &server[c - 1];
    XRRCrtcInfo *info = calloc (1, sizeof (*info) + 3 * sizeof (RROutput));
    info->x = s->x; info->y = s->y;
    info->width = s->mode == None ? 0 : ceil (s->width * s->scalex);
    info->height = s->mode == None ? 0 : ceil (s->height * s->scaley);
    info->mode = s->mode; info->rotation = s->rotation; info->rotations = RR_Rotate_0;
    info->noutput = s->mode == None ? 0 : 1;
    info->outputs = (RROutput *) (info + 1); info->outputs[0] = server_outputs[c - 1];
    info->npossible = 2; info->possible = info->outputs + 1;
    memcpy (info->possible, possible_outputs, sizeof (possible_outputs));
    return info;
}
static Status fake_config (Display *d, XRRScreenResources *r, RRCrtc c, Time t,
                           int x, int y, RRMode mode, Rotation rotation, RROutput *o, int n)
{
    g_assert_cmpint (grab_depth, ==, 1);
    configurations++;
    if (fail_config && mode != None)
    { fail_config--; return RRSetConfigFailed; }
    XfceRRCrtc *s = &server[c - 1];
    s->x = x; s->y = y; s->mode = mode; s->rotation = rotation;
    if (n > 0) server_outputs[c - 1] = o[0];
    if (mode != None)
    {
        XfceRRCrtc *target = xfce_displays_helper_x11_find_crtc_by_id (fixture, c);
        s->width = target->width; s->height = target->height;
        g_assert_cmpint (x + ceil (s->width * s->scalex), <=, server_width);
        g_assert_cmpint (y + ceil (s->height * s->scaley), <=, server_height);
    }
    return RRSetConfigSuccess;
}
static void fake_size (Display *d, Window w, int x, int y, int mmx, int mmy)
{ g_assert_cmpint (grab_depth, ==, 1); server_width = x; server_height = y; }
static void fake_transform (Display *d, RRCrtc c, XTransform *t, const char *f, XFixed *p, int n)
{ server_transforms[c - 1] = *t; server[c - 1].scalex = XFixedToDouble (t->matrix[0][0]); server[c - 1].scaley = XFixedToDouble (t->matrix[1][1]); }
static Status fake_get_transform (Display *d, RRCrtc c, XRRCrtcTransformAttributes **a)
{ *a = calloc (1, sizeof (**a)); (*a)->currentTransform = server_transforms[c - 1]; return 1; }
static void fake_primary (Display *d, Window w, RROutput o) { server_primary = o; }
static RROutput fake_get_primary (Display *d, Window w) { return server_primary; }
static Atom fake_atom (Display *d, const char *name, Bool only)
{
    if (!strcmp (name, GLOBAL_SCALE_ATOM)) return 10;
    if (!strcmp (name, GLOBAL_SCALE_GENERATION_ATOM)) return 11;
    return g_str_has_suffix (name, "_1") ? 12 : 13;
}
static int fake_property (Display *d, Window w, Atom a, Atom type, int fmt, int mode,
                          const unsigned char *data, int n)
{
    g_assert_cmpint (grab_depth, ==, 1);
    if (a >= 12)
    {
        g_clear_pointer (&canonical_properties[a - 12], g_array_unref);
        canonical_properties[a - 12] = g_array_sized_new (FALSE, FALSE, sizeof (unsigned long), n);
        g_array_append_vals (canonical_properties[a - 12], data, n);
    }
    else if (a == 10) published_compensation = *(unsigned long *) data;
    else
    {
        published_generation = *(unsigned long *) data;
        if (!(published_generation & 1))
            for (int i = 0; i < 2; i++)
            {
                XfceRRCrtc *target = g_ptr_array_index (fixture->crtcs, i);
                if (target->mode == None) continue;
                g_assert_cmpint (server[i].mode, ==, target->mode);
                g_assert_cmpint (server[i].x, ==, target->x);
                g_assert_cmpfloat (server[i].scalex, ==, XFixedToDouble (XDoubleToFixed (target->scalex)));
            }
    }
    return 1;
}
static int fake_get_property (Display *d, Window w, Atom a, long offset, long length,
                              Bool del, Atom type, Atom *actual, int *format,
                              unsigned long *count, unsigned long *remaining, unsigned char **data)
{
    *remaining = 0; *format = 32; *actual = XA_CARDINAL;
    if (a < 12)
    {
        *count = 1; *data = calloc (1, sizeof (unsigned long) + 1);
        *(unsigned long *) *data = a == 10 ? published_compensation : published_generation;
    }
    else if (canonical_properties[a - 12] != NULL)
    {
        GArray *words = canonical_properties[a - 12];
        *count = words->len; *data = g_memdup2 (words->data, words->len * sizeof (unsigned long));
    }
    else { *count = 0; *actual = None; *data = NULL; }
    return Success;
}
static XRRScreenResources *fake_resources (Display *d, Window w)
{
    XRRScreenResources *r = g_new0 (XRRScreenResources, 1);
    r->nmode = 2; r->modes = g_memdup2 (server_modes, sizeof (server_modes));
    r->ncrtc = 2; r->crtcs = g_new (RRCrtc, 2); r->crtcs[0] = 1; r->crtcs[1] = 2;
    r->noutput = 2; r->outputs = g_new (RROutput, 2); r->outputs[0] = 10; r->outputs[1] = 11;
    return r;
}
static void fake_free_resources (XRRScreenResources *r)
{ g_free (r->modes); g_free (r->crtcs); g_free (r->outputs); g_free (r); }
static XRROutputInfo *fake_output (Display *d, XRRScreenResources *r, RROutput id)
{
    gint i = id - 10;
    XRROutputInfo *o = g_new0 (XRROutputInfo, 1);
    o->name = g_strdup (i == 0 ? "eDP-1" : "HDMI-1"); o->connection = RR_Connected;
    o->mm_width = i == 0 ? 294 : 531; o->mm_height = i == 0 ? 166 : 299;
    o->crtc = None;
    for (gint c = 0; c < 2; c++)
        if (server[c].mode != None && server_outputs[c] == id) o->crtc = c + 1;
    o->nmode = 2; o->npreferred = 1; o->modes = g_new (RRMode, 2);
    o->modes[0] = 100; o->modes[1] = 101;
    return o;
}
static void fake_free_output (XRROutputInfo *o)
{ g_free (o->name); g_free (o->modes); g_free (o); }
static int fake_sync (Display *d, Bool discard) { g_assert_cmpint (grab_depth, ==, 1); return 1; }
static void fake_grab (GdkDisplay *d) { g_assert_cmpint (grab_depth++, ==, 0); }
static void fake_ungrab (GdkDisplay *d) { g_assert_cmpint (--grab_depth, ==, 0); }
static void fake_gdk_sync (GdkDisplay *d) { g_assert_cmpint (grab_depth, ==, 0); }
static void fake_void (GdkDisplay *d) { }
static gint fake_trap_pop (GdkDisplay *d) { return 0; }
static gint fake_dimension (void) { return -1; }
static XfconfChannel *fake_channel (XfceDisplaysHelper *h) { return NULL; }
static gint fake_get_int (XfconfChannel *c, const gchar *p, gint fallback)
{ GValue *v = g_hash_table_lookup (properties, p); return v && G_VALUE_HOLDS_INT (v) ? g_value_get_int (v) : fallback; }
static gboolean fake_get_bool (XfconfChannel *c, const gchar *p, gboolean fallback)
{ GValue *v = g_hash_table_lookup (properties, p); return v && G_VALUE_HOLDS_BOOLEAN (v) ? g_value_get_boolean (v) : fallback; }
static gchar *fake_get_string (XfconfChannel *c, const gchar *p, const gchar *fallback)
{ GValue *v = g_hash_table_lookup (properties, p); return g_strdup (v && G_VALUE_HOLDS_STRING (v) ? g_value_get_string (v) : fallback); }
static void value_free (gpointer p) { g_value_unset (p); g_free (p); }
static GValue *new_value (const gchar *p, GType type)
{ GValue *v = g_new0 (GValue, 1); g_value_init (v, type); g_hash_table_replace (properties, g_strdup (p), v); return v; }
static gboolean fake_set_int (XfconfChannel *c, const gchar *p, gint v)
{ g_value_set_int (new_value (p, G_TYPE_INT), v); return TRUE; }
static gboolean fake_set_bool (XfconfChannel *c, const gchar *p, gboolean v)
{ g_value_set_boolean (new_value (p, G_TYPE_BOOLEAN), v); return TRUE; }
static gboolean fake_set_string (XfconfChannel *c, const gchar *p, const gchar *v)
{ g_value_set_string (new_value (p, G_TYPE_STRING), v); return TRUE; }
static gboolean fake_reset (XfconfChannel *c, const gchar *p, gboolean recursive)
{
    GHashTableIter it; gpointer key;
    g_hash_table_iter_init (&it, properties);
    while (g_hash_table_iter_next (&it, &key, NULL))
        if (!strcmp (key, p) || (recursive && g_str_has_prefix (key, p) && ((gchar *) key)[strlen (p)] == '/'))
            g_hash_table_iter_remove (&it);
    return TRUE;
}
static GHashTable *fake_properties (XfconfChannel *c, const gchar *prefix)
{
    GHashTable *copy = g_hash_table_new (g_str_hash, g_str_equal);
    GHashTableIter it; gpointer k, v;
    g_hash_table_iter_init (&it, properties);
    while (g_hash_table_iter_next (&it, &k, &v))
        if (g_str_has_prefix (k, prefix)) g_hash_table_insert (copy, k, v);
    return copy;
}
static gboolean fake_randr_reload (XfceRandr *r)
{
    g_assert_false (published_generation & 1);
    g_assert_cmpint (published_compensation, ==, XDoubleToFixed (fixture->compensation));
    for (guint n = 0; n < 2; n++)
    {
        XRRCrtcInfo *info = fake_info (NULL, NULL, n + 1);
        r->scalex[n] = server[n].scalex / fixture->compensation;
        r->scaley[n] = server[n].scaley / fixture->compensation;
        xfce_global_scale_crtc_read (NULL, 1, n + 1, info, &server_transforms[n],
                                     fixture->compensation, &r->scalex[n], &r->scaley[n]);
        XRRFreeCrtcInfo (info);
    }
    return !fail_commit;
}
static void fake_randr_save (XfceRandr *r, const gchar *scheme, XfconfChannel *c, guint n)
{
    g_assert_cmpstr (scheme, ==, "Default");
    g_assert_true (fake_get_bool (NULL, GLOBAL_SCALE_PENDING, FALSE));
    commits++;
    gchar *key = g_strdup_printf ("/Default/%s/Scale", n == 0 ? "eDP-1" : "HDMI-1");
    g_value_set_double (new_value (key, G_TYPE_DOUBLE), r->scalex[n]);
    g_free (key);
}

static void setup (XfceDisplaysHelperX11 *h, int percent, int width)
{
    memset (h, 0, sizeof (*h)); fixture = h;
    h->xdisplay = (Display *) 1;
    static gdouble randr_scalex[2], randr_scaley[2];
    fixture_randr.noutput = 2; h->randr = &fixture_randr;
    fixture_randr.scalex = randr_scalex; fixture_randr.scaley = randr_scaley;
    randr_scalex[0] = randr_scalex[1] = randr_scaley[0] = randr_scaley[1] = 1.0;
    properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, value_free);
    h->crtcs = g_ptr_array_new_with_free_func ((GDestroyNotify) xfce_displays_helper_x11_free_crtc);
    h->outputs = g_ptr_array_new_with_free_func ((GDestroyNotify) xfce_displays_helper_x11_free_output);
    h->compensation = XFixedToDouble (XDoubleToFixed (xfce_global_scale_compensation (percent)));
    h->scale_percent = percent; h->managed_scale = TRUE; h->scale_initialized = TRUE; h->primary = 10;
    for (int i = 0; i < 2; i++)
    {
        XfceRRCrtc *crtc = g_new0 (XfceRRCrtc, 1);
        crtc->id = i + 1; crtc->mode = 100; crtc->width = width; crtc->height = 1080;
        crtc->rotation = crtc->rotations = RR_Rotate_0;
        crtc->x = round (i * width * h->compensation);
        crtc->scalex = crtc->scaley = h->compensation;
        crtc->noutput = 1; crtc->outputs = g_new (RROutput, 1); crtc->outputs[0] = 10 + i;
        g_ptr_array_add (h->crtcs, crtc); server[i] = *crtc;
        server_outputs[i] = 10 + i;
        server_transforms[i] = (XTransform) { .matrix = {
            { XDoubleToFixed (crtc->scalex), 0, 0 }, { 0, XDoubleToFixed (crtc->scaley), 0 }, { 0, 0, XDoubleToFixed (1.0) } } };
        XfceRROutput *output = g_new0 (XfceRROutput, 1);
        output->id = 10 + i; output->active = TRUE;
        output->info = fake_output (NULL, NULL, 10 + i);
        output->info->crtc = i + 1;
        g_ptr_array_add (h->outputs, output);
    }
    server_modes[0] = (XRRModeInfo) { .id = 100, .width = width, .height = 1080, .dotClock = 60, .hTotal = 1, .vTotal = 1 };
    server_modes[1] = (XRRModeInfo) { .id = 101, .width = 3840, .height = 2160, .dotClock = 60, .hTotal = 1, .vTotal = 1 };
    h->resources = fake_resources (NULL, 1);
    fake_set_int (NULL, GLOBAL_SCALE_PROP, percent);
    fake_set_string (NULL, ACTIVE_PROFILE, "work-profile");
    server_width = ceil (2 * width * h->compensation); server_height = ceil (1080 * h->compensation);
    server_primary = 10; configurations = fail_config = grab_depth = 0; max_width = 8192;
    commits = 0; fail_commit = FALSE;
    published_generation = 0; published_compensation = XDoubleToFixed (h->compensation);
}
static void teardown (XfceDisplaysHelperX11 *h)
{
    if (h->scale_timeout != 0)
        g_source_remove (h->scale_timeout);
    global_scale_snapshot_clear (h);
    g_ptr_array_unref (h->crtcs);
    g_ptr_array_unref (h->outputs); fake_free_resources (h->resources);
    for (guint i = 0; i < 2; i++)
        g_clear_pointer (&canonical_properties[i], g_array_unref);
    g_hash_table_unref (properties);
}
static void profile (const gchar *scheme, gboolean second_primary)
{
    for (int i = 0; i < 2; i++)
    {
        gchar *p = g_strdup_printf ("/%s/%s", scheme, i == 0 ? "eDP-1" : "HDMI-1");
        gchar *key;
        fake_set_string (NULL, p, "Monitor");
#define PUT(s, f, v) key = g_strconcat (p, s, NULL); f (NULL, key, v); g_free (key)
        PUT ("/Active", fake_set_bool, TRUE);
        PUT ("/Primary", fake_set_bool, second_primary ? i == 1 : i == 0);
        PUT ("/Resolution", fake_set_string, "1920x1080");
        PUT ("/RefreshRate", fake_set_int, 60);
        key = g_strconcat (p, "/RefreshRate", NULL);
        g_value_set_double (new_value (key, G_TYPE_DOUBLE), 60); g_free (key);
        PUT ("/Position/X", fake_set_int, i * 1920);
        PUT ("/Position/Y", fake_set_int, 0);
        g_free (p);
#undef PUT
    }
}
static void test_complete_rollback (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 125, 1920);
    global_scale_snapshot (&h); h.preview_restore_profile = TRUE;
    for (int i = 0; i < 2; i++)
    { XfceRRCrtc *c = g_ptr_array_index (h.crtcs, i); c->width = 3840; c->x = round (i * 3840 * h.compensation); c->changed = TRUE; }
    g_assert_true (global_scale_apply (&h, 200));
    g_assert_false (global_scale_apply (&h, 125)); /* old-scale/new-layout cannot fit */
    global_scale_revert (&h);
    g_assert_cmpint (h.scale_percent, ==, 125);
    g_assert_cmpint (server_width, ==, 6144);
    g_assert_cmpint (server[1].x, ==, 3072);
    g_assert_cmpint (server_primary, ==, 10);
    g_assert_cmpstr (g_value_get_string (g_hash_table_lookup (properties, ACTIVE_PROFILE)), ==, "work-profile");
    g_assert_false (published_generation & 1);
    teardown (&h);
}
static void test_failed_apply_rollback (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 100, 1920); global_scale_snapshot (&h);
    fail_config = 1;
    g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*Failed to configure CRTC*");
    g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*Failed to apply display settings*");
    g_assert_false (global_scale_apply (&h, 125));
    g_test_assert_expected_messages ();
    g_assert_true (published_generation & 1);
    g_assert_true (global_scale_restore (&h));
    g_assert_false (published_generation & 1);
    g_assert_cmpint (server_width, ==, 3840); g_assert_cmpfloat (server[0].scalex, ==, 1);
    teardown (&h);
}
static void test_primary_only (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 100, 1920); profile ("Temp", TRUE);
    xfce_displays_helper_x11_channel_apply ((XfceDisplaysHelper *) &h, "Temp");
    g_assert_cmpint (server_primary, ==, 11);
    g_assert_cmpint (configurations, ==, 0);
    teardown (&h);
}
static void test_startup_partial (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 125, 1920); profile ("Default", FALSE);
    fake_reset (NULL, "/Default/HDMI-1", TRUE); h.initializing = TRUE;
    xfce_displays_helper_x11_channel_apply ((XfceDisplaysHelper *) &h, "Default");
    h.initializing = FALSE;
    g_assert_true (global_scale_apply (&h, 125));
    g_assert_cmpfloat (server[1].scalex, ==, h.compensation);
    g_assert_cmpint (server[1].x, ==, 3072);
    g_assert_cmpint (server_width, ==, 6144);
    teardown (&h);
}
static void test_automatic_topology (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 100, 1920);
    ((XfceRRCrtc *) g_ptr_array_index (h.crtcs, 0))->mode = None;
    ((XfceRROutput *) g_ptr_array_index (h.outputs, 1))->info->crtc = None;
    h.primary = 11;
    g_assert_cmpint (global_scale_resolve (&h, 0), ==, 100);
    ((XfceRRCrtc *) g_ptr_array_index (h.crtcs, 0))->mode = 100;
    h.primary = 10;
    g_assert_cmpint (global_scale_resolve (&h, 0), ==, 125);
    teardown (&h);
}
static void test_normalized_extents (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 100, 1920);
    for (int sign = -1; sign <= 1; sign += 2)
    {
        for (int i = 0; i < 2; i++)
        { XfceRRCrtc *c = g_ptr_array_index (h.crtcs, i); c->x = sign * 4000 + i * 1920; c->changed = TRUE; }
        g_assert_true (global_scale_apply (&h, 125));
        g_assert_cmpint (server_width, ==, 6144);
        g_assert_true (global_scale_apply (&h, 100));
    }
    max_width = 5000;
    g_assert_false (global_scale_apply (&h, 125));
    g_assert_cmpfloat (h.compensation, ==, 1);
    g_assert_cmpint (((XfceRRCrtc *) g_ptr_array_index (h.crtcs, 1))->x, ==, 1920);
    teardown (&h);
}
static void test_legacy_choice (void)
{
    g_assert_false (xfce_global_scale_choice_changed (-1, FALSE, 100));
    g_assert_true (xfce_global_scale_choice_changed (100, FALSE, 100));
    g_assert_false (xfce_global_scale_choice_changed (100, TRUE, 100));
}
static void test_fractional_round_trips (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 100, 1920);
    const gint choices[] = { 125, 150, 175, 200, 100 };
    for (guint pass = 0; pass < 10; pass++)
        for (guint n = 0; n < G_N_ELEMENTS (choices); n++)
        {
            g_assert_true (global_scale_apply (&h, choices[n]));
            if (choices[n] == 100)
            {
                g_assert_cmpint (server_width, ==, 3840);
                g_assert_cmpint (server[1].x, ==, 1920);
                g_assert_cmpfloat (server[0].scalex, ==, 1);
                g_assert_cmpfloat (server[1].scalex, ==, 1);
            }
        }
    teardown (&h);
}
static void test_output_pixel_scale_composition (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 100, 1920);
    for (int i = 0; i < 2; i++)
    {
        XfceRRCrtc *c = g_ptr_array_index (h.crtcs, i);
        c->output_scalex = c->output_scaley = 1.25;
        c->scalex = c->scaley = 1.25; c->x = i * 2400;
        server[i] = *c;
    }
    server_width = 4800; server_height = 1350;
    const gint choices[] = { 125, 150, 175, 200, 100 };
    for (guint pass = 0; pass < 10; pass++)
        for (guint n = 0; n < G_N_ELEMENTS (choices); n++)
            g_assert_true (global_scale_apply (&h, choices[n]));
    g_assert_cmpint (server_width, ==, 4800);
    g_assert_cmpint (server[1].x, ==, 2400);
    g_assert_cmpfloat (server[0].scalex, ==, 1.25);
    g_assert_cmpfloat (server[1].scalex, ==, 1.25);
    teardown (&h);
}
static void request (XfceDisplaysHelperX11 *h, gint choice)
{
    GValue value = G_VALUE_INIT; g_value_init (&value, G_TYPE_INT);
    g_value_set_int (&value, choice);
    global_scale_property_changed (NULL, GLOBAL_SCALE_REQUEST, &value, h);
    g_value_unset (&value);
}
static void answer (XfceDisplaysHelperX11 *h, gboolean keep)
{
    GValue value = G_VALUE_INIT; g_value_init (&value, G_TYPE_BOOLEAN);
    g_value_set_boolean (&value, TRUE);
    global_scale_property_changed (NULL, keep ? GLOBAL_SCALE_CONFIRM : GLOBAL_SCALE_CANCEL, &value, h);
    g_value_unset (&value);
}
static void test_staged_frontend_exit (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 125, 1920); profile ("Temp", TRUE);
    fake_set_bool (NULL, GLOBAL_SCALE_RESTORE_PROFILE, TRUE);
    xfce_displays_helper_x11_channel_apply ((XfceDisplaysHelper *) &h, "Temp");
    g_assert_nonnull (h.preview_crtcs); g_assert_cmpint (h.scale_timeout, !=, 0);
    g_assert_cmpint (server_primary, ==, 10);
    g_assert_cmpint (configurations, ==, 0);
    GSource *timeout = g_main_context_find_source_by_id (NULL, h.scale_timeout);
    g_source_set_ready_time (timeout, 0);
    while (h.scale_timeout != 0)
        g_main_context_iteration (NULL, TRUE);
    g_assert_cmpint (server_primary, ==, 10);
    g_assert_cmpint (fake_get_int (NULL, GLOBAL_SCALE_PROP, -1), ==, 125);
    g_assert_null (g_hash_table_lookup (properties, "/Temp/eDP-1"));
    g_assert_null (g_hash_table_lookup (properties, GLOBAL_SCALE_RESTORE_PROFILE));
    g_assert_cmpint (configurations, ==, 0);
    teardown (&h);
}
static void test_combined_cancel_and_confirm (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 100, 1920); profile ("Temp", TRUE);
    fake_set_bool (NULL, GLOBAL_SCALE_RESTORE_PROFILE, TRUE);
    xfce_displays_helper_x11_channel_apply ((XfceDisplaysHelper *) &h, "Temp");
    request (&h, 125);
    g_assert_true (fake_get_bool (NULL, GLOBAL_SCALE_PENDING, FALSE));
    answer (&h, FALSE);
    g_assert_cmpint (server_primary, ==, 10); g_assert_cmpint (server_width, ==, 3840);
    g_assert_false (fake_get_bool (NULL, GLOBAL_SCALE_PENDING, TRUE));
    profile ("Temp", TRUE); fake_set_bool (NULL, GLOBAL_SCALE_RESTORE_PROFILE, TRUE);
    xfce_displays_helper_x11_channel_apply ((XfceDisplaysHelper *) &h, "Temp");
    request (&h, 125); answer (&h, TRUE);
    g_assert_cmpint (commits, ==, 2);
    g_assert_cmpint (server_primary, ==, 11); g_assert_cmpint (server_width, ==, 6144);
    g_assert_cmpint (fake_get_int (NULL, GLOBAL_SCALE_PROP, -1), ==, 125);
    g_assert_null (h.preview_crtcs); g_assert_cmpint (h.scale_timeout, ==, 0);
    g_assert_cmpstr (g_value_get_string (g_hash_table_lookup (properties, ACTIVE_PROFILE)), ==, "Default");
    teardown (&h);
}
static void test_unmanaged_rollback (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 100, 1920);
    h.managed_scale = FALSE; fake_reset (NULL, GLOBAL_SCALE_PROP, TRUE);
    request (&h, 125);
    g_assert_cmpint (fake_get_int (NULL, GLOBAL_SCALE_TOOLKIT, -1), ==, 2);
    answer (&h, FALSE);
    g_assert_false (h.managed_scale);
    g_assert_cmpint (fake_get_int (NULL, GLOBAL_SCALE_TOOLKIT, -1), ==, 0);
    g_assert_null (g_hash_table_lookup (properties, GLOBAL_SCALE_PROP));
    request (&h, 100); answer (&h, TRUE);
    g_assert_true (h.managed_scale);
    g_assert_cmpint (fake_get_int (NULL, GLOBAL_SCALE_TOOLKIT, -1), ==, 1);
    teardown (&h);
}
static void test_rollback_failure_reported (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 100, 1920); request (&h, 125);
    fail_config = 1;
    g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*Failed to configure CRTC*");
    g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*Failed to apply display settings*");
    answer (&h, FALSE);
    g_test_assert_expected_messages ();
    g_assert_cmpstr (g_value_get_string (g_hash_table_lookup (properties, GLOBAL_SCALE_ERROR)), ==, "rollback-failed");
    g_assert_true (published_generation & 1);
    teardown (&h);
}
static void test_failed_confirmation_keeps_profile (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 100, 1920); profile ("Temp", TRUE);
    fake_set_bool (NULL, GLOBAL_SCALE_RESTORE_PROFILE, TRUE);
    xfce_displays_helper_x11_channel_apply ((XfceDisplaysHelper *) &h, "Temp");
    request (&h, 125); fail_commit = TRUE; answer (&h, TRUE);
    g_assert_cmpint (commits, ==, 0);
    g_assert_cmpint (fake_get_int (NULL, GLOBAL_SCALE_PROP, -1), ==, 100);
    g_assert_cmpint (server_width, ==, 3840); g_assert_cmpint (server_primary, ==, 10);
    g_assert_false (fake_get_bool (NULL, GLOBAL_SCALE_PENDING, TRUE));
    teardown (&h);
}
static void stage_large_layout (XfceDisplaysHelperX11 *h)
{
    gint before = configurations;
    profile ("Temp", TRUE);
    for (gint n = 0; n < 2; n++)
    {
        gchar *key = g_strdup_printf ("/Temp/%s/Resolution", n == 0 ? "eDP-1" : "HDMI-1");
        fake_set_string (NULL, key, "3840x2160"); g_free (key);
        key = g_strdup_printf ("/Temp/%s/Position/X", n == 0 ? "eDP-1" : "HDMI-1");
        fake_set_int (NULL, key, n * 3840); g_free (key);
    }
    fake_set_bool (NULL, GLOBAL_SCALE_RESTORE_PROFILE, TRUE);
    xfce_displays_helper_x11_channel_apply ((XfceDisplaysHelper *) h, "Temp");
    g_assert_true (h->preview_staged); g_assert_false (h->preview_failed);
    g_assert_cmpint (configurations, ==, before);
    g_assert_cmpint (server_width, ==, 6144); g_assert_cmpint (server_primary, ==, 10);
    g_assert_cmpstr (g_value_get_string (g_hash_table_lookup (properties, ACTIVE_PROFILE)), ==, "work-profile");
}
static void test_production_combined_large_layout (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 125, 1920);
    stage_large_layout (&h);
    /* Exercise the production reload entry point between the two xfconf
     * signals: a pending RandR event must not erase the in-memory Temp. */
    xfce_displays_helper_x11_reload (&h);
    request (&h, 200);
    g_assert_true (fake_get_bool (NULL, GLOBAL_SCALE_PENDING, FALSE));
    g_assert_cmpint (server_width, ==, 7680); g_assert_cmpint (server_height, ==, 2160);
    g_assert_cmpint (server_primary, ==, 11); g_assert_cmpint (h.scale_percent, ==, 200);
    g_assert_cmpstr (g_value_get_string (g_hash_table_lookup (properties, ACTIVE_PROFILE)), ==, "Temp");
    answer (&h, FALSE);
    g_assert_cmpint (server_width, ==, 6144); g_assert_cmpint (server[1].x, ==, 3072);
    g_assert_cmpint (h.scale_percent, ==, 125);
    stage_large_layout (&h); request (&h, 200); answer (&h, TRUE);
    g_assert_cmpint (fake_get_int (NULL, GLOBAL_SCALE_PROP, -1), ==, 200);
    g_assert_cmpint (server_width, ==, 7680); g_assert_cmpint (commits, ==, 2);
    teardown (&h);
}
static void test_large_staged_frontend_exit (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 125, 1920);
    stage_large_layout (&h);
    GSource *timeout = g_main_context_find_source_by_id (NULL, h.scale_timeout);
    g_source_set_ready_time (timeout, 0);
    while (h.scale_timeout != 0)
        g_main_context_iteration (NULL, TRUE);
    g_assert_cmpint (configurations, ==, 0);
    g_assert_cmpint (server_width, ==, 6144); g_assert_cmpint (server_primary, ==, 10);
    g_assert_null (h.staged_crtcs); g_assert_null (h.preview_crtcs);
    g_assert_null (g_hash_table_lookup (properties, "/Temp/eDP-1"));
    g_assert_cmpstr (g_value_get_string (g_hash_table_lookup (properties, ACTIVE_PROFILE)), ==, "work-profile");
    teardown (&h);
}
static void set_output_pixel_scale (XfceDisplaysHelperX11 *h, gdouble scale)
{
    for (gint n = 0; n < 2; n++)
    {
        XfceRRCrtc *c = g_ptr_array_index (h->crtcs, n);
        c->scalex = c->scaley = c->output_scalex = c->output_scaley = scale;
        c->x = round (n * 1920 * scale); server[n] = *c;
        server_transforms[n] = (XTransform) { .matrix = {
            { XDoubleToFixed (scale), 0, 0 }, { 0, XDoubleToFixed (scale), 0 }, { 0, 0, XDoubleToFixed (1.0) } } };
    }
    server_width = ceil (3840 * scale); server_height = ceil (1080 * scale);
}
static void test_reload_cycle_and_profile_commit (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 100, 1920); set_output_pixel_scale (&h, 1.25);
    const gint choices[] = { 125, 150, 175, 200, 100 };
    for (guint pass = 0; pass < 10; pass++)
        for (guint n = 0; n < G_N_ELEMENTS (choices); n++)
        {
            request (&h, choices[n]);
            g_assert_true (fake_get_bool (NULL, GLOBAL_SCALE_PENDING, FALSE));
            xfce_displays_helper_x11_reload (&h); /* actual cache destruction/list_crtcs */
            for (guint c = 0; c < 2; c++)
            {
                XfceRRCrtc *crtc = g_ptr_array_index (h.crtcs, c);
                g_assert_cmpfloat (crtc->output_scalex, ==, 1.25);
                g_assert_cmpfloat (crtc->output_scaley, ==, 1.25);
            }
            /* Exercise the production Confirm profile path after reload. */
            h.preview_restore_profile = TRUE;
            answer (&h, TRUE);
            g_assert_cmpfloat (g_value_get_double (g_hash_table_lookup (properties, "/Default/eDP-1/Scale")), ==, 1.25);
            g_assert_cmpfloat (g_value_get_double (g_hash_table_lookup (properties, "/Default/HDMI-1/Scale")), ==, 1.25);
        }
    g_assert_cmpint (server_width, ==, 4800); g_assert_cmpint (server[1].x, ==, 2400);
    g_assert_cmpfloat (server[0].scalex, ==, 1.25); g_assert_cmpfloat (server[1].scalex, ==, 1.25);
    g_assert_cmpint (commits, ==, 100);
    teardown (&h);
}
static void test_external_transform_and_assignment (void)
{
    XfceDisplaysHelperX11 h; setup (&h, 100, 1920); set_output_pixel_scale (&h, 1.25);
    g_assert_true (global_scale_apply (&h, 150));
    /* Geometry-only external changes are observed, while the unchanged
     * transform/output assignment still has a valid canonical scale. */
    server[0].mode = 101; server[0].width = 3840; server[0].height = 2160;
    xfce_displays_helper_x11_reload (&h);
    g_assert_cmpint (((XfceRRCrtc *) g_ptr_array_index (h.crtcs, 0))->mode, ==, 101);
    g_assert_cmpfloat (((XfceRRCrtc *) g_ptr_array_index (h.crtcs, 0))->output_scalex, ==, 1.25);
    server[0].mode = 100; server[0].width = 1920; server[0].height = 1080;
    /* A real external transform differs from the recorded server proof. */
    server_transforms[0].matrix[0][0] += 100;
    server[0].scalex = XFixedToDouble (server_transforms[0].matrix[0][0]);
    xfce_displays_helper_x11_reload (&h);
    g_assert_cmpfloat (((XfceRRCrtc *) g_ptr_array_index (h.crtcs, 0))->output_scalex, ==, 0);
    gdouble external = server[0].scalex / h.compensation;
    g_assert_true (global_scale_apply (&h, 100));
    g_assert_cmpfloat_with_epsilon (server[0].scalex, external, 1.0 / 65536);
    /* Even identical diagonal factors do not hide changes to another matrix
     * element or to the output set assigned to a CRTC. */
    server_transforms[1].matrix[0][2] = XDoubleToFixed (3.0);
    xfce_displays_helper_x11_reload (&h);
    g_assert_cmpfloat (((XfceRRCrtc *) g_ptr_array_index (h.crtcs, 1))->output_scalex, ==, 0);
    server_transforms[1].matrix[0][2] = 0;
    server_outputs[0] = 11; server_outputs[1] = 10;
    xfce_displays_helper_x11_reload (&h);
    g_assert_cmpfloat (((XfceRRCrtc *) g_ptr_array_index (h.crtcs, 0))->output_scalex, ==, 0);
    g_assert_cmpfloat (((XfceRRCrtc *) g_ptr_array_index (h.crtcs, 1))->output_scalex, ==, 0);
    teardown (&h);

    setup (&h, 100, 1920);
    g_assert_true (global_scale_apply (&h, 125));
    server_transforms[0].matrix[0][0] -= 1; /* genuine near-identity external change */
    server[0].scalex = XFixedToDouble (server_transforms[0].matrix[0][0]);
    xfce_displays_helper_x11_reload (&h);
    g_assert_cmpfloat (((XfceRRCrtc *) g_ptr_array_index (h.crtcs, 0))->output_scalex, ==, 0);
    g_assert_true (global_scale_apply (&h, 100));
    g_assert_cmpfloat (server[0].scalex, <, 1.0);
    teardown (&h);
}
int main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/transactions/complete-multimonitor-rollback", test_complete_rollback);
    g_test_add_func ("/transactions/partial-server-failure", test_failed_apply_rollback);
    g_test_add_func ("/transactions/primary-only", test_primary_only);
    g_test_add_func ("/transactions/startup-partial-profile", test_startup_partial);
    g_test_add_func ("/transactions/automatic-new-topology", test_automatic_topology);
    g_test_add_func ("/transactions/normalized-extents", test_normalized_extents);
    g_test_add_func ("/transactions/legacy-explicit-100", test_legacy_choice);
    g_test_add_func ("/transactions/frontend-exit-before-request", test_staged_frontend_exit);
    g_test_add_func ("/transactions/combined-cancel-and-confirm", test_combined_cancel_and_confirm);
    g_test_add_func ("/transactions/unmanaged-rollback", test_unmanaged_rollback);
    g_test_add_func ("/transactions/rollback-failure-reported", test_rollback_failure_reported);
    g_test_add_func ("/transactions/fractional-round-trips", test_fractional_round_trips);
    g_test_add_func ("/transactions/output-pixel-scale-composition", test_output_pixel_scale_composition);
    g_test_add_func ("/transactions/failed-confirmation-keeps-profile", test_failed_confirmation_keeps_profile);
    g_test_add_func ("/transactions/production-temp-request-large-layout", test_production_combined_large_layout);
    g_test_add_func ("/transactions/large-temp-frontend-exit", test_large_staged_frontend_exit);
    g_test_add_func ("/transactions/reload-cycle-profile-commit", test_reload_cycle_and_profile_commit);
    g_test_add_func ("/transactions/external-transform-and-assignment", test_external_transform_and_assignment);
    return g_test_run ();
}
