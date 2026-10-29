#include "search.h"

#include <string.h>

#include "window.h"

/* Highlighting stops here so a one-letter search in a huge file stays fast;
   navigation (next/previous) still works past the limit. */
#define MAX_MATCHES 100000
#define REFRESH_MS  250

struct Search {
    Win       *w;
    GtkWidget *root, *find, *replace, *replace_row, *count;
    GtkWidget *case_btn, *word_btn, *regex_btn;
    guint      refresh_id;
};

/* ── Core ── */

static int search_flags(Search *s) {
    int f = 0;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->case_btn))) f |= SCFIND_MATCHCASE;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->word_btn))) f |= SCFIND_WHOLEWORD;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->regex_btn)))
        f |= SCFIND_REGEXP | SCFIND_CXX11REGEX;
    return f;
}

static gboolean is_regex(Search *s) {
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->regex_btn));
}

static const char *needle(Search *s) {
    return gtk_entry_get_text(GTK_ENTRY(s->find));
}

/* Searches [from, to) (backwards when from > to). Returns the match start,
   -1 when nothing is found, -2 for an invalid regular expression. */
static sptr_t find_in(Doc *d, Search *s, sptr_t from, sptr_t to, sptr_t *end) {
    const char *t = needle(s);
    sci_send(d, SCI_SETSEARCHFLAGS, search_flags(s), 0);
    sci_send(d, SCI_SETTARGETRANGE, from, to);
    sptr_t r = sci_send(d, SCI_SEARCHINTARGET, strlen(t), (sptr_t)t);
    if (r >= 0) *end = sci_send(d, SCI_GETTARGETEND, 0, 0);
    return r;
}

static void set_error(Search *s, gboolean error) {
    GtkStyleContext *sc = gtk_widget_get_style_context(s->find);
    if (error) gtk_style_context_add_class(sc, "error");
    else gtk_style_context_remove_class(sc, "error");
}

static void clear_matches(Doc *d) {
    sci_send(d, SCI_SETINDICATORCURRENT, IND_FIND, 0);
    sci_send(d, SCI_INDICATORCLEARRANGE, 0, sci_send(d, SCI_GETLENGTH, 0, 0));
    g_array_set_size(d->matches, 0);
    d->matches_capped = FALSE;
    if (d->marks) gtk_widget_queue_draw(d->marks);
}

/* "3 of 42" when the selection is a match, else just the total. */
static void update_count(Search *s, Doc *d) {
    if (!*needle(s)) {
        gtk_label_set_text(GTK_LABEL(s->count), "");
        return;
    }
    guint n = d->matches->len;
    if (n == 0) {
        gtk_label_set_text(GTK_LABEL(s->count), "No results");
        return;
    }
    const char *plus = d->matches_capped ? "+" : "";
    sptr_t sel = sci_send(d, SCI_GETSELECTIONSTART, 0, 0);
    guint lo = 0, hi = n;                      /* lower bound of sel */
    while (lo < hi) {
        guint mid = lo + (hi - lo) / 2;
        if (g_array_index(d->matches, sptr_t, mid) < sel) lo = mid + 1;
        else hi = mid;
    }
    char buf[64];
    if (lo < n && g_array_index(d->matches, sptr_t, lo) == sel &&
        sci_send(d, SCI_GETSELECTIONEND, 0, 0) > sel)
        snprintf(buf, sizeof buf, "%u of %u%s", lo + 1, n, plus);
    else
        snprintf(buf, sizeof buf, "%u%s match%s", n, plus, n == 1 ? "" : "es");
    gtk_label_set_text(GTK_LABEL(s->count), buf);
}

static void highlight_all(Search *s, Doc *d) {
    clear_matches(d);
    set_error(s, FALSE);
    if (!*needle(s)) {
        update_count(s, d);
        return;
    }
    sptr_t len = sci_send(d, SCI_GETLENGTH, 0, 0);
    sptr_t pos = 0, end = 0;
    while (pos <= len) {
        sptr_t r = find_in(d, s, pos, len, &end);
        if (r == -2) { set_error(s, TRUE); break; }
        if (r < 0) break;
        if (d->matches->len >= MAX_MATCHES) { d->matches_capped = TRUE; break; }
        g_array_append_val(d->matches, r);
        if (end > r) {
            sci_send(d, SCI_INDICATORFILLRANGE, r, end - r);
            pos = end;
        } else {                               /* empty regex match */
            sptr_t next = sci_send(d, SCI_POSITIONAFTER, end, 0);
            if (next <= end) break;
            pos = next;
        }
    }
    if (d->marks) gtk_widget_queue_draw(d->marks);
    update_count(s, d);
}

static void select_match(Doc *d, sptr_t start, sptr_t end) {
    sptr_t line = sci_send(d, SCI_LINEFROMPOSITION, start, 0);
    sci_send(d, SCI_ENSUREVISIBLEENFORCEPOLICY, line, 0);
    sci_send(d, SCI_SETSEL, start, end);
    sci_send(d, SCI_SCROLLRANGE, end, start);
}

/* Finds from `from` in the given direction, wrapping around the document. */
static gboolean find_from(Search *s, Doc *d, sptr_t from, gboolean forward) {
    if (!*needle(s)) return FALSE;
    sptr_t len = sci_send(d, SCI_GETLENGTH, 0, 0);
    sptr_t end = 0;
    sptr_t r = find_in(d, s, from, forward ? len : 0, &end);
    if (r == -2) { set_error(s, TRUE); return FALSE; }
    if (r < 0) r = find_in(d, s, forward ? 0 : len, from, &end);
    if (r < 0) {
        gtk_widget_error_bell(s->find);
        return FALSE;
    }
    select_match(d, r, end);
    update_count(s, d);
    return TRUE;
}

void search_next(Search *s, gboolean forward) {
    Doc *d = win_current(s->w);
    if (!d || !*needle(s)) return;
    sptr_t a = sci_send(d, SCI_GETSELECTIONSTART, 0, 0);
    sptr_t b = sci_send(d, SCI_GETSELECTIONEND, 0, 0);
    sptr_t from = forward ? b : a;
    /* Step past an empty match at the caret, or we would find it again. */
    if (forward && a == b) {
        sptr_t end;
        if (find_in(d, s, a, a, &end) == a) from = sci_send(d, SCI_POSITIONAFTER, a, 0);
    }
    find_from(s, d, from, forward);
}

/* TRUE when the selection is exactly a match; leaves it as the target. */
static gboolean selection_is_match(Search *s, Doc *d) {
    sptr_t a = sci_send(d, SCI_GETSELECTIONSTART, 0, 0);
    sptr_t b = sci_send(d, SCI_GETSELECTIONEND, 0, 0);
    sptr_t end;
    return a != b && find_in(d, s, a, b, &end) == a && end == b;
}

static sptr_t replace_target(Search *s, Doc *d) {
    const char *rep = gtk_entry_get_text(GTK_ENTRY(s->replace));
    sci_send(d, is_regex(s) ? SCI_REPLACETARGETRE : SCI_REPLACETARGET,
             strlen(rep), (sptr_t)rep);
    return sci_send(d, SCI_GETTARGETEND, 0, 0);
}

static void replace_one(Search *s) {
    Doc *d = win_current(s->w);
    if (!d || !*needle(s)) return;
    if (selection_is_match(s, d)) {
        sptr_t end = replace_target(s, d);
        sci_send(d, SCI_SETSEL, end, end);
    }
    search_next(s, TRUE);
}

static void replace_all(Search *s) {
    Doc *d = win_current(s->w);
    if (!d || !*needle(s)) return;
    guint n = 0;
    sptr_t pos = 0, end = 0;

    sci_send(d, SCI_BEGINUNDOACTION, 0, 0);   /* one undo step for all */
    for (;;) {
        sptr_t len = sci_send(d, SCI_GETLENGTH, 0, 0);
        if (pos > len) break;
        sptr_t r = find_in(d, s, pos, len, &end);
        if (r == -2) { set_error(s, TRUE); break; }
        if (r < 0) break;
        sptr_t new_end = replace_target(s, d);
        n++;
        if (end > r) {
            pos = new_end;
        } else {
            sptr_t next = sci_send(d, SCI_POSITIONAFTER, new_end, 0);
            if (next <= new_end) break;
            pos = next;
        }
    }
    sci_send(d, SCI_ENDUNDOACTION, 0, 0);

    highlight_all(s, d);
    char buf[64];
    snprintf(buf, sizeof buf, "Replaced %u", n);
    gtk_label_set_text(GTK_LABEL(s->count), buf);
}

/* ── Refresh scheduling ── */

static gboolean refresh_cb(gpointer data) {
    Search *s = data;
    s->refresh_id = 0;
    Doc *d = win_current(s->w);
    if (d && search_visible(s)) highlight_all(s, d);
    return G_SOURCE_REMOVE;
}

void search_doc_changed(Search *s, Doc *d) {
    (void)d;
    if (!search_visible(s) || !*needle(s)) return;
    if (s->refresh_id) g_source_remove(s->refresh_id);
    s->refresh_id = g_timeout_add(REFRESH_MS, refresh_cb, s);
}

void search_doc_switched(Search *s) {
    Doc *d = win_current(s->w);
    if (d && search_visible(s)) highlight_all(s, d);
}

/* ── UI ── */

gboolean search_visible(Search *s) {
    return gtk_revealer_get_reveal_child(GTK_REVEALER(s->root));
}

void search_show(Search *s, gboolean replace) {
    Doc *d = win_current(s->w);
    if (d) {
        sptr_t a = sci_send(d, SCI_GETSELECTIONSTART, 0, 0);
        sptr_t b = sci_send(d, SCI_GETSELECTIONEND, 0, 0);
        if (b > a && b - a < 512 &&
            sci_send(d, SCI_LINEFROMPOSITION, a, 0) == sci_send(d, SCI_LINEFROMPOSITION, b, 0)) {
            char *text = g_malloc((size_t)(b - a) + 1);
            sci_send(d, SCI_GETSELTEXT, 0, (sptr_t)text);
            gtk_entry_set_text(GTK_ENTRY(s->find), text);   /* triggers search */
            g_free(text);
        }
    }
    gtk_widget_set_visible(s->replace_row, replace);
    gtk_revealer_set_reveal_child(GTK_REVEALER(s->root), TRUE);
    win_update_actions(s->w);
    gtk_widget_grab_focus(s->find);
    if (d) highlight_all(s, d);
}

void search_hide(Search *s) {
    gtk_revealer_set_reveal_child(GTK_REVEALER(s->root), FALSE);
    if (s->refresh_id) {
        g_source_remove(s->refresh_id);
        s->refresh_id = 0;
    }
    Doc *d = win_current(s->w);
    if (d) {
        clear_matches(d);
        gtk_widget_grab_focus(d->sci);
    }
    win_update_actions(s->w);
}

static void on_find_changed(GtkSearchEntry *e, gpointer data) {
    (void)e;
    Search *s = data;
    Doc *d = win_current(s->w);
    if (!d) return;
    highlight_all(s, d);
    /* Search as you type, starting where the current selection begins. */
    if (*needle(s)) find_from(s, d, sci_send(d, SCI_GETSELECTIONSTART, 0, 0), TRUE);
}

static void on_options_toggled(GtkToggleButton *b, gpointer data) {
    (void)b;
    on_find_changed(NULL, data);
}

static gboolean on_find_key(GtkWidget *w, GdkEventKey *ev, gpointer data) {
    (void)w;
    Search *s = data;
    if (ev->keyval == GDK_KEY_Return || ev->keyval == GDK_KEY_KP_Enter) {
        search_next(s, !(ev->state & GDK_SHIFT_MASK));
        return TRUE;
    }
    if (ev->keyval == GDK_KEY_Escape) {
        search_hide(s);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_replace_key(GtkWidget *w, GdkEventKey *ev, gpointer data) {
    (void)w;
    Search *s = data;
    if (ev->keyval == GDK_KEY_Return || ev->keyval == GDK_KEY_KP_Enter) {
        if (ev->state & GDK_CONTROL_MASK) replace_all(s);
        else replace_one(s);
        return TRUE;
    }
    if (ev->keyval == GDK_KEY_Escape) {
        search_hide(s);
        return TRUE;
    }
    return FALSE;
}

static void on_prev(GtkButton *b, gpointer s)        { (void)b; search_next(s, FALSE); }
static void on_next(GtkButton *b, gpointer s)        { (void)b; search_next(s, TRUE); }
static void on_replace(GtkButton *b, gpointer s)     { (void)b; replace_one(s); }
static void on_replace_all(GtkButton *b, gpointer s) { (void)b; replace_all(s); }
static void on_close(GtkButton *b, gpointer s)       { (void)b; search_hide(s); }

static GtkWidget *icon_button(const char *icon, const char *tip, GCallback cb, Search *s) {
    GtkWidget *b = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(b, tip);
    gtk_widget_set_focus_on_click(b, FALSE);
    g_signal_connect(b, "clicked", cb, s);
    return b;
}

static GtkWidget *toggle(const char *label, const char *tip, Search *s) {
    GtkWidget *b = gtk_toggle_button_new_with_label(label);
    gtk_widget_set_tooltip_text(b, tip);
    gtk_widget_set_focus_on_click(b, FALSE);
    g_signal_connect(b, "toggled", G_CALLBACK(on_options_toggled), s);
    return b;
}

Search *search_new(Win *w) {
    Search *s = g_new0(Search, 1);
    s->w = w;

    s->find = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(s->find), "Find");
    gtk_widget_set_hexpand(s->find, TRUE);
    g_signal_connect(s->find, "search-changed", G_CALLBACK(on_find_changed), s);
    g_signal_connect(s->find, "key-press-event", G_CALLBACK(on_find_key), s);

    s->count = gtk_label_new(NULL);
    gtk_label_set_width_chars(GTK_LABEL(s->count), 14);
    gtk_label_set_xalign(GTK_LABEL(s->count), 1.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(s->count), "dim-label");

    s->case_btn  = toggle("Aa", "Match Case", s);
    s->word_btn  = toggle("W", "Whole Word", s);
    s->regex_btn = toggle(".*", "Regular Expression", s);

    GtkWidget *nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(nav), "linked");
    gtk_container_add(GTK_CONTAINER(nav), icon_button("go-up-symbolic", "Previous (Shift+Enter, Shift+F3)", G_CALLBACK(on_prev), s));
    gtk_container_add(GTK_CONTAINER(nav), icon_button("go-down-symbolic", "Next (Enter, F3)", G_CALLBACK(on_next), s));

    GtkWidget *opts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(opts), "linked");
    gtk_container_add(GTK_CONTAINER(opts), s->case_btn);
    gtk_container_add(GTK_CONTAINER(opts), s->word_btn);
    gtk_container_add(GTK_CONTAINER(opts), s->regex_btn);

    GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(row1), s->find, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row1), s->count, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row1), nav, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row1), opts, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row1),
        icon_button("window-close-symbolic", "Close (Esc)", G_CALLBACK(on_close), s), FALSE, FALSE, 0);

    s->replace = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(s->replace), "Replace with");
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(s->replace), GTK_ENTRY_ICON_PRIMARY,
                                      "edit-find-replace-symbolic");
    gtk_widget_set_hexpand(s->replace, TRUE);
    g_signal_connect(s->replace, "key-press-event", G_CALLBACK(on_replace_key), s);

    GtkWidget *rep = gtk_button_new_with_mnemonic("_Replace");
    gtk_widget_set_tooltip_text(rep, "Replace and find next (Enter)");
    g_signal_connect(rep, "clicked", G_CALLBACK(on_replace), s);
    GtkWidget *rep_all = gtk_button_new_with_mnemonic("Replace _All");
    gtk_widget_set_tooltip_text(rep_all, "Replace all (Ctrl+Enter), one undo step");
    g_signal_connect(rep_all, "clicked", G_CALLBACK(on_replace_all), s);

    s->replace_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(s->replace_row), s->replace, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(s->replace_row), rep, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(s->replace_row), rep_all, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(s->replace_row, TRUE);
    gtk_widget_show_all(s->replace_row);
    gtk_widget_hide(s->replace_row);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 6);
    gtk_widget_set_name(box, "le-searchbar");
    gtk_box_pack_start(GTK_BOX(box), row1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), s->replace_row, FALSE, FALSE, 0);

    s->root = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(s->root), GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_revealer_set_transition_duration(GTK_REVEALER(s->root), 120);
    gtk_container_add(GTK_CONTAINER(s->root), box);
    return s;
}

GtkWidget *search_widget(Search *s) {
    return s->root;
}

void search_free(Search *s) {
    if (s->refresh_id) g_source_remove(s->refresh_id);
    g_free(s);
}
