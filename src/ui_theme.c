#include "ui_theme.h"

#include <gtk/gtk.h>

#include "settings.h"

static GtkCssProvider *provider;

/* Colours are passed as #rrggbb; GTK's mix() and alpha() derive the rest so
   every theme only has to define its editor palette. */
static const char css_template[] =
    "@define-color le_bg %s;\n"
    "@define-color le_fg %s;\n"
    "@define-color le_chrome %s;\n"
    "@define-color le_sel %s;\n"
    "@define-color le_dim %s;\n"
    "@define-color le_border %s;\n"
    "@define-color le_accent %s;\n"
    "@define-color le_error %s;\n"
    "@define-color le_hover mix(@le_chrome, @le_fg, 0.08);\n"
    /* Named colours many GTK themes read for their accent (selected tab,
       focus, check marks); pointing them at the theme keeps one palette. */
    "@define-color theme_selected_bg_color @le_accent;\n"
    "@define-color theme_selected_fg_color @le_bg;\n"
    "@define-color selected_bg_color @le_accent;\n"
    "@define-color accent_color @le_accent;\n"
    "@define-color accent_bg_color @le_accent;\n"

    "window, dialog, messagedialog, .background, popover, menu, .csd.popup decoration,"
    " tooltip, .dialog-action-area {"
    "  background-color: @le_chrome; color: @le_fg; }\n"
    "tooltip { background-color: @le_bg; border: 1px solid @le_border; }\n"
    "tooltip * { color: @le_fg; }\n"
    "label { color: @le_fg; }\n"
    "label.dim-label, .dim-label { color: @le_dim; opacity: 1; }\n"
    "separator { background-color: @le_border; }\n"
    "decoration { box-shadow: 0 3px 9px 1px alpha(black, 0.35), 0 0 0 1px @le_border; }\n"

    "headerbar, .titlebar {"
    "  background-image: none; background-color: @le_chrome; color: @le_fg;"
    "  border-color: @le_border; box-shadow: none; }\n"
    "headerbar:backdrop { background-color: @le_chrome; }\n"
    "headerbar .title { color: @le_fg; }\n"
    "headerbar .subtitle { color: @le_dim; }\n"

    "button {"
    "  background-image: none; background-color: transparent; color: @le_fg;"
    "  border-color: transparent; box-shadow: none; text-shadow: none;"
    "  -gtk-icon-shadow: none; }\n"
    "button:hover { background-color: @le_hover; border-color: @le_border; }\n"
    "button:active, button:checked { background-color: @le_sel; border-color: @le_border; }\n"
    "button:disabled { color: alpha(@le_fg, 0.4); }\n"
    "button:disabled label { color: alpha(@le_fg, 0.4); }\n"
    ".linked > button { border-color: @le_border; }\n"
    "dialog button, messagedialog button, .dialog-action-area button {"
    "  border-color: @le_border; background-color: @le_bg; }\n"
    "button.suggested-action { background-color: @le_accent; color: @le_bg; }\n"

    "entry, spinbutton, spinbutton entry {"
    "  background-image: none; background-color: @le_bg; color: @le_fg;"
    "  border-color: @le_border; box-shadow: none; caret-color: @le_fg; }\n"
    "entry:focus, spinbutton:focus { border-color: @le_accent;"
    "  box-shadow: inset 0 0 0 1px @le_accent; }\n"
    "entry image { color: @le_dim; }\n"
    "entry.error { color: @le_error; border-color: @le_error; }\n"
    "spinbutton button { border-color: @le_border; }\n"
    "selection, *:selected, entry selection, label selection {"
    "  background-color: @le_sel; color: @le_fg; }\n"
    "combobox button.combo { background-color: @le_bg; border-color: @le_border; }\n"

    "switch { background-color: @le_bg; border-color: @le_border; background-image: none; }\n"
    "switch:checked { background-color: @le_accent; }\n"
    "switch slider { background-color: @le_fg; border-color: @le_border;"
    "  background-image: none; box-shadow: none; }\n"

    "notebook { background-color: @le_bg; }\n"
    "notebook > header { background-color: @le_chrome; border-color: @le_border;"
    "  box-shadow: none; }\n"
    "notebook > header > tabs > tab { color: @le_dim; background-color: transparent;"
    "  border-color: transparent; box-shadow: none; }\n"
    "notebook > header > tabs > tab label { color: @le_dim; }\n"
    "notebook > header > tabs > tab:hover { background-color: @le_hover; }\n"
    /* Same specificity as the system theme's header.top/... rules, else
       its own accent colour (often purple or blue) wins over ours. */
    "notebook > header > tabs > tab:checked,"
    " notebook > header.top > tabs > tab:checked { background-color: @le_bg;"
    "  box-shadow: inset 0 -2px @le_accent; border-color: transparent; }\n"
    "notebook > header.bottom > tabs > tab:checked { background-color: @le_bg;"
    "  box-shadow: inset 0 2px @le_accent; }\n"
    "notebook > header.top > tabs > tab:hover:not(:checked),"
    " notebook > header.bottom > tabs > tab:hover:not(:checked) {"
    "  background-color: @le_hover; box-shadow: none; }\n"
    "notebook > header.top > tabs > tab, notebook > header.bottom > tabs > tab {"
    "  border-color: transparent; }\n"
    "notebook > header.top { box-shadow: inset 0 -1px @le_border; }\n"
    "notebook > header.top > tabs > tab:checked label { color: @le_fg; }\n"
    "notebook > header > tabs > tab:checked label { color: @le_fg; }\n"
    "notebook > header > tabs > arrow { color: @le_dim; }\n"
    "notebook > stack { background-color: @le_bg; }\n"

    "scrollbar { background-color: @le_bg; border-color: @le_border; }\n"
    "scrollbar slider { background-color: alpha(@le_fg, 0.25); border-color: transparent; }\n"
    "scrollbar slider:hover { background-color: alpha(@le_fg, 0.4); }\n"
    "scrollbar slider:active { background-color: alpha(@le_fg, 0.55); }\n"

    "popover, menu { border-color: @le_border; }\n"
    "modelbutton, menuitem { color: @le_fg; }\n"
    "modelbutton:hover, menuitem:hover { background-color: @le_hover; }\n"
    "modelbutton accelerator, menuitem accelerator { color: @le_dim; }\n"
    "modelbutton:disabled { color: alpha(@le_fg, 0.4); }\n"
    "check, radio { background-image: none; background-color: @le_bg;"
    "  border-color: @le_border; color: @le_fg; }\n"

    "#le-searchbar { background-color: @le_chrome; border-top: 1px solid @le_border; }\n"
    "#le-status { color: @le_dim; }\n"

    "filechooser, filechooser .view, placessidebar, placessidebar list, treeview,"
    " treeview.view, list, row { background-color: @le_bg; color: @le_fg; }\n"
    "treeview.view:selected, row:selected { background-color: @le_sel; }\n"
    "treeview.view header button { background-color: @le_chrome; color: @le_dim; }\n";

static void hex(char out[8], unsigned c) {
    g_snprintf(out, 8, "#%06x", c & 0xffffff);
}

void ui_theme_apply(void) {
    const Theme *t = settings_theme();
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme",
                 t->dark, NULL);

    if (!provider) {
        provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    char bg[8], fg[8], chrome[8], sel[8], dim[8], border[8], accent[8], err[8];
    hex(bg, t->bg);
    hex(fg, t->syn[R_FG]);
    hex(chrome, t->margin == t->bg ? t->caret_line : t->margin);
    hex(sel, t->sel);
    hex(dim, t->line_no);
    hex(border, t->whitespace);
    hex(accent, t->accent);
    hex(err, t->syn[R_DELETED]);

    char *base = g_strdup_printf(css_template, bg, fg, chrome, sel, dim, border, accent, err);
    /* Interface font size; the editor text has its own setting. Applied to
       every toplevel kind, children inherit it. */
    GString *css = g_string_new(base);
    g_free(base);
    if (settings.ui_font[0] || settings.ui_font_size >= 6) {
        g_string_append(css, "window, dialog, popover, menu, tooltip, .popup {");
        if (settings.ui_font[0]) {
            /* Quote the family; strip quotes a name could carry. */
            char *fam = g_strdelimit(g_strdup(settings.ui_font), "\"\\;{}", ' ');
            g_string_append_printf(css, " font-family: \"%s\";", fam);
            g_free(fam);
        }
        if (settings.ui_font_size >= 6) {
            char num[G_ASCII_DTOSTR_BUF_SIZE];
            g_string_append_printf(css, " font-size: %spt;",
                                   g_ascii_dtostr(num, sizeof num, settings.ui_font_size));
        }
        g_string_append(css, " }\n");
    }
    GError *e = NULL;
    if (!gtk_css_provider_load_from_data(provider, css->str, -1, &e)) {
        g_warning("Theme CSS: %s", e->message);
        g_error_free(e);
    }
    g_string_free(css, TRUE);
}
