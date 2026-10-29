#include "doc.h"

#include <string.h>

#include "fileio.h"
#include "lang.h"
#include "search.h"
#include "settings.h"
#include "window.h"

#define TAB_MAX_CHARS 30

static void doc_free(gpointer p) {
    Doc *d = p;
    g_array_free(d->matches, TRUE);
    g_free(d->path);
    g_free(d);
}

/* Widen the line number margin only when the digit count changes. */
static void update_margin(Doc *d) {
    if (!settings.line_numbers) {
        sci_send(d, SCI_SETMARGINWIDTHN, 0, 0);
        d->ln_digits = 0;
        return;
    }
    sptr_t lines = sci_send(d, SCI_GETLINECOUNT, 0, 0);
    int digits = 1;
    while (lines >= 10) { lines /= 10; digits++; }
    if (digits < 3) digits = 3;
    if (digits == d->ln_digits) return;
    d->ln_digits = digits;

    char sample[24];
    memset(sample, '9', (size_t)digits + 1);
    sample[digits + 1] = '\0';
    sptr_t px = sci_send(d, SCI_TEXTWIDTH, STYLE_LINENUMBER, (sptr_t)sample);
    sci_send(d, SCI_SETMARGINWIDTHN, 0, px);
}

static void on_uri_dropped(Doc *d, const char *text) {
    char **uris = g_uri_list_extract_uris(text);
    for (char **u = uris; u && *u; u++) {
        char *path = g_filename_from_uri(*u, NULL, NULL);
        if (path) win_open_path(d->win, path);
        g_free(path);
    }
    g_strfreev(uris);
}

static void on_notify(GtkWidget *w, gint id, SCNotification *n, gpointer data) {
    (void)w; (void)id;
    Doc *d = data;
    gboolean current = d == win_current(d->win);
    switch (n->nmhdr.code) {
    case SCN_SAVEPOINTREACHED:
    case SCN_SAVEPOINTLEFT:
        doc_update_label(d);
        if (current) win_update_title(d->win);
        break;
    case SCN_UPDATEUI:
        if (n->updated & SC_UPDATE_CONTENT) {
            update_margin(d);
            if (current) search_doc_changed(d->win->search, d);
        }
        if (current && (n->updated & (SC_UPDATE_CONTENT | SC_UPDATE_SELECTION))) {
            win_update_status(d->win);
            win_update_actions(d->win);
        }
        break;
    case SCN_URIDROPPED:
        on_uri_dropped(d, n->text);
        break;
    default:
        break;
    }
}

/* ── Search marks on the vertical scrollbar ──
   Painted on top of Scintilla's own scrollbar widget after it draws itself,
   so no extra layer sits over the editor and steals mouse input. */

static void find_vscroll(GtkWidget *child, gpointer data) {
    if (GTK_IS_SCROLLBAR(child) &&
        gtk_orientable_get_orientation(GTK_ORIENTABLE(child)) == GTK_ORIENTATION_VERTICAL)
        *(GtkWidget **)data = child;
}

static gboolean draw_marks(GtkWidget *sb, cairo_t *cr, gpointer data) {
    Doc *d = data;
    if (d->matches->len == 0) return FALSE;

    int width = gtk_widget_get_allocated_width(sb);
    int height = gtk_widget_get_allocated_height(sb);
    sptr_t lines = sci_send(d, SCI_GETLINECOUNT, 0, 0);
    sptr_t total = sci_send(d, SCI_VISIBLEFROMDOCLINE, lines, 0);
    if (total <= 0) return FALSE;

    unsigned c = settings_theme()->mark;
    cairo_set_source_rgba(cr, ((c >> 16) & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0,
                          (c & 0xff) / 255.0, 0.9);
    const int mh = 3;
    int span = height - mh;
    int last = -1;
    sptr_t last_line = -1;
    for (guint i = 0; i < d->matches->len; i++) {
        sptr_t line = sci_send(d, SCI_LINEFROMPOSITION, g_array_index(d->matches, sptr_t, i), 0);
        if (line == last_line) continue;
        last_line = line;
        sptr_t vis = sci_send(d, SCI_VISIBLEFROMDOCLINE, line, 0);
        int py = (int)((double)vis / (double)total * span);
        if (py == last) continue;       /* one rectangle per pixel row */
        last = py;
        cairo_rectangle(cr, 2, py, width - 4, mh);
    }
    cairo_fill(cr);
    return FALSE;
}

/* ── Ctrl + mouse wheel changes the font size of every tab ── */

static gboolean on_scroll(GtkWidget *w, GdkEventScroll *ev, gpointer data) {
    (void)w;
    Doc *d = data;
    if (!(ev->state & GDK_CONTROL_MASK)) return FALSE;

    int step = 0;
    if (ev->direction == GDK_SCROLL_UP) step = 1;
    else if (ev->direction == GDK_SCROLL_DOWN) step = -1;
    else if (ev->direction == GDK_SCROLL_SMOOTH) {
        /* Touchpads and hi-res wheels send fractions: act per whole notch. */
        d->scroll_acc -= ev->delta_y;
        if (d->scroll_acc >= 1.0) { step = 1; d->scroll_acc = 0; }
        else if (d->scroll_acc <= -1.0) { step = -1; d->scroll_acc = 0; }
    }
    if (step) win_zoom(d->win, step);
    return TRUE;                    /* never let Scintilla scroll or zoom */
}

/* ── Tab label ── */

static gboolean on_tab_click(GtkWidget *w, GdkEventButton *ev, gpointer data) {
    (void)w;
    if (ev->type == GDK_BUTTON_PRESS && ev->button == 2) {
        Doc *d = data;
        win_close_doc(d->win, d);
        return TRUE;
    }
    return FALSE;
}

static void on_tab_close(GtkButton *b, gpointer data) {
    (void)b;
    Doc *d = data;
    win_close_doc(d->win, d);
}

Doc *doc_new(Win *w) {
    Doc *d = g_new0(Doc, 1);
    d->win = w;
    d->lang = "Plain Text";
    d->matches = g_array_new(FALSE, FALSE, sizeof(sptr_t));
    d->sci = scintilla_new();
    g_object_set_data_full(G_OBJECT(d->sci), "doc", d, doc_free);

    sci_send(d, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    sci_send(d, SCI_SETMARGINWIDTHN, 1, 0);
    sci_send(d, SCI_SETMARGINLEFT, 0, 4);
    sci_send(d, SCI_SETCARETWIDTH, 2, 0);
    sci_send(d, SCI_SETCARETLINEVISIBLEALWAYS, 1, 0);
    sci_send(d, SCI_SETMULTIPLESELECTION, 1, 0);
    sci_send(d, SCI_SETADDITIONALSELECTIONTYPING, 1, 0);
    sci_send(d, SCI_SETVIRTUALSPACEOPTIONS, SCVS_RECTANGULARSELECTION, 0);
    sci_send(d, SCI_SETSCROLLWIDTH, 1, 0);
    sci_send(d, SCI_SETSCROLLWIDTHTRACKING, 1, 0);

    /* Performance: cache layout per page, style the rest of a large file in
       idle time instead of blocking on open. */
    sci_send(d, SCI_SETLAYOUTCACHE, SC_CACHE_PAGE, 0);
    sci_send(d, SCI_SETIDLESTYLING, SC_IDLESTYLING_ALL, 0);
    sci_send(d, SCI_SETMODEVENTMASK, 0, 0);   /* we use no SCN_MODIFIED */
    doc_apply_settings(d);

    g_signal_connect(d->sci, SCINTILLA_NOTIFY, G_CALLBACK(on_notify), d);
    gtk_widget_add_events(d->sci, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(d->sci, "scroll-event", G_CALLBACK(on_scroll), d);

    d->page = d->sci;
    gtk_container_forall(GTK_CONTAINER(d->sci), find_vscroll, &d->marks);
    if (d->marks)
        g_signal_connect_after(d->marks, "draw", G_CALLBACK(draw_marks), d);

    /* Tab label: name + close button; middle click closes too. */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    d->label = gtk_label_new(NULL);
    gtk_label_set_ellipsize(GTK_LABEL(d->label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(d->label), TAB_MAX_CHARS);
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(close, FALSE);
    gtk_widget_set_tooltip_text(close, "Close Tab");
    g_signal_connect(close, "clicked", G_CALLBACK(on_tab_close), d);
    gtk_box_pack_start(GTK_BOX(box), d->label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), close, FALSE, FALSE, 0);

    d->tab = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(d->tab), FALSE);
    gtk_container_add(GTK_CONTAINER(d->tab), box);
    g_signal_connect(d->tab, "button-press-event", G_CALLBACK(on_tab_click), d);
    gtk_widget_show_all(d->tab);
    gtk_widget_show_all(d->page);

    doc_update_label(d);
    return d;
}

void doc_apply_settings(Doc *d) {
    lang_apply_for_path(d, d->path);
    sci_send(d, SCI_SETWRAPMODE, settings.wrap ? SC_WRAP_WORD : SC_WRAP_NONE, 0);
    d->ln_digits = 0;            /* font may have changed: re-measure */
    update_margin(d);
    if (d->marks) gtk_widget_queue_draw(d->marks);
}

void doc_apply_font_size(Doc *d) {
    sptr_t size = (sptr_t)(settings.font_size * SC_FONT_SIZE_MULTIPLIER + 0.5);
    for (int s = 0; s <= STYLE_MAX; s++)
        sci_send(d, SCI_STYLESETSIZEFRACTIONAL, s, size);
    d->ln_digits = 0;
    update_margin(d);
}

gboolean doc_is_dirty(Doc *d) {
    return sci_send(d, SCI_GETMODIFY, 0, 0) != 0;
}

gboolean doc_is_pristine(Doc *d) {
    return !d->path && !doc_is_dirty(d) && sci_send(d, SCI_GETLENGTH, 0, 0) == 0;
}

char *doc_name(Doc *d) {
    return d->path ? g_path_get_basename(d->path) : g_strdup("Untitled");
}

void doc_update_label(Doc *d) {
    char *name = doc_name(d);
    const char *mark = doc_is_dirty(d) ? "● " : (d->path && !d->on_disk ? "✕ " : "");
    char *text = g_strconcat(mark, name, NULL);
    gtk_label_set_text(GTK_LABEL(d->label), text);
    /* An ellipsizing label asks for almost no width; request the full name
       (up to the cap) so tabs show it instead of just "…". */
    glong chars = g_utf8_strlen(text, -1);
    gtk_label_set_width_chars(GTK_LABEL(d->label), (int)MIN(chars, TAB_MAX_CHARS));
    gtk_widget_set_tooltip_text(d->tab, d->path ? d->path : "Not saved yet");
    g_free(text);
    g_free(name);
}

gboolean doc_load(Doc *d, const char *path, char **err) {
    lang_apply_for_path(d, path);
    if (!fileio_load(d, path, err)) {
        lang_apply_for_path(d, d->path);
        return FALSE;
    }
    if (d->path != path) {
        g_free(d->path);
        d->path = g_strdup(path);
    }
    g_array_set_size(d->matches, 0);
    d->ln_digits = 0;
    update_margin(d);
    doc_update_label(d);
    return TRUE;
}

gboolean doc_reload(Doc *d, char **err) {
    sptr_t pos = sci_send(d, SCI_GETCURRENTPOS, 0, 0);
    sptr_t top = sci_send(d, SCI_GETFIRSTVISIBLELINE, 0, 0);

    /* Load into a fresh Scintilla document so a failed reload (file became
       unreadable or invalid UTF-8) leaves the current text untouched. */
    sptr_t old = sci_send(d, SCI_GETDOCPOINTER, 0, 0);
    sci_send(d, SCI_ADDREFDOCUMENT, 0, old);
    sptr_t fresh = sci_send(d, SCI_CREATEDOCUMENT, 0, SC_DOCUMENTOPTION_DEFAULT);
    sci_send(d, SCI_SETDOCPOINTER, 0, fresh);
    sci_send(d, SCI_RELEASEDOCUMENT, 0, fresh);   /* the view holds it now */

    char *path = g_strdup(d->path);
    gboolean ok = doc_load(d, path, err);
    g_free(path);

    if (ok) {
        sptr_t len = sci_send(d, SCI_GETLENGTH, 0, 0);
        sci_send(d, SCI_GOTOPOS, pos < len ? pos : len, 0);
        sci_send(d, SCI_SETFIRSTVISIBLELINE, top, 0);
    } else {
        sci_send(d, SCI_SETDOCPOINTER, 0, old);   /* frees the fresh one */
        lang_apply_for_path(d, d->path);
        sci_send(d, SCI_GOTOPOS, pos, 0);
        sci_send(d, SCI_SETFIRSTVISIBLELINE, top, 0);
    }
    sci_send(d, SCI_RELEASEDOCUMENT, 0, old);
    return ok;
}
