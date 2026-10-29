# Changelog

All notable changes to Light Edit are listed here.
The format follows [Keep a Changelog](https://keepachangelog.com/) and the
project uses [Semantic Versioning](https://semver.org/).

## 1.0.0 (2026-10-29)

First release, written from scratch in C17 with GTK 3, Scintilla 5.6.7 and
Lexilla 5.5.4.

### Added

**Files and tabs**

* Tabs showing file names, reorder by dragging, close with the button or a middle click
* A file that is already open gets focused instead of opened again
* Session: open tabs, the active tab, caret and scroll position are restored at start;
  only the visible tab is read, the rest load on first view
* New file, open (several at once), save, save as
* Print with syntax colours on white paper, word wrap and a header with path and page numbers
* Reload of files changed on disk when the window gets focus; asks first if there are unsaved edits
* Prompt to save unsaved tabs when closing a tab or the window
* Single instance: files passed on the command line open in the running window
* Drag and drop of files onto the editor

**UTF‑8 and saving**

* Chunked loading with UTF‑8 validation; non UTF‑8 files are refused with the byte offset
* BOM removal and preservation of LF, CRLF and CR line endings
* Atomic save through a synced temporary file and rename, keeping permissions,
  owner and symlinks
* Save directly from the editor buffer without copying the text

**Editing**

* Undo, redo, cut, copy, paste, select all, duplicate line, go to line
* Multiple carets and rectangular selection
* Find and replace bar with match case, whole word and regular expressions
* Highlight of all matches in the text and marks on the vertical scrollbar
* Replace all as one undo step
* Syntax highlighting for C, C++, JavaScript/TypeScript, Java, Go, Rust, Python,
  Shell, JSON, HTML, XML, CSS, Makefile, YAML, Markdown, INI/TOML, Diff,
  LaTeX/TeX and Assembly (NASM/Intel and GNU as; x86, ARM and RISC‑V)

**Appearance and settings**

* 10 themes applied to the whole window: header bar, tabs, scrollbars, search bar,
  status bar, menus and dialogs, each with its own accent colour
* Editor font and size; Ctrl + mouse wheel and Ctrl+Plus/Minus/0 for the size
* Interface font and size, or the desktop font
* Text intensity from 30 % (lighter) to 170 % (heavier)
* Tab width, spaces instead of tabs, visible whitespace, line numbers,
  current line highlight, word wrap
* Settings saved to `~/.config/light-edit/settings.ini`

**Project**

* Application icon and desktop entry, `make install` and `make uninstall`
* Release build and debug build with AddressSanitizer and UBSan
* MIT License
