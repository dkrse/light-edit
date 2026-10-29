#include "window.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "doc.h"
#include "fileio.h"
#include "lang.h"
#include "prefs.h"
#include "print.h"
#include "search.h"
#include "settings.h"
#include "ui_theme.h"

/* ── Helpers ── */

static Doc *doc_at(Win *w, int i) {
    return doc_from_widget(gtk_notebook_get_nth_page(w->nb, i));
}

Doc *win_current(Win *w) {
    return doc_at(w, gtk_notebook_get_current_page(w->nb));
}

Win *win_from_window(GtkWindow *w) {
    return w ? g_object_get_data(G_OBJECT(w), "win") : NULL;
}

static char *canonical_path(const char *path) {
    char *real = realpath(path, NULL);
    if (real) {
        char *r = g_strdup(real);
        free(real);
        return r;
    }
    return g_canonicalize_filename(path, NULL);
}

static void show_error(Win *w, const char *msg) {
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(w->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void focus_doc(Win *w, Doc *d) {
    gtk_notebook_set_current_page(w->nb, gtk_notebook_page_num(w->nb, d->page));
    gtk_widget_grab_focus(d->sci);
}

/* Reads a tab restored from the session the first time it is shown. */
static void ensure_loaded(Win *w, Doc *d) {
    if (!d || !d->pending) return;
    d->pending = FALSE;
    char *path = g_strdup(d->path);
    char *err = NULL;
    if (doc_load(d, path, &err)) {
        sptr_t len = sci_send(d, SCI_GETLENGTH, 0, 0);
        sptr_t pos = d->restore_pos < len ? d->restore_pos : len;
        sci_send(d, SCI_SETSEL, pos, pos);
        sci_send(d, SCI_SETFIRSTVISIBLELINE, d->restore_top, 0);
    } else {
        d->on_disk = FALSE;
        doc_update_label(d);
        show_error(w, err);
        g_free(err);
    }
    g_free(path);
}

/* ── Title and status bar ── */

void win_update_title(Win *w) {
    Doc *d = win_current(w);
    if (!d) {
        gtk_window_set_title(GTK_WINDOW(w->window), "Light Edit");
        return;
    }
    char *name = doc_name(d);
    char *dir = d->path ? g_path_get_dirname(d->path) : NULL;
    /* Show the home directory as ~ to keep the title short. */
    const char *home = g_get_home_dir();
    size_t hl = strlen(home);
    if (dir && hl > 1 && strncmp(dir, home, hl) == 0 && (dir[hl] == '/' || dir[hl] == '\0')) {
        char *short_dir = g_strconcat("~", dir + hl, NULL);
        g_free(dir);
        dir = short_dir;
    }
    char *title = g_strdup_printf("%s%s%s%s · Light Edit",
        doc_is_dirty(d) ? "● " : "", name,
        dir ? " · " : "", dir ? dir : "");
    gtk_window_set_title(GTK_WINDOW(w->window), title);
    g_free(title);
    g_free(dir);
    g_free(name);
}

void win_update_status(Win *w) {
    Doc *d = win_current(w);
    if (!d) return;
    sptr_t pos   = sci_send(d, SCI_GETCURRENTPOS, 0, 0);
    sptr_t line  = sci_send(d, SCI_LINEFROMPOSITION, pos, 0);
    sptr_t col   = sci_send(d, SCI_GETCOLUMN, pos, 0);
    sptr_t lines = sci_send(d, SCI_GETLINECOUNT, 0, 0);
    sptr_t sel   = sci_send(d, SCI_GETSELECTIONEND, 0, 0) -
                   sci_send(d, SCI_GETSELECTIONSTART, 0, 0);
    int eol = (int)sci_send(d, SCI_GETEOLMODE, 0, 0);
    const char *eols = eol == SC_EOL_CRLF ? "CRLF" : eol == SC_EOL_CR ? "CR" : "LF";

    char buf[160];
    if (sel > 0)
        snprintf(buf, sizeof buf, "Ln %ld, Col %ld  (%ld selected)   %ld lines   %s   UTF-8   %s",
                 (long)line + 1, (long)col + 1, (long)sel, (long)lines, eols, d->lang);
    else
        snprintf(buf, sizeof buf, "Ln %ld, Col %ld   %ld lines   %s   UTF-8   %s",
                 (long)line + 1, (long)col + 1, (long)lines, eols, d->lang);
    gtk_label_set_text(GTK_LABEL(w->status), buf);
}

/* ── Documents ── */

static Doc *add_doc(Win *w) {
    Doc *d = doc_new(w);
    int i = gtk_notebook_append_page(w->nb, d->page, d->tab);
    gtk_notebook_set_tab_reorderable(w->nb, d->page, TRUE);
    if (!w->restoring) gtk_notebook_set_current_page(w->nb, i);
    return d;
}

void win_new_doc(Win *w) {
    Doc *d = add_doc(w);
    gtk_widget_grab_focus(d->sci);
}

static Doc *find_by_path(Win *w, const char *path) {
    int n = gtk_notebook_get_n_pages(w->nb);
    for (int i = 0; i < n; i++) {
        Doc *d = doc_at(w, i);
        if (d->path && strcmp(d->path, path) == 0) return d;
    }
    return NULL;
}

void win_open_path(Win *w, const char *path) {
    char *canon = canonical_path(path);
    Doc *d = find_by_path(w, canon);
    if (d) {
        focus_doc(w, d);
        g_free(canon);
        return;
    }

    Doc *cur = win_current(w);
    gboolean reuse = cur && doc_is_pristine(cur);
    d = reuse ? cur : add_doc(w);

    char *err = NULL;
    if (doc_load(d, canon, &err)) {
        focus_doc(w, d);
        win_update_title(w);
        win_update_status(w);
        win_save_session(w);
    } else {
        if (!reuse) gtk_widget_destroy(d->page);   /* frees the Doc */
        show_error(w, err);
        g_free(err);
    }
    g_free(canon);
}

static char *choose_save_path(Win *w, Doc *d) {
    GtkFileChooserNative *fc = gtk_file_chooser_native_new("Save As",
        GTK_WINDOW(w->window), GTK_FILE_CHOOSER_ACTION_SAVE, "_Save", "_Cancel");
    GtkFileChooser *ch = GTK_FILE_CHOOSER(fc);
    gtk_file_chooser_set_do_overwrite_confirmation(ch, TRUE);
    if (d->path) gtk_file_chooser_set_filename(ch, d->path);
    else gtk_file_chooser_set_current_name(ch, "Untitled.txt");

    char *path = NULL;
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT)
        path = gtk_file_chooser_get_filename(ch);
    g_object_unref(fc);
    return path;
}

static gboolean save_doc(Win *w, Doc *d, gboolean save_as) {
    char *path = (save_as || !d->path) ? choose_save_path(w, d) : g_strdup(d->path);
    if (!path) return FALSE;

    char *err = NULL;
    if (!fileio_save(d, path, &err)) {
        show_error(w, err);
        g_free(err);
        g_free(path);
        return FALSE;
    }

    char *canon = canonical_path(path);
    g_free(path);
    if (!d->path || strcmp(canon, d->path) != 0) {
        g_free(d->path);
        d->path = canon;
        lang_apply_for_path(d, d->path);
        win_save_session(w);
    } else {
        g_free(canon);
    }
    doc_update_label(d);
    if (d == win_current(w)) {
        win_update_title(w);
        win_update_status(w);
    }
    return TRUE;
}

/* TRUE when the caller may go on discarding the document. */
static gboolean confirm_discard(Win *w, Doc *d) {
    if (!doc_is_dirty(d)) return TRUE;
    focus_doc(w, d);

    char *name = doc_name(d);
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(w->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
        "Save changes to “%s” before closing?", name);
    g_free(name);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg),
        "Your changes will be lost if you don't save them.");
    gtk_dialog_add_buttons(GTK_DIALOG(dlg),
        "Close _without Saving", GTK_RESPONSE_REJECT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
    int r = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);

    if (r == GTK_RESPONSE_REJECT) return TRUE;
    if (r == GTK_RESPONSE_ACCEPT) return save_doc(w, d, FALSE);
    return FALSE;
}

gboolean win_close_doc(Win *w, Doc *d) {
    if (!confirm_discard(w, d)) return FALSE;
    gtk_widget_destroy(d->page);               /* frees the Doc */
    if (gtk_notebook_get_n_pages(w->nb) == 0) win_new_doc(w);
    win_save_session(w);
    return TRUE;
}

/* ── External changes ──
   Checked when the window gets focus: one stat() per open file, no inotify
   watches to keep alive, and the user sees the result as soon as they look. */

static gboolean ask_reload(Win *w, Doc *d) {
    char *name = doc_name(d);
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(w->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
        "“%s” was changed on disk.", name);
    g_free(name);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg),
        "Reload it and lose your unsaved changes?");
    gtk_dialog_add_buttons(GTK_DIALOG(dlg),
        "_Keep My Version", GTK_RESPONSE_REJECT,
        "_Reload", GTK_RESPONSE_ACCEPT, NULL);
    int r = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    return r == GTK_RESPONSE_ACCEPT;
}

static gboolean check_disk_idle(gpointer data) {
    Win *w = data;
    w->disk_idle = 0;
    w->checking_disk = TRUE;

    int n = gtk_notebook_get_n_pages(w->nb);
    for (int i = 0; i < n; i++) {
        Doc *d = doc_at(w, i);
        if (!d->path || d->pending) continue;

        gint64 mtime;
        goffset size;
        gboolean exists = fileio_stat(d->path, &mtime, &size);
        if (!exists) {
            if (d->on_disk) {
                d->on_disk = FALSE;
                doc_update_label(d);
            }
            continue;
        }
        if (d->on_disk && mtime == d->mtime_ns && size == d->size) continue;

        /* Remember what we saw so a "keep" answer is not asked again. */
        d->mtime_ns = mtime;
        d->size = size;
        d->on_disk = TRUE;
        if (doc_is_dirty(d) && !ask_reload(w, d)) {
            doc_update_label(d);
            continue;
        }
        char *err = NULL;
        if (!doc_reload(d, &err)) {
            show_error(w, err);
            g_free(err);
        }
    }
    win_update_title(w);
    win_update_status(w);
    w->checking_disk = FALSE;
    return G_SOURCE_REMOVE;
}

static gboolean on_focus_in(GtkWidget *wid, GdkEvent *ev, gpointer data) {
    (void)wid; (void)ev;
    Win *w = data;
    /* Dialogs shown by the check give focus back; don't loop on that. */
    if (!w->checking_disk && !w->disk_idle)
        w->disk_idle = g_idle_add(check_disk_idle, w);
    return FALSE;
}

/* ── Actions ── */

static void act_new(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    win_new_doc(data);
}

/* Asks for a name, creates the (empty) file on disk and opens it. */
static void act_new_file(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    GtkFileChooserNative *fc = gtk_file_chooser_native_new("New File",
        GTK_WINDOW(w->window), GTK_FILE_CHOOSER_ACTION_SAVE, "_Create", "_Cancel");
    GtkFileChooser *ch = GTK_FILE_CHOOSER(fc);
    gtk_file_chooser_set_do_overwrite_confirmation(ch, TRUE);
    gtk_file_chooser_set_current_name(ch, "Untitled.txt");
    Doc *cur = win_current(w);
    if (cur && cur->path) {
        char *dir = g_path_get_dirname(cur->path);
        gtk_file_chooser_set_current_folder(ch, dir);
        g_free(dir);
    }
    char *path = NULL;
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT)
        path = gtk_file_chooser_get_filename(ch);
    g_object_unref(fc);
    if (!path) return;

    /* The chooser already confirmed replacing an existing file. */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) {
        char *msg = g_strdup_printf("Cannot create \"%s\": %s", path, g_strerror(errno));
        show_error(w, msg);
        g_free(msg);
    } else {
        close(fd);
        win_open_path(w, path);
    }
    g_free(path);
}

static void act_open(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    GtkFileChooserNative *fc = gtk_file_chooser_native_new("Open",
        GTK_WINDOW(w->window), GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");
    GtkFileChooser *ch = GTK_FILE_CHOOSER(fc);
    gtk_file_chooser_set_select_multiple(ch, TRUE);
    Doc *d = win_current(w);
    if (d && d->path) {
        char *dir = g_path_get_dirname(d->path);
        gtk_file_chooser_set_current_folder(ch, dir);
        g_free(dir);
    }
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
        GSList *files = gtk_file_chooser_get_filenames(ch);
        for (GSList *l = files; l; l = l->next) win_open_path(w, l->data);
        g_slist_free_full(files, g_free);
    }
    g_object_unref(fc);
}

static void act_save(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    Doc *d = win_current(w);
    if (d) save_doc(w, d, FALSE);
}

static void act_save_as(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    Doc *d = win_current(w);
    if (d) save_doc(w, d, TRUE);
}

static void act_close_tab(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    Doc *d = win_current(w);
    if (d) win_close_doc(w, d);
}

static void act_next_tab(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    int n = gtk_notebook_get_n_pages(w->nb);
    gtk_notebook_set_current_page(w->nb, (gtk_notebook_get_current_page(w->nb) + 1) % n);
}

static void act_prev_tab(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    int n = gtk_notebook_get_n_pages(w->nb);
    gtk_notebook_set_current_page(w->nb, (gtk_notebook_get_current_page(w->nb) + n - 1) % n);
}

static void act_wrap(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)p;
    settings.wrap = !settings.wrap;
    g_simple_action_set_state(a, g_variant_new_boolean(settings.wrap));
    win_apply_settings(data);
    settings_save();
}

static void act_quit(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    gtk_window_close(GTK_WINDOW(w->window));
}

/* Edit actions go to the search entries when they have focus, otherwise to
   the editor. Their keys are not bound as accelerators, so Ctrl+Z, Ctrl+C...
   keep working natively in every widget; the menu only shows the labels. */
static GtkWidget *focused_editable(Win *w) {
    GtkWidget *f = gtk_window_get_focus(GTK_WINDOW(w->window));
    return f && GTK_IS_EDITABLE(f) ? f : NULL;
}

#define EDIT_ACTION(fn, sci_msg, editable_call)                          \
    static void fn(GSimpleAction *a, GVariant *p, gpointer data) {       \
        (void)a; (void)p;                                                \
        Win *w = data;                                                   \
        GtkWidget *e = focused_editable(w);                              \
        Doc *d = win_current(w);                                         \
        if (e) { editable_call; }                                        \
        else if (d) sci_send(d, sci_msg, 0, 0);                          \
    }

EDIT_ACTION(act_undo, SCI_UNDO, (void)0)
EDIT_ACTION(act_redo, SCI_REDO, (void)0)
EDIT_ACTION(act_cut, SCI_CUT, gtk_editable_cut_clipboard(GTK_EDITABLE(e)))
EDIT_ACTION(act_copy, SCI_COPY, gtk_editable_copy_clipboard(GTK_EDITABLE(e)))
EDIT_ACTION(act_paste, SCI_PASTE, gtk_editable_paste_clipboard(GTK_EDITABLE(e)))
EDIT_ACTION(act_select_all, SCI_SELECTALL, gtk_editable_select_region(GTK_EDITABLE(e), 0, -1))

static void act_duplicate(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Doc *d = win_current(data);
    if (d) sci_send(d, SCI_SELECTIONDUPLICATE, 0, 0);
}

static void act_find(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    search_show(w->search, FALSE);
}

static void act_replace(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    search_show(w->search, TRUE);
}

static void act_find_next(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    search_next(w->search, TRUE);
}

static void act_find_prev(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    search_next(w->search, FALSE);
}

static void act_find_close(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    search_hide(w->search);
}

static void act_goto_line(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    Doc *d = win_current(w);
    if (!d) return;
    sptr_t lines = sci_send(d, SCI_GETLINECOUNT, 0, 0);
    sptr_t cur = sci_send(d, SCI_LINEFROMPOSITION, sci_send(d, SCI_GETCURRENTPOS, 0, 0), 0);

    GtkWidget *dlg = gtk_dialog_new_with_buttons("Go to Line", GTK_WINDOW(w->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Go", GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
    GtkWidget *spin = gtk_spin_button_new_with_range(1, (double)lines, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), (double)cur + 1);
    gtk_entry_set_activates_default(GTK_ENTRY(spin), TRUE);
    char *hint = g_strdup_printf("Line (1 to %ld):", (long)lines);
    GtkWidget *label = gtk_label_new(hint);
    g_free(hint);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), spin, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), box);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
        sptr_t line = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin)) - 1;
        sci_send(d, SCI_ENSUREVISIBLEENFORCEPOLICY, line, 0);
        sci_send(d, SCI_GOTOLINE, line, 0);
        sci_send(d, SCI_VERTICALCENTRECARET, 0, 0);
    }
    gtk_widget_destroy(dlg);
    gtk_widget_grab_focus(d->sci);
}

static void act_print(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    Win *w = data;
    Doc *d = win_current(w);
    if (d) print_doc(w, d);
}

static void act_zoom_in(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    win_zoom(data, 1);
}

static void act_zoom_out(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    win_zoom(data, -1);
}

static void act_zoom_reset(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    win_zoom(data, 0);
}

static void act_settings(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    prefs_show(data);
}

static const GActionEntry actions[] = {
    { "new",        act_new,        NULL, NULL,    NULL, {0} },
    { "open",       act_open,       NULL, NULL,    NULL, {0} },
    { "save",       act_save,       NULL, NULL,    NULL, {0} },
    { "save-as",    act_save_as,    NULL, NULL,    NULL, {0} },
    { "close-tab",  act_close_tab,  NULL, NULL,    NULL, {0} },
    { "next-tab",   act_next_tab,   NULL, NULL,    NULL, {0} },
    { "prev-tab",   act_prev_tab,   NULL, NULL,    NULL, {0} },
    { "wrap",       act_wrap,       NULL, "false", NULL, {0} },
    { "quit",       act_quit,       NULL, NULL,    NULL, {0} },
    { "undo",       act_undo,       NULL, NULL,    NULL, {0} },
    { "redo",       act_redo,       NULL, NULL,    NULL, {0} },
    { "cut",        act_cut,        NULL, NULL,    NULL, {0} },
    { "copy",       act_copy,       NULL, NULL,    NULL, {0} },
    { "paste",      act_paste,      NULL, NULL,    NULL, {0} },
    { "select-all", act_select_all, NULL, NULL,    NULL, {0} },
    { "duplicate",  act_duplicate,  NULL, NULL,    NULL, {0} },
    { "find",       act_find,       NULL, NULL,    NULL, {0} },
    { "replace",    act_replace,    NULL, NULL,    NULL, {0} },
    { "find-next",  act_find_next,  NULL, NULL,    NULL, {0} },
    { "find-prev",  act_find_prev,  NULL, NULL,    NULL, {0} },
    { "find-close", act_find_close, NULL, NULL,    NULL, {0} },
    { "goto-line",  act_goto_line,  NULL, NULL,    NULL, {0} },
    { "settings",   act_settings,   NULL, NULL,    NULL, {0} },
    { "new-file",   act_new_file,   NULL, NULL,    NULL, {0} },
    { "print",      act_print,      NULL, NULL,    NULL, {0} },
    { "zoom-in",    act_zoom_in,    NULL, NULL,    NULL, {0} },
    { "zoom-out",   act_zoom_out,   NULL, NULL,    NULL, {0} },
    { "zoom-reset", act_zoom_reset, NULL, NULL,    NULL, {0} },
};

static void set_accels(GtkApplication *app) {
    static const struct { const char *action; const char *keys[4]; } map[] = {
        { "win.new-file",   { "<Primary><Alt>n", NULL } },
        { "win.print",      { "<Primary>p", NULL } },
        { "win.zoom-in",    { "<Primary>plus", "<Primary>equal", "<Primary>KP_Add", NULL } },
        { "win.zoom-out",   { "<Primary>minus", "<Primary>KP_Subtract", NULL } },
        { "win.zoom-reset", { "<Primary>0", "<Primary>KP_0", NULL } },
        { "win.new",        { "<Primary>n", "<Primary>t", NULL } },
        { "win.open",       { "<Primary>o", NULL } },
        { "win.save",       { "<Primary>s", NULL } },
        { "win.save-as",    { "<Primary><Shift>s", NULL } },
        { "win.close-tab",  { "<Primary>w", NULL } },
        { "win.next-tab",   { "<Primary>Page_Down", "<Primary>Tab", NULL } },
        { "win.prev-tab",   { "<Primary>Page_Up", "<Primary><Shift>ISO_Left_Tab", NULL } },
        { "win.quit",       { "<Primary>q", NULL } },
        { "win.find",       { "<Primary>f", NULL } },
        { "win.replace",    { "<Primary>h", NULL } },
        { "win.find-next",  { "F3", NULL } },
        { "win.find-prev",  { "<Shift>F3", NULL } },
        { "win.find-close", { "Escape", NULL } },
        { "win.goto-line",  { "<Primary>g", NULL } },
        { "win.settings",   { "<Primary>comma", NULL } },
    };
    for (size_t i = 0; i < G_N_ELEMENTS(map); i++)
        gtk_application_set_accels_for_action(app, map[i].action, map[i].keys);
}

static void set_enabled(Win *w, const char *name, gboolean on) {
    GAction *a = g_action_map_lookup_action(G_ACTION_MAP(w->window), name);
    if (a) g_simple_action_set_enabled(G_SIMPLE_ACTION(a), on);
}

void win_update_actions(Win *w) {
    Doc *d = win_current(w);
    set_enabled(w, "undo", d && sci_send(d, SCI_CANUNDO, 0, 0));
    set_enabled(w, "redo", d && sci_send(d, SCI_CANREDO, 0, 0));
    /* Escape belongs to the editor unless the search bar is open. */
    set_enabled(w, "find-close", search_visible(w->search));
}

void win_apply_settings(Win *w) {
    ui_theme_apply();
    int n = gtk_notebook_get_n_pages(w->nb);
    for (int i = 0; i < n; i++) doc_apply_settings(doc_at(w, i));
    GAction *wrap = g_action_map_lookup_action(G_ACTION_MAP(w->window), "wrap");
    g_simple_action_set_state(G_SIMPLE_ACTION(wrap), g_variant_new_boolean(settings.wrap));
    search_doc_switched(w->search);
}

static gboolean save_settings_cb(gpointer data) {
    Win *w = data;
    w->settings_save_id = 0;
    settings_save();
    return G_SOURCE_REMOVE;
}

void win_zoom(Win *w, int step) {
    double size = step == 0 ? 11.0 : settings.font_size + step;
    if (size < 6) size = 6;
    if (size > 72) size = 72;
    if (size == settings.font_size) return;
    settings.font_size = size;
    int n = gtk_notebook_get_n_pages(w->nb);
    for (int i = 0; i < n; i++) doc_apply_font_size(doc_at(w, i));

    char buf[48];
    g_snprintf(buf, sizeof buf, "Font size %g pt", size);
    gtk_label_set_text(GTK_LABEL(w->status), buf);

    /* Many wheel notches in a row: write the settings file once. */
    if (w->settings_save_id) g_source_remove(w->settings_save_id);
    w->settings_save_id = g_timeout_add(700, save_settings_cb, w);
}

/* ── Session ── */

static char *session_path(void) {
    return g_build_filename(g_get_user_config_dir(), "light-edit", "session.ini", NULL);
}

void win_save_session(Win *w) {
    if (w->restoring) return;
    int n = gtk_notebook_get_n_pages(w->nb);
    Doc *cur = win_current(w);
    GPtrArray *files = g_ptr_array_new();
    GPtrArray *pos = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *top = g_ptr_array_new_with_free_func(g_free);
    int current = -1;

    for (int i = 0; i < n; i++) {
        Doc *d = doc_at(w, i);
        if (!d->path) continue;                  /* untitled tabs are not kept */
        if (d == cur) current = (int)files->len;
        sptr_t p = d->pending ? d->restore_pos : sci_send(d, SCI_GETCURRENTPOS, 0, 0);
        sptr_t t = d->pending ? d->restore_top : sci_send(d, SCI_GETFIRSTVISIBLELINE, 0, 0);
        g_ptr_array_add(files, d->path);
        g_ptr_array_add(pos, g_strdup_printf("%ld", (long)p));
        g_ptr_array_add(top, g_strdup_printf("%ld", (long)t));
    }

    GKeyFile *kf = g_key_file_new();
    g_key_file_set_string_list(kf, "session", "files",
        (const char * const *)files->pdata, files->len);
    g_key_file_set_string_list(kf, "session", "positions",
        (const char * const *)pos->pdata, pos->len);
    g_key_file_set_string_list(kf, "session", "top_lines",
        (const char * const *)top->pdata, top->len);
    g_key_file_set_integer(kf, "session", "current", current);

    char *path = session_path();
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    gsize len;
    char *data = g_key_file_to_data(kf, &len, NULL);
    g_file_set_contents(path, data, (gssize)len, NULL);   /* atomic */

    g_free(data);
    g_free(dir);
    g_free(path);
    g_key_file_free(kf);
    g_ptr_array_free(files, TRUE);
    g_ptr_array_free(pos, TRUE);
    g_ptr_array_free(top, TRUE);
}

void win_restore_session(Win *w) {
    char *path = session_path();
    GKeyFile *kf = g_key_file_new();
    gboolean ok = g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);
    g_free(path);
    if (!ok) {
        g_key_file_free(kf);
        return;
    }

    gsize nf = 0, np = 0, nt = 0;
    char **files = g_key_file_get_string_list(kf, "session", "files", &nf, NULL);
    char **pos = g_key_file_get_string_list(kf, "session", "positions", &np, NULL);
    char **top = g_key_file_get_string_list(kf, "session", "top_lines", &nt, NULL);
    int current = g_key_file_get_integer(kf, "session", "current", NULL);
    g_key_file_free(kf);

    /* Tabs are created without reading the files: only the one shown is
       loaded now, the others when first selected. */
    w->restoring = TRUE;
    int cur_page = -1;
    for (gsize i = 0; i < nf; i++) {
        if (!g_file_test(files[i], G_FILE_TEST_IS_REGULAR)) continue;
        Doc *d = doc_new(w);
        d->path = g_strdup(files[i]);
        d->pending = TRUE;
        d->on_disk = TRUE;
        d->restore_pos = i < np ? g_ascii_strtoll(pos[i], NULL, 10) : 0;
        d->restore_top = i < nt ? g_ascii_strtoll(top[i], NULL, 10) : 0;
        lang_apply_for_path(d, d->path);
        doc_update_label(d);
        int page = gtk_notebook_append_page(w->nb, d->page, d->tab);
        gtk_notebook_set_tab_reorderable(w->nb, d->page, TRUE);
        if ((int)i == current) cur_page = page;
    }
    w->restoring = FALSE;
    g_strfreev(files);
    g_strfreev(pos);
    g_strfreev(top);

    if (gtk_notebook_get_n_pages(w->nb) == 0) return;
    if (cur_page >= 0) gtk_notebook_set_current_page(w->nb, cur_page);
    Doc *d = win_current(w);
    ensure_loaded(w, d);
    win_update_title(w);
    win_update_status(w);
    win_update_actions(w);
}

/* ── Window ── */

static gboolean on_delete(GtkWidget *wid, GdkEvent *ev, gpointer data) {
    (void)wid; (void)ev;
    Win *w = data;
    int n = gtk_notebook_get_n_pages(w->nb);
    for (int i = 0; i < n; i++)
        if (!confirm_discard(w, doc_at(w, i))) return TRUE;   /* stay open */
    win_save_session(w);
    return FALSE;
}

static void on_destroy(GtkWidget *wid, gpointer data) {
    (void)wid;
    Win *w = data;
    if (w->disk_idle) g_source_remove(w->disk_idle);
    if (w->settings_save_id) {
        g_source_remove(w->settings_save_id);
        settings_save();
    }
    search_free(w->search);
    g_free(w);
}

static void on_switch_page(GtkNotebook *nb, GtkWidget *page, guint num, gpointer data) {
    (void)nb; (void)page; (void)num;
    Win *w = data;
    if (w->restoring) return;
    ensure_loaded(w, win_current(w));
    /* Matches of the tab we left are dropped; the new one is searched. */
    int np = gtk_notebook_get_n_pages(w->nb);
    for (int i = 0; i < np; i++) {
        Doc *d = doc_at(w, i);
        if (d != win_current(w) && d->matches->len) {
            sci_send(d, SCI_SETINDICATORCURRENT, IND_FIND, 0);
            sci_send(d, SCI_INDICATORCLEARRANGE, 0, sci_send(d, SCI_GETLENGTH, 0, 0));
            g_array_set_size(d->matches, 0);
        }
    }
    search_doc_switched(w->search);
    win_update_title(w);
    win_update_status(w);
    win_update_actions(w);
}

static void menu_add(GMenu *m, const char *label, const char *action, const char *accel) {
    GMenuItem *it = g_menu_item_new(label, action);
    if (accel) g_menu_item_set_attribute(it, "accel", "s", accel);
    g_menu_append_item(m, it);
    g_object_unref(it);
}

static void menu_section(GMenu *menu, GMenu *section) {
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);
}

static GMenuModel *build_menu(void) {
    GMenu *menu = g_menu_new();

    GMenu *file = g_menu_new();
    menu_add(file, "New Tab", "win.new", NULL);
    menu_add(file, "New File…", "win.new-file", NULL);
    menu_add(file, "Open…", "win.open", NULL);
    menu_add(file, "Save", "win.save", NULL);
    menu_add(file, "Save As…", "win.save-as", NULL);
    menu_add(file, "Print…", "win.print", NULL);
    menu_section(menu, file);

    GMenu *edit = g_menu_new();
    GMenu *e1 = g_menu_new();
    menu_add(e1, "Undo", "win.undo", "<Primary>z");
    menu_add(e1, "Redo", "win.redo", "<Primary><Shift>z");
    menu_section(edit, e1);
    GMenu *e2 = g_menu_new();
    menu_add(e2, "Cut", "win.cut", "<Primary>x");
    menu_add(e2, "Copy", "win.copy", "<Primary>c");
    menu_add(e2, "Paste", "win.paste", "<Primary>v");
    menu_add(e2, "Select All", "win.select-all", "<Primary>a");
    menu_add(e2, "Duplicate Line", "win.duplicate", "<Primary>d");
    menu_section(edit, e2);
    GMenu *e3 = g_menu_new();
    menu_add(e3, "Find…", "win.find", NULL);
    menu_add(e3, "Find and Replace…", "win.replace", NULL);
    menu_add(e3, "Find Next", "win.find-next", NULL);
    menu_add(e3, "Find Previous", "win.find-prev", NULL);
    menu_add(e3, "Go to Line…", "win.goto-line", NULL);
    menu_section(edit, e3);
    GMenu *edit_sec = g_menu_new();
    g_menu_append_submenu(edit_sec, "Edit", G_MENU_MODEL(edit));
    g_object_unref(edit);
    menu_section(menu, edit_sec);

    GMenu *view = g_menu_new();
    menu_add(view, "Word Wrap", "win.wrap", NULL);
    menu_add(view, "Settings…", "win.settings", NULL);
    menu_section(menu, view);

    GMenu *end = g_menu_new();
    menu_add(end, "Close Tab", "win.close-tab", NULL);
    menu_add(end, "Quit", "win.quit", NULL);
    menu_section(menu, end);
    return G_MENU_MODEL(menu);
}

Win *win_create(GtkApplication *app) {
    Win *w = g_new0(Win, 1);
    w->app = app;
    w->window = gtk_application_window_new(app);
    g_object_set_data(G_OBJECT(w->window), "win", w);
    gtk_window_set_default_size(GTK_WINDOW(w->window), 1000, 700);

    g_action_map_add_action_entries(G_ACTION_MAP(w->window), actions,
                                    G_N_ELEMENTS(actions), w);
    set_accels(app);
    w->search = search_new(w);

    /* Header bar */
    GtkWidget *hb = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
    GtkWidget *open = gtk_button_new_with_mnemonic("_Open");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(open), "win.open");
    GtkWidget *save = gtk_button_new_with_mnemonic("_Save");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(save), "win.save");
    GtkWidget *find = gtk_button_new_from_icon_name("edit-find-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(find, "Find (Ctrl+F)");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(find), "win.find");
    GtkWidget *menu_btn = gtk_menu_button_new();
    gtk_button_set_image(GTK_BUTTON(menu_btn),
        gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON));
    GMenuModel *model = build_menu();
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn), model);
    g_object_unref(model);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), open);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), menu_btn);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), save);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), find);
    gtk_window_set_titlebar(GTK_WINDOW(w->window), hb);

    /* Tabs */
    w->nb = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_notebook_set_scrollable(w->nb, TRUE);
    gtk_notebook_set_show_border(w->nb, FALSE);
    GtkWidget *add = gtk_button_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(add), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(add, "New Tab (Ctrl+T)");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(add), "win.new");
    gtk_widget_show(add);
    gtk_notebook_set_action_widget(w->nb, add, GTK_PACK_END);
    g_signal_connect_after(w->nb, "switch-page", G_CALLBACK(on_switch_page), w);

    /* Status bar */
    w->status = gtk_label_new(NULL);
    gtk_widget_set_name(w->status, "le-status");
    gtk_label_set_xalign(GTK_LABEL(w->status), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(w->status), PANGO_ELLIPSIZE_END);
    gtk_widget_set_margin_start(w->status, 8);
    gtk_widget_set_margin_end(w->status, 8);
    gtk_widget_set_margin_top(w->status, 3);
    gtk_widget_set_margin_bottom(w->status, 3);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(w->nb), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), search_widget(w->search), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), w->status, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(w->window), box);

    g_signal_connect(w->window, "delete-event", G_CALLBACK(on_delete), w);
    g_signal_connect(w->window, "destroy", G_CALLBACK(on_destroy), w);
    g_signal_connect(w->window, "focus-in-event", G_CALLBACK(on_focus_in), w);

    ui_theme_apply();
    GAction *wrap = g_action_map_lookup_action(G_ACTION_MAP(w->window), "wrap");
    g_simple_action_set_state(G_SIMPLE_ACTION(wrap), g_variant_new_boolean(settings.wrap));

    gtk_widget_show_all(w->window);
    win_update_actions(w);
    return w;
}
