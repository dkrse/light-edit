#pragma once

#include <gtk/gtk.h>
#include <stdint.h>

#include "Scintilla.h"
#include "ScintillaWidget.h"

typedef struct Win Win;
typedef struct Search Search;

/* Scintilla indicator used to highlight search matches. */
#define IND_FIND 8

/* One open document = one tab. Owned by its Scintilla widget (freed when the
   widget is finalized), so closing a tab can never leak or double free it. */
typedef struct Doc {
    Win       *win;
    GtkWidget *page;        /* notebook page (the Scintilla widget) */
    GtkWidget *sci;
    GtkWidget *marks;       /* Scintilla's vertical scrollbar; search marks drawn on it */
    GtkWidget *tab;         /* tab label box */
    GtkWidget *label;
    char      *path;        /* canonical absolute path, NULL for untitled */
    const char *lang;       /* language name for the status bar */
    gint64     mtime_ns;    /* disk state after the last load/save */
    goffset    size;
    gboolean   on_disk;
    int        ln_digits;   /* line number margin is sized for this many */
    GArray    *matches;     /* sptr_t start positions of search matches */
    gboolean   matches_capped;
    /* Restored from the session but not read yet: loaded on first view. */
    gboolean   pending;
    sptr_t     restore_pos, restore_top;
    double     scroll_acc;  /* smooth Ctrl+scroll accumulator */
} Doc;

struct Win {
    GtkApplication *app;
    GtkWidget      *window;
    GtkNotebook    *nb;
    GtkWidget      *status;
    Search         *search;
    gboolean        checking_disk;  /* re-entrancy guard for focus checks */
    guint           disk_idle;      /* pending check source, 0 if none */
    guint           settings_save_id;  /* debounced save after zooming */
    gboolean        restoring;      /* building tabs from the session */
};

/* Scintilla takes colours as 0xBBGGRR; write them as 0xRRGGBB here. */
#define RGB(c) ((((c) & 0xff) << 16) | ((c) & 0xff00) | (((c) >> 16) & 0xff))

static inline sptr_t sci_send(Doc *d, unsigned int msg, uptr_t w, sptr_t l) {
    return scintilla_send_message((ScintillaObject *)d->sci, msg, w, l);
}
