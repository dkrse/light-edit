#include "print.h"

#include "doc.h"

#define HEADER_H 24.0   /* points reserved above the text for the header */

typedef struct {
    Doc    *d;
    char   *title;
    GArray *pages;      /* sptr_t start position of each page */
} PrintJob;

static GtkPrintSettings *last_settings;
static GtkPageSetup     *last_setup;

static void fill_range(struct Sci_RangeToFormatFull *fr, cairo_t *cr, double w, double h) {
    fr->hdc = cr;
    fr->hdcTarget = cr;
    fr->rc.left = 0;
    fr->rc.top = (int)HEADER_H;
    fr->rc.right = (int)w;
    fr->rc.bottom = (int)h;
    fr->rcPage.left = 0;
    fr->rcPage.top = 0;
    fr->rcPage.right = (int)w;
    fr->rcPage.bottom = (int)h;
}

/* Paginate once up front: Scintilla tells where each page ends. */
static void on_begin(GtkPrintOperation *op, GtkPrintContext *ctx, gpointer data) {
    PrintJob *pj = data;
    Doc *d = pj->d;
    cairo_t *cr = gtk_print_context_get_cairo_context(ctx);
    double w = gtk_print_context_get_width(ctx);
    double h = gtk_print_context_get_height(ctx);
    sptr_t len = sci_send(d, SCI_GETLENGTH, 0, 0);

    g_array_set_size(pj->pages, 0);   /* begin-print can run again for preview */
    struct Sci_RangeToFormatFull fr;
    fill_range(&fr, cr, w, h);
    sptr_t pos = 0;
    do {
        g_array_append_val(pj->pages, pos);
        fr.chrg.cpMin = pos;
        fr.chrg.cpMax = len;
        sptr_t next = sci_send(d, SCI_FORMATRANGEFULL, FALSE, (sptr_t)&fr);
        if (next <= pos) break;                 /* no progress: stop */
        pos = next;
    } while (pos < len);
    gtk_print_operation_set_n_pages(op, (gint)pj->pages->len);
}

static void draw_header(PrintJob *pj, GtkPrintContext *ctx, cairo_t *cr, int page) {
    double w = gtk_print_context_get_width(ctx);
    PangoLayout *lay = gtk_print_context_create_pango_layout(ctx);
    PangoFontDescription *fd = pango_font_description_from_string("Sans 8");
    pango_layout_set_font_description(lay, fd);
    pango_font_description_free(fd);

    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    pango_layout_set_text(lay, pj->title, -1);
    pango_layout_set_width(lay, (int)(w * 0.75 * PANGO_SCALE));
    pango_layout_set_ellipsize(lay, PANGO_ELLIPSIZE_START);
    cairo_move_to(cr, 0, 0);
    pango_cairo_show_layout(cr, lay);

    char num[48];
    g_snprintf(num, sizeof num, "%d / %u", page + 1, pj->pages->len);
    pango_layout_set_width(lay, -1);
    pango_layout_set_text(lay, num, -1);
    int tw, th;
    pango_layout_get_pixel_size(lay, &tw, &th);
    cairo_move_to(cr, w - tw, 0);
    pango_cairo_show_layout(cr, lay);

    cairo_set_line_width(cr, 0.5);
    cairo_move_to(cr, 0, th + 4);
    cairo_line_to(cr, w, th + 4);
    cairo_stroke(cr);
    g_object_unref(lay);
}

static void on_draw_page(GtkPrintOperation *op, GtkPrintContext *ctx, gint page, gpointer data) {
    (void)op;
    PrintJob *pj = data;
    Doc *d = pj->d;
    cairo_t *cr = gtk_print_context_get_cairo_context(ctx);
    double w = gtk_print_context_get_width(ctx);
    double h = gtk_print_context_get_height(ctx);

    draw_header(pj, ctx, cr, page);

    struct Sci_RangeToFormatFull fr;
    fill_range(&fr, cr, w, h);
    fr.chrg.cpMin = g_array_index(pj->pages, sptr_t, page);
    fr.chrg.cpMax = (guint)page + 1 < pj->pages->len
        ? g_array_index(pj->pages, sptr_t, page + 1)
        : sci_send(d, SCI_GETLENGTH, 0, 0);
    sci_send(d, SCI_FORMATRANGEFULL, TRUE, (sptr_t)&fr);
}

static void on_end(GtkPrintOperation *op, GtkPrintContext *ctx, gpointer data) {
    (void)op; (void)ctx;
    PrintJob *pj = data;
    sci_send(pj->d, SCI_FORMATRANGEFULL, FALSE, 0);   /* drop Scintilla's cache */
}

void print_doc(Win *w, Doc *d) {
    PrintJob pj = { d, d->path ? g_strdup(d->path) : g_strdup("Untitled"),
                    g_array_new(FALSE, FALSE, sizeof(sptr_t)) };

    /* Paper is white whatever the theme; keep syntax colours readable. */
    sci_send(d, SCI_SETPRINTCOLOURMODE, SC_PRINT_COLOURONWHITE, 0);
    sci_send(d, SCI_SETPRINTWRAPMODE, SC_WRAP_WORD, 0);
    sci_send(d, SCI_SETPRINTMAGNIFICATION, 0, 0);

    GtkPrintOperation *op = gtk_print_operation_new();
    gtk_print_operation_set_job_name(op, pj.title);
    gtk_print_operation_set_unit(op, GTK_UNIT_POINTS);
    gtk_print_operation_set_embed_page_setup(op, TRUE);
    if (last_settings) gtk_print_operation_set_print_settings(op, last_settings);
    if (last_setup) gtk_print_operation_set_default_page_setup(op, last_setup);
    g_signal_connect(op, "begin-print", G_CALLBACK(on_begin), &pj);
    g_signal_connect(op, "draw-page", G_CALLBACK(on_draw_page), &pj);
    g_signal_connect(op, "end-print", G_CALLBACK(on_end), &pj);

    GError *err = NULL;
    GtkPrintOperationResult r = gtk_print_operation_run(op,
        GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, GTK_WINDOW(w->window), &err);
    if (r == GTK_PRINT_OPERATION_RESULT_APPLY) {
        g_set_object(&last_settings, gtk_print_operation_get_print_settings(op));
        g_set_object(&last_setup, gtk_print_operation_get_default_page_setup(op));
    } else if (r == GTK_PRINT_OPERATION_RESULT_ERROR) {
        GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(w->window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "Printing failed: %s",
            err ? err->message : "unknown error");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
    }
    g_clear_error(&err);
    g_object_unref(op);       /* synchronous run: the job is finished here */
    g_array_free(pj.pages, TRUE);
    g_free(pj.title);
}
