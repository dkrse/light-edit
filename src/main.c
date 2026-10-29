#include "settings.h"
#include "window.h"

#define APP_ID "io.github.krse.LightEdit"

static Win *get_window(GtkApplication *app) {
    Win *w = win_from_window(gtk_application_get_active_window(app));
    if (w) return w;
    w = win_create(app);
    win_restore_session(w);
    return w;
}

/* Installed: the icon comes from the icon theme. Run from the build tree:
   load data/<APP_ID>.svg relative to the executable (build/<type>/). */
static void set_icon(void) {
    gtk_window_set_default_icon_name(APP_ID);
    if (gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), APP_ID)) return;
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    if (!exe) return;
    char *dir = g_path_get_dirname(exe);
    char *svg = g_build_filename(dir, "..", "..", "data", APP_ID ".svg", NULL);
    gtk_window_set_default_icon_from_file(svg, NULL);
    g_free(svg);
    g_free(dir);
    g_free(exe);
}

static void on_startup(GApplication *app, gpointer data) {
    (void)app; (void)data;
    set_icon();
}

static void on_activate(GtkApplication *app, gpointer data) {
    (void)data;
    Win *w = get_window(app);
    if (!win_current(w)) win_new_doc(w);
    gtk_window_present(GTK_WINDOW(w->window));
}

/* Also receives files from a second `light-edit file` launch, which
   GApplication forwards to the running instance. */
static void on_open(GApplication *gapp, GFile **files, gint n, const gchar *hint,
                    gpointer data) {
    (void)hint; (void)data;
    GtkApplication *app = GTK_APPLICATION(gapp);
    Win *w = get_window(app);
    for (gint i = 0; i < n; i++) {
        char *path = g_file_get_path(files[i]);
        if (path) win_open_path(w, path);
        g_free(path);
    }
    if (!win_current(w)) win_new_doc(w);
    gtk_window_present(GTK_WINDOW(w->window));
}

int main(int argc, char **argv) {
    settings_load();
    GtkApplication *app = gtk_application_new(APP_ID,
                                              G_APPLICATION_HANDLES_OPEN);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "open", G_CALLBACK(on_open), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
