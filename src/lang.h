#pragma once

#include "app.h"

/* Sets the base style (font, colours, margins) shared by every document. */
void lang_apply_base(Doc *d);

/* Picks a Lexilla lexer from the file name and applies its keywords and
   colours. Plain text when nothing matches. */
void lang_apply_for_path(Doc *d, const char *path);
