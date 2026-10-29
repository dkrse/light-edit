#pragma once

#include <glib.h>

#include "theme.h"

typedef struct {
    char     font[128];     /* family name */
    double   font_size;     /* points */
    int      theme;         /* index into themes[] */
    int      tab_width;
    gboolean use_spaces;
    gboolean show_whitespace;
    gboolean line_numbers;
    gboolean wrap;
    gboolean highlight_line;   /* background on the caret line */
    char     ui_font[128];     /* interface font family; "" = desktop font */
    double   ui_font_size;     /* menus, tabs, dialogs; 0 = desktop size */
    int      text_intensity;   /* percent: <100 fades, >100 heavier */
} Settings;

extern Settings settings;

/* Defaults first, then ~/.config/light-edit/settings.ini if present. */
void settings_load(void);
/* Atomic write of the settings file. */
void settings_save(void);

static inline const Theme *settings_theme(void) { return &themes[settings.theme]; }
