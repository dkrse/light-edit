#pragma once

#include "app.h"

Win  *win_create(GtkApplication *app);
Win  *win_from_window(GtkWindow *w);

Doc  *win_current(Win *w);
void  win_new_doc(Win *w);
/* Opens a file in a new tab, or switches to it if it is already open. */
void  win_open_path(Win *w, const char *path);
/* Asks about unsaved changes; returns FALSE if the user cancelled. */
gboolean win_close_doc(Win *w, Doc *d);

void  win_update_title(Win *w);
void  win_update_status(Win *w);
/* Enables Undo/Redo and the like for the current state. */
void  win_update_actions(Win *w);
/* Re-applies `settings` to every tab and to GTK (dark variant). */
void  win_apply_settings(Win *w);
/* Font size up (+1) or down (-1) for all tabs; saved shortly after. */
void  win_zoom(Win *w, int step);

/* Open tabs are remembered in ~/.config/light-edit/session.ini. */
void  win_save_session(Win *w);
void  win_restore_session(Win *w);
