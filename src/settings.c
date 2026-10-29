#include "settings.h"

#include <stdio.h>

Settings settings;

static char *config_path(void) {
    return g_build_filename(g_get_user_config_dir(), "light-edit", "settings.ini", NULL);
}

static int clamp_int(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void settings_load(void) {
    g_strlcpy(settings.font, "Monospace", sizeof settings.font);
    settings.font_size = 11.0;
    settings.theme = 0;
    settings.tab_width = 4;
    settings.use_spaces = FALSE;
    settings.show_whitespace = FALSE;
    settings.line_numbers = TRUE;
    settings.wrap = FALSE;
    settings.highlight_line = TRUE;
    settings.ui_font[0] = '\0';
    settings.ui_font_size = 0;
    settings.text_intensity = 100;

    char *path = config_path();
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        GError *e = NULL;
        char *s = g_key_file_get_string(kf, "editor", "font", NULL);
        if (s && *s) g_strlcpy(settings.font, s, sizeof settings.font);
        g_free(s);

        double sz = g_key_file_get_double(kf, "editor", "font_size", &e);
        if (!e && sz >= 4 && sz <= 96) settings.font_size = sz;
        g_clear_error(&e);

        s = g_key_file_get_string(kf, "interface", "font", NULL);
        if (s) g_strlcpy(settings.ui_font, s, sizeof settings.ui_font);
        g_free(s);

        int ti = g_key_file_get_integer(kf, "editor", "text_intensity", &e);
        if (!e) settings.text_intensity = clamp_int(ti, 30, 170);
        g_clear_error(&e);

        double ui = g_key_file_get_double(kf, "interface", "font_size", &e);
        if (!e && (ui == 0 || (ui >= 6 && ui <= 32))) settings.ui_font_size = ui;
        g_clear_error(&e);

        s = g_key_file_get_string(kf, "editor", "theme", NULL);
        settings.theme = theme_find(s);
        g_free(s);

        int tw = g_key_file_get_integer(kf, "editor", "tab_width", &e);
        if (!e) settings.tab_width = clamp_int(tw, 1, 16);
        g_clear_error(&e);

#define LOAD_BOOL(key, field) do { \
            gboolean v = g_key_file_get_boolean(kf, "editor", key, &e); \
            if (!e) settings.field = v; \
            g_clear_error(&e); \
        } while (0)
        LOAD_BOOL("use_spaces", use_spaces);
        LOAD_BOOL("show_whitespace", show_whitespace);
        LOAD_BOOL("line_numbers", line_numbers);
        LOAD_BOOL("wrap", wrap);
        LOAD_BOOL("highlight_line", highlight_line);
#undef LOAD_BOOL
    }
    g_key_file_free(kf);
    g_free(path);
}

void settings_save(void) {
    GKeyFile *kf = g_key_file_new();
    g_key_file_set_string(kf, "editor", "font", settings.font);
    g_key_file_set_double(kf, "editor", "font_size", settings.font_size);
    g_key_file_set_string(kf, "editor", "theme", settings_theme()->name);
    g_key_file_set_integer(kf, "editor", "tab_width", settings.tab_width);
    g_key_file_set_boolean(kf, "editor", "use_spaces", settings.use_spaces);
    g_key_file_set_boolean(kf, "editor", "show_whitespace", settings.show_whitespace);
    g_key_file_set_boolean(kf, "editor", "line_numbers", settings.line_numbers);
    g_key_file_set_boolean(kf, "editor", "wrap", settings.wrap);
    g_key_file_set_boolean(kf, "editor", "highlight_line", settings.highlight_line);
    g_key_file_set_integer(kf, "editor", "text_intensity", settings.text_intensity);
    g_key_file_set_string(kf, "interface", "font", settings.ui_font);
    g_key_file_set_double(kf, "interface", "font_size", settings.ui_font_size);

    char *path = config_path();
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);

    gsize len;
    char *data = g_key_file_to_data(kf, &len, NULL);
    GError *e = NULL;
    if (!g_file_set_contents(path, data, (gssize)len, &e)) {   /* atomic */
        g_warning("Cannot save settings: %s", e->message);
        g_error_free(e);
    }
    g_free(data);
    g_free(dir);
    g_free(path);
    g_key_file_free(kf);
}
