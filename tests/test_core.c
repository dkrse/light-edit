/* Unit and regression tests for file I/O, settings, language detection,
   search and window teardown.

   Needs a display (X11 or Wayland); exits 77 (skipped) without one. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <glib/gstdio.h>

#include "app.h"
#include "doc.h"
#include "fileio.h"
#include "lang.h"
#include "search.h"
#include "settings.h"
#include "window.h"

static int failures;
static char *tmpdir;

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
                   fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } \
} while (0)

static char *tmp_file(const char *name, const char *data, gssize len) {
    char *p = g_build_filename(tmpdir, name, NULL);
    if (data) g_file_set_contents(p, data, len, NULL);
    return p;
}

static Doc *bare_doc(void) {
    Doc *d = g_new0(Doc, 1);
    d->sci = scintilla_new();
    g_object_ref_sink(d->sci);
    d->matches = g_array_new(FALSE, FALSE, sizeof(sptr_t));
    return d;
}

static void bare_free(Doc *d) {
    gtk_widget_destroy(d->sci);
    g_object_unref(d->sci);
    g_array_free(d->matches, TRUE);
    g_free(d);
}

static char *doc_text(Doc *d) {
    sptr_t len = sci_send(d, SCI_GETLENGTH, 0, 0);
    char *buf = g_malloc((size_t)len + 1);
    sci_send(d, SCI_GETTEXT, (uptr_t)len + 1, (sptr_t)buf);
    return buf;
}

static void pump(void) {
    while (gtk_events_pending()) gtk_main_iteration();
}

/* ── fileio ── */

static void test_load_basic(void) {
    Doc *d = bare_doc();
    char *p = tmp_file("a.txt", "hello\nworld\n", -1), *err = NULL;
    CHECK(fileio_load(d, p, &err), "load: %s", err);
    char *t = doc_text(d);
    CHECK(strcmp(t, "hello\nworld\n") == 0, "text \"%s\"", t);
    CHECK(sci_send(d, SCI_GETEOLMODE, 0, 0) == SC_EOL_LF, "LF mode");
    CHECK(!sci_send(d, SCI_GETMODIFY, 0, 0), "not modified after load");
    CHECK(!sci_send(d, SCI_CANUNDO, 0, 0), "no undo after load");
    CHECK(d->on_disk && d->size == 12, "on_disk/size");
    g_free(t); g_free(p); bare_free(d);
}

static void test_load_eol_and_bom(void) {
    struct { const char *data; int mode; } cases[] = {
        { "a\r\nb\r\n", SC_EOL_CRLF }, { "a\rb\r", SC_EOL_CR }, { "no newline", SC_EOL_LF },
        { "", SC_EOL_LF }, { "x\r", SC_EOL_CR },
    };
    for (size_t i = 0; i < G_N_ELEMENTS(cases); i++) {
        Doc *d = bare_doc();
        char *p = tmp_file("eol.txt", cases[i].data, -1), *err = NULL;
        CHECK(fileio_load(d, p, &err), "load: %s", err);
        CHECK(sci_send(d, SCI_GETEOLMODE, 0, 0) == cases[i].mode, "case %zu: eol mode", i);
        g_free(p); bare_free(d);
    }

    Doc *d = bare_doc();
    char *p = tmp_file("bom.txt", "\xEF\xBB\xBFtext", -1), *err = NULL;
    CHECK(fileio_load(d, p, &err), "bom load: %s", err);
    char *t = doc_text(d);
    CHECK(strcmp(t, "text") == 0, "BOM dropped, got \"%s\"", t);
    g_free(t); g_free(p); bare_free(d);
}

static void test_load_errors(void) {
    Doc *d = bare_doc();
    char *err = NULL;
    char *p = tmp_file("bad.txt", "ok\xff\xfe", 4);
    CHECK(!fileio_load(d, p, &err) && err && strstr(err, "UTF-8"), "invalid UTF-8 rejected");
    CHECK(sci_send(d, SCI_GETLENGTH, 0, 0) == 0, "doc empty after failure");
    g_free(err); err = NULL; g_free(p);

    p = tmp_file("trunc.txt", "ab\xc3", 3);                 /* cut-off sequence at EOF */
    CHECK(!fileio_load(d, p, &err), "truncated UTF-8 at EOF rejected");
    g_free(err); err = NULL; g_free(p);

    CHECK(!fileio_load(d, tmpdir, &err) && err, "directory rejected");
    g_free(err); err = NULL;

    p = g_build_filename(tmpdir, "missing.txt", NULL);
    CHECK(!fileio_load(d, p, &err) && err, "missing file rejected");
    g_free(err); g_free(p);
    bare_free(d);
}

/* A multi-byte character split across the 1 MiB read chunks must survive. */
static void test_load_chunk_boundary(void) {
    const size_t chunk = 1024 * 1024;
    GString *s = g_string_new(NULL);
    while (s->len < chunk - 1) g_string_append_c(s, 'a');
    g_string_append(s, "\xc5\xa1");                         /* š straddles the boundary */
    for (int i = 0; i < 1000; i++) g_string_append(s, "\xe2\x82\xac");  /* € */
    char *p = tmp_file("big.txt", s->str, (gssize)s->len), *err = NULL;
    Doc *d = bare_doc();
    CHECK(fileio_load(d, p, &err), "chunked load: %s", err);
    CHECK((size_t)sci_send(d, SCI_GETLENGTH, 0, 0) == s->len, "length kept");
    char *t = doc_text(d);
    CHECK(memcmp(t, s->str, s->len) == 0, "content kept");
    g_free(t); g_free(p); bare_free(d);
    g_string_free(s, TRUE);
}

static void test_save(void) {
    Doc *d = bare_doc();
    char *p = tmp_file("save.txt", "old", -1), *err = NULL;
    g_chmod(p, 0640);
    sci_send(d, SCI_SETTEXT, 0, (sptr_t)"new content\n");
    CHECK(fileio_save(d, p, &err), "save: %s", err);
    char *back = NULL;
    g_file_get_contents(p, &back, NULL, NULL);
    CHECK(back && strcmp(back, "new content\n") == 0, "saved content");
    struct stat st;
    CHECK(stat(p, &st) == 0 && (st.st_mode & 0777) == 0640, "permissions kept");
    CHECK(!sci_send(d, SCI_GETMODIFY, 0, 0), "save point set");
    CHECK(d->on_disk && d->size == 12, "stat updated");
    g_free(back);

    /* Through a symlink: the target is replaced, the link stays a link. */
    char *link = g_build_filename(tmpdir, "link.txt", NULL);
    CHECK(symlink(p, link) == 0, "symlink");
    sci_send(d, SCI_SETTEXT, 0, (sptr_t)"via link");
    CHECK(fileio_save(d, link, &err), "save via link: %s", err);
    CHECK(g_file_test(link, G_FILE_TEST_IS_SYMLINK), "link kept");
    g_file_get_contents(p, &back, NULL, NULL);
    CHECK(back && strcmp(back, "via link") == 0, "target written");
    g_free(back);
    g_unlink(link); g_free(link);

    /* No leftover temporary files. */
    GDir *dir = g_dir_open(tmpdir, 0, NULL);
    const char *n;
    while ((n = g_dir_read_name(dir))) CHECK(n[0] != '.', "temp file left: %s", n);
    g_dir_close(dir);

    char *bad = g_build_filename(tmpdir, "nodir", "x.txt", NULL);
    CHECK(!fileio_save(d, bad, &err) && err, "save into missing dir fails");
    g_free(err); g_free(bad);
    g_free(p); bare_free(d);
}

/* ── settings ── */

static void test_settings_roundtrip(void) {
    settings_load();
    CHECK(!settings.show_eol && !settings.show_whitespace, "defaults off");
    settings.show_eol = TRUE;
    settings.tab_width = 7;
    settings.font_size = 13.5;
    settings_save();
    settings.show_eol = FALSE;
    settings.tab_width = 4;
    settings_load();
    CHECK(settings.show_eol, "show_eol persisted");
    CHECK(settings.tab_width == 7, "tab_width persisted");
    CHECK(settings.font_size == 13.5, "font_size persisted");

    char *path = g_build_filename(g_get_user_config_dir(), "light-edit", "settings.ini", NULL);
    g_file_set_contents(path, "[editor]\ntab_width=999\nfont_size=1\ntheme=nope\n", -1, NULL);
    settings_load();
    CHECK(settings.tab_width == 16, "tab_width clamped, got %d", settings.tab_width);
    CHECK(settings.font_size == 11.0, "bad font size ignored");
    CHECK(settings.theme >= 0 && settings.theme < theme_count, "unknown theme falls back");
    g_unlink(path);
    g_free(path);
    settings_load();
}

/* ── languages ── */

static void test_lang(void) {
    struct { const char *path, *lang; } cases[] = {
        { "/x/main.c", "C" }, { "/x/MAIN.C", "C" }, { "/x/notes.txt", "Plain Text" },
        { "/x/.hidden", "Plain Text" }, { "/x/Makefile", NULL }, { NULL, "Plain Text" },
    };
    for (size_t i = 0; i < G_N_ELEMENTS(cases); i++) {
        Doc *d = bare_doc();
        lang_apply_for_path(d, cases[i].path);
        if (cases[i].lang)
            CHECK(strcmp(d->lang, cases[i].lang) == 0, "%s: lang %s, want %s",
                  cases[i].path ? cases[i].path : "(null)", d->lang, cases[i].lang);
        else
            CHECK(strcmp(d->lang, "Plain Text") != 0, "%s: should have a lexer", cases[i].path);
        char name[64] = "";
        sci_send(d, SCI_GETLEXERLANGUAGE, 0, (sptr_t)name);
        CHECK(name[0], "%s: a lexer is always set (never container lexing)",
              cases[i].path ? cases[i].path : "(null)");
        bare_free(d);
    }

    /* Line-end display: whitespace mode uses Scintilla's marks, the arrow mode not. */
    Doc *d = bare_doc();
    settings.show_whitespace = TRUE; settings.show_eol = FALSE;
    lang_apply_base(d);
    CHECK(sci_send(d, SCI_GETVIEWEOL, 0, 0), "whitespace shows EOL");
    settings.show_whitespace = FALSE; settings.show_eol = TRUE;
    lang_apply_base(d);
    CHECK(!sci_send(d, SCI_GETVIEWEOL, 0, 0), "arrow mode hides Scintilla EOL");
    settings.show_eol = FALSE;
    bare_free(d);
}

/* ── window, documents and search ── */

static Win *make_win(GtkApplication *app) {
    Win *w = win_create(app);
    win_new_doc(w);
    pump();
    return w;
}

static void test_window_docs(GtkApplication *app) {
    Win *w = make_win(app);
    Doc *d = win_current(w);
    CHECK(d && doc_is_pristine(d), "fresh pristine doc");

    char *p = tmp_file("open.txt", "one\ntwo\n", -1);
    win_open_path(w, p);
    CHECK(gtk_notebook_get_n_pages(w->nb) == 1, "pristine tab reused");
    CHECK(win_current(w)->path && strcmp(g_path_get_basename(win_current(w)->path), "open.txt") == 0,
          "file opened");
    win_open_path(w, p);
    CHECK(gtk_notebook_get_n_pages(w->nb) == 1, "same file not opened twice");

    win_new_doc(w);
    CHECK(gtk_notebook_get_n_pages(w->nb) == 2, "new tab");
    CHECK(win_close_doc(w, win_current(w)), "close clean tab");
    CHECK(gtk_notebook_get_n_pages(w->nb) == 1, "one tab left");
    CHECK(win_close_doc(w, win_current(w)), "close last tab");
    CHECK(gtk_notebook_get_n_pages(w->nb) == 1, "empty tab recreated");

    /* Reload keeps the text when the file turns invalid. */
    win_open_path(w, p);
    d = win_current(w);
    g_file_set_contents(p, "bad\xff", 4, NULL);
    char *err = NULL;
    CHECK(!doc_reload(d, &err), "reload of invalid file fails");
    g_free(err);
    char *t = doc_text(d);
    CHECK(strcmp(t, "one\ntwo\n") == 0, "text kept after failed reload: \"%s\"", t);
    g_free(t);
    g_file_set_contents(p, "fresh\n", -1, NULL);
    CHECK(doc_reload(d, &err), "reload: %s", err);
    t = doc_text(d);
    CHECK(strcmp(t, "fresh\n") == 0, "reloaded text");
    g_free(t);

    gtk_widget_destroy(w->window);
    pump();
    g_free(p);
}

static void test_search(GtkApplication *app) {
    Win *w = make_win(app);
    Doc *d = win_current(w);
    sci_send(d, SCI_SETTEXT, 0, (sptr_t)"foo bar foo\nbaz foo\n");

    /* Multiple selections: only the main one may prefill the entry (the old
       code copied all of them into a buffer sized for one). */
    sci_send(d, SCI_SETSELECTION, 0, 3);              /* "foo" */
    sci_send(d, SCI_ADDSELECTION, 12, 19);            /* "baz foo" */
    sci_send(d, SCI_SETMAINSELECTION, 0, 0);
    search_show(w->search, FALSE);
    pump();
    CHECK(search_visible(w->search), "search bar shown");
    CHECK(d->matches->len == 3, "3 matches of foo, got %u", d->matches->len);

    sci_send(d, SCI_SETEMPTYSELECTION, 0, 0);
    search_next(w->search, TRUE);
    CHECK(sci_send(d, SCI_GETSELECTIONSTART, 0, 0) == 0, "first match");
    search_next(w->search, TRUE);
    CHECK(sci_send(d, SCI_GETSELECTIONSTART, 0, 0) == 8, "second match");
    search_next(w->search, TRUE);
    search_next(w->search, TRUE);
    CHECK(sci_send(d, SCI_GETSELECTIONSTART, 0, 0) == 0, "wraps around");
    search_next(w->search, FALSE);
    CHECK(sci_send(d, SCI_GETSELECTIONSTART, 0, 0) == 16, "backwards wraps");

    search_hide(w->search);
    CHECK(!search_visible(w->search) && d->matches->len == 0, "hidden and cleared");

    gtk_widget_destroy(w->window);
    pump();
}

/* Closing a window with several tabs, some never loaded, must not touch the
   freed window (switch-page and notifications fire during teardown). */
static void test_window_teardown(GtkApplication *app) {
    Win *w = make_win(app);
    for (int i = 0; i < 5; i++) {
        char name[32];
        g_snprintf(name, sizeof name, "t%d.c", i);
        char *p = tmp_file(name, "int x;\n", -1);
        win_open_path(w, p);
        g_free(p);
    }
    search_show(w->search, FALSE);
    gtk_notebook_set_current_page(w->nb, 2);
    pump();
    GtkWidget *win = w->window;
    CHECK(win_from_window(GTK_WINDOW(win)) == w, "win lookup");
    gtk_widget_destroy(win);
    pump();
    CHECK(TRUE, "teardown survived");
}

int main(int argc, char **argv) {
    tmpdir = g_dir_make_tmp("light-edit-test-XXXXXX", NULL);
    char *cfg = g_build_filename(tmpdir, "config", NULL);
    g_setenv("XDG_CONFIG_HOME", cfg, TRUE);
    if (!gtk_init_check(&argc, &argv)) {
        fprintf(stderr, "SKIP: no display\n");
        return 77;
    }
    settings_load();

    test_load_basic();
    test_load_eol_and_bom();
    test_load_errors();
    test_load_chunk_boundary();
    test_save();
    test_settings_roundtrip();
    test_lang();

    GtkApplication *app = gtk_application_new("io.github.krse.LightEditTest",
                                              G_APPLICATION_NON_UNIQUE);
    g_application_register(G_APPLICATION(app), NULL, NULL);
    test_window_docs(app);
    test_search(app);
    test_window_teardown(app);
    g_object_unref(app);

    char *rm[] = { "rm", "-rf", tmpdir, NULL };
    g_spawn_sync(NULL, rm, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL);
    g_free(cfg);
    g_free(tmpdir);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
