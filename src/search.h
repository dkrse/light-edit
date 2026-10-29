#pragma once

#include "app.h"

Search    *search_new(Win *w);
void       search_free(Search *s);
GtkWidget *search_widget(Search *s);

gboolean   search_visible(Search *s);
/* Shows the bar (with the replace row if asked) and focuses the find entry,
   prefilled with the selection when it is a single line. */
void       search_show(Search *s, gboolean replace);
void       search_hide(Search *s);
void       search_next(Search *s, gboolean forward);

/* The current tab changed: re-highlight in the new one. */
void       search_doc_switched(Search *s);
/* The text of the current document changed: refresh matches shortly. */
void       search_doc_changed(Search *s, Doc *d);
