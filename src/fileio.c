#include "fileio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHUNK (1024 * 1024)

gboolean fileio_stat(const char *path, gint64 *mtime_ns, goffset *size) {
    struct stat st;
    if (stat(path, &st) != 0) return FALSE;
    *mtime_ns = (gint64)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
    *size = st.st_size;
    return TRUE;
}

static void set_err(char **err, const char *what, const char *path, int e) {
    *err = g_strdup_printf("%s \"%s\": %s", what, path, g_strerror(e));
}

/* Picks the EOL mode from the first line ending so new lines match the file. */
static void detect_eol(Doc *d) {
    sptr_t len = sci_send(d, SCI_GETLENGTH, 0, 0);
    sptr_t lim = len < 65536 ? len : 65536;
    for (sptr_t i = 0; i < lim; i++) {
        char c = (char)sci_send(d, SCI_GETCHARAT, i, 0);
        if (c == '\r') {
            char n = i + 1 < len ? (char)sci_send(d, SCI_GETCHARAT, i + 1, 0) : 0;
            sci_send(d, SCI_SETEOLMODE, n == '\n' ? SC_EOL_CRLF : SC_EOL_CR, 0);
            return;
        }
        if (c == '\n') break;
    }
    sci_send(d, SCI_SETEOLMODE, SC_EOL_LF, 0);
}

gboolean fileio_load(Doc *d, const char *path, char **err) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { set_err(err, "Cannot open", path, errno); return FALSE; }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        set_err(err, "Cannot read", path, errno);
        close(fd);
        return FALSE;
    }
    if (!S_ISREG(st.st_mode)) {
        *err = g_strdup_printf("\"%s\" is not a regular file.", path);
        close(fd);
        return FALSE;
    }
    if (st.st_size > FILEIO_MAX_SIZE) {
        *err = g_strdup_printf("\"%s\" is too large (%.1f MB).", path,
                               st.st_size / (1024.0 * 1024.0));
        close(fd);
        return FALSE;
    }
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);

    /* No undo history for the initial content, and reserve the final size
       up front so the gap buffer is allocated once. */
    sci_send(d, SCI_SETUNDOCOLLECTION, 0, 0);
    sci_send(d, SCI_CLEARALL, 0, 0);
    sci_send(d, SCI_ALLOCATE, (uptr_t)st.st_size + 1, 0);

    char *buf = g_malloc(CHUNK + 4);
    size_t carry = 0;          /* bytes of a UTF-8 sequence split by a chunk */
    gboolean first = TRUE, ok = TRUE;
    goffset offset = 0;

    for (;;) {
        ssize_t r = read(fd, buf + carry, CHUNK);
        if (r < 0) {
            if (errno == EINTR) continue;
            set_err(err, "Cannot read", path, errno);
            ok = FALSE;
            break;
        }
        size_t have = carry + (size_t)r;
        char *p = buf;
        if (first && have >= 3 && memcmp(p, "\xEF\xBB\xBF", 3) == 0) {
            p += 3;              /* drop the BOM; it is saved without one */
            have -= 3;
        }
        first = FALSE;

        const char *end;
        size_t valid = have;
        carry = 0;
        if (!g_utf8_validate_len(p, have, &end)) {
            valid = (size_t)(end - p);
            size_t rest = have - valid;
            /* An incomplete sequence at the end of a chunk is fine as long
               as more data follows; anything else is not UTF-8. */
            if (r > 0 && rest < 4 &&
                g_utf8_get_char_validated(end, (gssize)rest) == (gunichar)-2) {
                carry = rest;
            } else {
                *err = g_strdup_printf(
                    "\"%s\" is not valid UTF-8 (byte offset %" G_GOFFSET_FORMAT ").\n"
                    "Light Edit only opens UTF-8 text files.",
                    path, offset + (goffset)valid);
                ok = FALSE;
                break;
            }
        }
        if (valid) sci_send(d, SCI_APPENDTEXT, valid, (sptr_t)p);
        offset += (goffset)valid;
        if (carry) memmove(buf, p + valid, carry);
        if (r == 0) break;
    }

    g_free(buf);
    close(fd);

    if (!ok) sci_send(d, SCI_CLEARALL, 0, 0);
    else detect_eol(d);

    sci_send(d, SCI_SETUNDOCOLLECTION, 1, 0);
    sci_send(d, SCI_EMPTYUNDOBUFFER, 0, 0);
    sci_send(d, SCI_SETSAVEPOINT, 0, 0);
    sci_send(d, SCI_GOTOPOS, 0, 0);

    if (ok) {
        d->mtime_ns = (gint64)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
        d->size = st.st_size;
        d->on_disk = TRUE;
    }
    return ok;
}

static gboolean write_all(int fd, const char *p, size_t len) {
    while (len) {
        ssize_t w = write(fd, p, len);
        if (w < 0) {
            if (errno == EINTR) continue;
            return FALSE;
        }
        p += w;
        len -= (size_t)w;
    }
    return TRUE;
}

gboolean fileio_save(Doc *d, const char *path, char **err) {
    /* Save through symlinks: replace the file they point to, not the link. */
    char *target = realpath(path, NULL);
    if (!target) target = strdup(path);

    char *dir = g_path_get_dirname(target);
    char *base = g_path_get_basename(target);
    char *tmp = g_strdup_printf("%s/.%s.XXXXXX", dir, base);
    gboolean ok = FALSE;

    int fd = mkostemp(tmp, O_CLOEXEC);
    if (fd < 0) {
        set_err(err, "Cannot create a temporary file in", dir, errno);
        goto out;
    }

    struct stat st;
    if (stat(target, &st) == 0) {
        if (fchmod(fd, st.st_mode & 07777) != 0) { /* keep permissions */ }
        if (fchown(fd, st.st_uid, st.st_gid) != 0) { /* best effort */ }
    } else {
        mode_t mask = umask(0);
        umask(mask);
        if (fchmod(fd, 0666 & ~mask) != 0) { /* mkstemp's 0600 stays */ }
    }

    /* SCI_GETCHARACTERPOINTER closes the gap and hands out the buffer, so
       the save needs no copy of the document. */
    size_t len = (size_t)sci_send(d, SCI_GETLENGTH, 0, 0);
    const char *data = (const char *)sci_send(d, SCI_GETCHARACTERPOINTER, 0, 0);

    if (!write_all(fd, data, len) || fsync(fd) != 0) {
        set_err(err, "Cannot write", target, errno);
        close(fd);
        unlink(tmp);
        goto out;
    }
    if (close(fd) != 0) {
        set_err(err, "Cannot write", target, errno);
        unlink(tmp);
        goto out;
    }
    if (rename(tmp, target) != 0) {
        set_err(err, "Cannot replace", target, errno);
        unlink(tmp);
        goto out;
    }

    int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) { fsync(dfd); close(dfd); }

    sci_send(d, SCI_SETSAVEPOINT, 0, 0);
    d->on_disk = fileio_stat(target, &d->mtime_ns, &d->size);
    ok = TRUE;

out:
    g_free(tmp);
    g_free(base);
    g_free(dir);
    free(target);
    return ok;
}
