#pragma once

#include <glib.h>

/* Syntax roles; lexer styles map onto these, themes give them colours. */
typedef enum {
    R_FG, R_COMMENT, R_KEYWORD, R_TYPE, R_STRING, R_NUMBER,
    R_PREPROC, R_TAG, R_ADDED, R_DELETED, R_COUNT
} Role;

typedef struct {
    const char *name;
    gboolean    dark;       /* also switches GTK to its dark variant */
    unsigned    bg, caret_line, sel, margin, line_no, whitespace;
    unsigned    find;       /* match highlight in the text */
    unsigned    mark;       /* match marks on the scrollbar */
    unsigned    accent;     /* active tab line, focus rings, switches */
    unsigned    syn[R_COUNT];
} Theme;

extern const Theme themes[];
extern const int   theme_count;

/* Index of the theme with this name, 0 (the default) if unknown. */
int theme_find(const char *name);
