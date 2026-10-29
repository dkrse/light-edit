#include "prefs.h"

#include "settings.h"
#include "window.h"

/* Only offer monospaced families: this is a code and text editor. */
static gboolean mono_filter(const PangoFontFamily *family, const PangoFontFace *face,
                            gpointer data) {
    (void)face; (void)data;
    return pango_font_family_is_monospace((PangoFontFamily *)family);
}

static void on_font(GtkFontButton *b, gpointer data) {
    PangoFontFamily *fam = gtk_font_chooser_get_font_family(GTK_FONT_CHOOSER(b));
    if (!fam) return;
    g_strlcpy(settings.font, pango_font_family_get_name(fam), sizeof settings.font);
    win_apply_settings(data);
}

static void on_size(GtkSpinButton *b, gpointer data) {
    settings.font_size = gtk_spin_button_get_value(b);
    win_apply_settings(data);
}

/* ── Interface font: any family and size, or the desktop's own ── */

static void set_ui_button(GtkWidget *b) {
    if (settings.ui_font[0]) {
        char *desc = g_strdup_printf("%s %g", settings.ui_font,
                                     settings.ui_font_size >= 6 ? settings.ui_font_size : 10);
        gtk_font_chooser_set_font(GTK_FONT_CHOOSER(b), desc);
        g_free(desc);
    } else {
        char *sys = NULL;
        g_object_get(gtk_settings_get_default(), "gtk-font-name", &sys, NULL);
        if (sys) gtk_font_chooser_set_font(GTK_FONT_CHOOSER(b), sys);
        g_free(sys);
    }
}

static void on_ui_font(GtkFontButton *b, gpointer data) {
    PangoFontDescription *fd = gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(b));
    if (!fd) return;
    const char *fam = pango_font_description_get_family(fd);
    if (fam) g_strlcpy(settings.ui_font, fam, sizeof settings.ui_font);
    int sz = pango_font_description_get_size(fd);
    if (sz > 0) settings.ui_font_size = (double)sz / PANGO_SCALE;
    pango_font_description_free(fd);
    win_apply_settings(data);
}

static void on_ui_system(GtkButton *b, gpointer data) {
    (void)b;
    settings.ui_font[0] = '\0';
    settings.ui_font_size = 0;
    set_ui_button(g_object_get_data(G_OBJECT(b), "font-button"));
    win_apply_settings(data);
}

/* ── Text intensity: below 100 % the text fades towards the background,
   above it the strokes get heavier. Restyling is debounced while dragging. */

static guint intensity_id;

static gboolean apply_intensity(gpointer data) {
    intensity_id = 0;
    win_apply_settings(data);
    return G_SOURCE_REMOVE;
}

static void on_intensity(GtkRange *r, gpointer data) {
    settings.text_intensity = (int)(gtk_range_get_value(r) + 0.5);
    if (intensity_id) g_source_remove(intensity_id);
    intensity_id = g_timeout_add(80, apply_intensity, data);
}

static gchar *intensity_label(GtkScale *s, gdouble v, gpointer data) {
    (void)s; (void)data;
    return g_strdup_printf("%d %%", (int)(v + 0.5));
}

static void on_theme(GtkComboBox *c, gpointer data) {
    int i = gtk_combo_box_get_active(c);
    if (i < 0 || i >= theme_count) return;
    settings.theme = i;
    win_apply_settings(data);
}

static void on_tab_width(GtkSpinButton *b, gpointer data) {
    settings.tab_width = gtk_spin_button_get_value_as_int(b);
    win_apply_settings(data);
}

static gboolean on_switch(GtkSwitch *sw, gboolean state, gpointer data) {
    gboolean *field = g_object_get_data(G_OBJECT(sw), "field");
    *field = state;
    win_apply_settings(data);
    return FALSE;
}

static void add_row(GtkGrid *g, int row, const char *text, GtkWidget *w) {
    GtkWidget *l = gtk_label_new_with_mnemonic(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), w);
    gtk_widget_set_hexpand(l, TRUE);
    gtk_grid_attach(g, l, 0, row, 1, 1);
    gtk_grid_attach(g, w, 1, row, 1, 1);
}

static GtkWidget *switch_for(gboolean *field, Win *w) {
    GtkWidget *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), *field);
    gtk_widget_set_halign(sw, GTK_ALIGN_END);
    g_object_set_data(G_OBJECT(sw), "field", field);
    g_signal_connect(sw, "state-set", G_CALLBACK(on_switch), w);
    return sw;
}

void prefs_show(Win *w) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons("Settings", GTK_WINDOW(w->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR,
        NULL, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 420, -1);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 24);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 18);
    GtkGrid *g = GTK_GRID(grid);
    int row = 0;

    GtkWidget *font = gtk_font_button_new();
    gtk_font_chooser_set_level(GTK_FONT_CHOOSER(font), GTK_FONT_CHOOSER_LEVEL_FAMILY);
    gtk_font_chooser_set_filter_func(GTK_FONT_CHOOSER(font), mono_filter, NULL, NULL);
    char *desc = g_strdup_printf("%s %g", settings.font, settings.font_size);
    gtk_font_chooser_set_font(GTK_FONT_CHOOSER(font), desc);
    g_free(desc);
    g_signal_connect(font, "font-set", G_CALLBACK(on_font), w);
    add_row(g, row++, "_Font", font);

    GtkWidget *size = gtk_spin_button_new_with_range(6, 72, 0.5);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(size), settings.font_size);
    g_signal_connect(size, "value-changed", G_CALLBACK(on_size), w);
    add_row(g, row++, "Font _size", size);

    GtkWidget *inten = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 30, 170, 5);
    gtk_range_set_value(GTK_RANGE(inten), settings.text_intensity);
    gtk_scale_add_mark(GTK_SCALE(inten), 100, GTK_POS_BOTTOM, NULL);
    gtk_scale_set_value_pos(GTK_SCALE(inten), GTK_POS_RIGHT);
    gtk_widget_set_size_request(inten, 200, -1);
    gtk_widget_set_tooltip_text(inten,
        "Lower: lighter text blended into the background.\n"
        "Higher: heavier strokes. 100 % is the theme as designed.");
    g_signal_connect(inten, "format-value", G_CALLBACK(intensity_label), NULL);
    g_signal_connect(inten, "value-changed", G_CALLBACK(on_intensity), w);
    add_row(g, row++, "Text _intensity", inten);

    GtkWidget *uifont = gtk_font_button_new();
    gtk_font_chooser_set_level(GTK_FONT_CHOOSER(uifont),
        GTK_FONT_CHOOSER_LEVEL_FAMILY | GTK_FONT_CHOOSER_LEVEL_SIZE);
    gtk_font_button_set_use_font(GTK_FONT_BUTTON(uifont), FALSE);
    set_ui_button(uifont);
    g_signal_connect(uifont, "font-set", G_CALLBACK(on_ui_font), w);
    GtkWidget *sys = gtk_button_new_with_label("System");
    gtk_widget_set_tooltip_text(sys, "Use the desktop's interface font");
    g_object_set_data(G_OBJECT(sys), "font-button", uifont);
    g_signal_connect(sys, "clicked", G_CALLBACK(on_ui_system), w);
    GtkWidget *uibox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(uibox), "linked");
    gtk_box_pack_start(GTK_BOX(uibox), uifont, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(uibox), sys, FALSE, FALSE, 0);
    add_row(g, row++, "_Interface font", uibox);

    GtkWidget *theme = gtk_combo_box_text_new();
    for (int i = 0; i < theme_count; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme), themes[i].name);
    gtk_combo_box_set_active(GTK_COMBO_BOX(theme), settings.theme);
    g_signal_connect(theme, "changed", G_CALLBACK(on_theme), w);
    add_row(g, row++, "_Theme", theme);

    GtkWidget *tabw = gtk_spin_button_new_with_range(1, 16, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(tabw), settings.tab_width);
    g_signal_connect(tabw, "value-changed", G_CALLBACK(on_tab_width), w);
    add_row(g, row++, "Tab _width", tabw);

    add_row(g, row++, "Insert spaces instead of tabs", switch_for(&settings.use_spaces, w));
    add_row(g, row++, "Show whitespace", switch_for(&settings.show_whitespace, w));
    add_row(g, row++, "Line numbers", switch_for(&settings.line_numbers, w));
    add_row(g, row++, "Highlight current line", switch_for(&settings.highlight_line, w));
    add_row(g, row++, "Word wrap", switch_for(&settings.wrap, w));

    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), grid);
    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    if (intensity_id) {                   /* last slider move still pending */
        g_source_remove(intensity_id);
        apply_intensity(w);
    }
    settings_save();
}
