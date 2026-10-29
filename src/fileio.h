#pragma once

#include "app.h"

/* Largest file we agree to open. Scintilla copes with more, but beyond this
   a text editor is the wrong tool and memory use stops being reasonable. */
#define FILEIO_MAX_SIZE ((goffset)1024 * 1024 * 1024)

/* Streams the file into the document in fixed-size chunks, validating UTF-8
   on the way. On failure the document is left empty and *err is set. */
gboolean fileio_load(Doc *d, const char *path, char **err);

/* Writes the buffer straight from Scintilla's memory to a temporary file in
   the same directory, fsyncs it and renames it over the target. */
gboolean fileio_save(Doc *d, const char *path, char **err);

/* Fills mtime/size from disk; returns FALSE if the file does not exist. */
gboolean fileio_stat(const char *path, gint64 *mtime_ns, goffset *size);
