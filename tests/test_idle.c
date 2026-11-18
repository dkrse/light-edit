/* Regression tests: an idle editor must not burn CPU.

   Plain text used to set no lexer at all (container lexing). With
   SC_IDLESTYLING_ALL Scintilla then kept asking for styling in an idle
   callback that never finished: 100 % of one core while doing nothing.

   Needs a display (X11 or Wayland); exits 77 (skipped) without one. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib/gstdio.h>

#include "app.h"
#include "lang.h"
#include "settings.h"

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
                   fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } \
} while (0)

static gboolean quit_cb(gpointer loop) {
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

/* Runs the main loop for ms milliseconds, returns CPU seconds used. */
static double run_loop_cpu(int ms) {
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(ms, quit_cb, loop);
    clock_t c0 = clock();
    g_main_loop_run(loop);
    clock_t c1 = clock();
    g_main_loop_unref(loop);
    return (double)(c1 - c0) / CLOCKS_PER_SEC;
}

/* A Doc with just what lang_apply_for_path needs, shown in a real window
   so Scintilla paints and schedules idle styling like in the app. */
static Doc *make_doc(GtkWidget **win_out) {
    Doc *d = g_new0(Doc, 1);
    d->sci = scintilla_new();
    sci_send(d, SCI_SETLAYOUTCACHE, SC_CACHE_PAGE, 0);
    sci_send(d, SCI_SETIDLESTYLING, SC_IDLESTYLING_ALL, 0);
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(win), 600, 400);
    gtk_container_add(GTK_CONTAINER(win), d->sci);
    gtk_widget_show_all(win);
    *win_out = win;
    return d;
}

static void fill(Doc *d) {
    GString *s = g_string_new(NULL);
    for (int i = 0; i < 5000; i++)
        g_string_append_printf(s, "line %d with some plain words in it\n", i);
    sci_send(d, SCI_SETTEXT, 0, (sptr_t)s->str);
    g_string_free(s, TRUE);
}

static void test_idle(const char *path, const char *want_lexer, const char *want_lang) {
    GtkWidget *win;
    Doc *d = make_doc(&win);
    lang_apply_for_path(d, path);
    fill(d);

    CHECK(strcmp(d->lang, want_lang) == 0, "%s: lang %s, want %s", path, d->lang, want_lang);

    char name[64] = "";
    sci_send(d, SCI_GETLEXERLANGUAGE, 0, (sptr_t)name);
    CHECK(strcmp(name, want_lexer) == 0, "%s: lexer \"%s\", want \"%s\"", path, name, want_lexer);

    run_loop_cpu(1000);                 /* let startup and idle styling finish */
    double cpu = run_loop_cpu(2000);    /* now it must be quiet */
    CHECK(cpu < 0.1, "%s: idle editor used %.2f s CPU in 2 s", path, cpu);

    sptr_t len = sci_send(d, SCI_GETLENGTH, 0, 0);
    sptr_t styled = sci_send(d, SCI_GETENDSTYLED, 0, 0);
    CHECK(styled == len, "%s: styled to %ld of %ld", path, (long)styled, (long)len);

    printf("%-12s lexer=%-5s cpu=%.3f s / 2 s\n", path, name, cpu);
    gtk_widget_destroy(win);
    g_free(d);
}

int main(int argc, char **argv) {
    /* Defaults only: don't read the user's settings file. Set before GTK
       starts, GLib caches the config dir on first use. */
    char *tmp = g_dir_make_tmp("light-edit-test-XXXXXX", NULL);
    g_setenv("XDG_CONFIG_HOME", tmp, TRUE);
    if (!gtk_init_check(&argc, &argv)) {
        fprintf(stderr, "SKIP: no display\n");
        return 77;
    }
    settings_load();

    test_idle("notes.txt", "null", "Plain Text");
    test_idle("README", "null", "Plain Text");
    test_idle("main.c", "cpp", "C");

    g_rmdir(tmp);
    g_free(tmp);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
