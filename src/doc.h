#pragma once

#include "app.h"

/* Creates the editor widget and tab label; the caller adds them to the
   notebook. The Doc is freed together with its Scintilla widget. */
Doc *doc_new(Win *w);

static inline Doc *doc_from_widget(GtkWidget *w) {
    return w ? g_object_get_data(G_OBJECT(w), "doc") : NULL;
}

gboolean doc_is_dirty(Doc *d);
/* Untitled, unmodified and empty: may be replaced by an opened file. */
gboolean doc_is_pristine(Doc *d);
char    *doc_name(Doc *d);          /* g_free() the result */

void     doc_update_label(Doc *d);
/* Re-applies font, theme, tabs, wrap and margins from `settings`. */
void     doc_apply_settings(Doc *d);
/* Cheaper than doc_apply_settings: only the font size changed. */
void     doc_apply_font_size(Doc *d);

/* path must already be canonical. */
gboolean doc_load(Doc *d, const char *path, char **err);
/* Re-reads the file, keeping the caret and scroll position. */
gboolean doc_reload(Doc *d, char **err);
