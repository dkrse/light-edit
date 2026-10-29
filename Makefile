# Light Edit: application code is pure C17. Scintilla and Lexilla are not
# kept in the repository: the first build downloads the pinned releases,
# checks their SHA-256, builds them with their own makefiles and links them
# statically.

BUILD ?= release

CC   ?= gcc
CXX  ?= g++
PKGS  = gtk+-3.0 gmodule-2.0

SCI_VER = 567
LEX_VER = 554
SCI_SHA = 19e4476706750056f86ca70974810e4a5b08e73477e13f7d9fae78243606f6a7
LEX_SHA = fcd9ef74236a12359d1039827c93dbf827aa476c46f9fd6eef60bb96db1af2f6
DL_URL  = https://www.scintilla.org

SCI   = vendor/scintilla
LEX   = vendor/lexilla
SCI_OK = $(SCI)/.extracted
LEX_OK = $(LEX)/.extracted
SCI_A = $(SCI)/bin/scintilla.a
LEX_A = $(LEX)/bin/liblexilla.a

CFLAGS_COMMON = -std=c17 -Wall -Wextra -Wshadow -Wstrict-prototypes \
                -D_GNU_SOURCE -DGTK \
                -I$(SCI)/include -I$(LEX)/include \
                $(shell pkg-config --cflags $(PKGS)) -MMD -MP

ifeq ($(BUILD),debug)
  CFLAGS  = $(CFLAGS_COMMON) -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
  LDEXTRA = -fsanitize=address,undefined
else
  CFLAGS  = $(CFLAGS_COMMON) -O2 -DNDEBUG -DG_DISABLE_CAST_CHECKS
  LDEXTRA = -s
endif

# Scintilla is C++ internally, so the C++ runtime has to be linked.
LDLIBS = $(shell pkg-config --libs $(PKGS)) -lstdc++ -lm

OUT = build/$(BUILD)
SRC = $(wildcard src/*.c)
OBJ = $(patsubst src/%.c,$(OUT)/%.o,$(SRC))
BIN = $(OUT)/light-edit

.PHONY: all debug run clean distclean vendor install uninstall

all: $(BIN)

debug:
	$(MAKE) BUILD=debug

$(BIN): $(OBJ) $(SCI_A) $(LEX_A)
	$(CC) $(OBJ) $(SCI_A) $(LEX_A) -o $@ $(LDLIBS) $(LDEXTRA)

$(OUT)/%.o: src/%.c | $(OUT) $(SCI_OK) $(LEX_OK)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT):
	mkdir -p $@

vendor: $(SCI_A) $(LEX_A)

# fetch <name> <version> <sha256>: download, verify, unpack into vendor/.
define fetch
	mkdir -p vendor
	curl -fL --retry 5 --retry-all-errors -o vendor/$(1)$(2).tgz $(DL_URL)/$(1)$(2).tgz
	echo "$(3)  vendor/$(1)$(2).tgz" | sha256sum -c -
	tar -xzf vendor/$(1)$(2).tgz -C vendor
	rm -f vendor/$(1)$(2).tgz
	touch vendor/$(1)/.extracted
endef

$(SCI_OK):
	$(call fetch,scintilla,$(SCI_VER),$(SCI_SHA))

$(LEX_OK):
	$(call fetch,lexilla,$(LEX_VER),$(LEX_SHA))

$(SCI_A): | $(SCI_OK)
	$(MAKE) -C $(SCI)/gtk GTK3=1 ../bin/scintilla.a

$(LEX_A): | $(LEX_OK)
	$(MAKE) -C $(LEX)/src ../bin/liblexilla.a

PREFIX ?= $(HOME)/.local
APP_ID  = io.github.krse.LightEdit

install: $(BIN)
	install -Dm755 $(BIN) $(PREFIX)/bin/light-edit
	install -Dm644 data/$(APP_ID).svg $(PREFIX)/share/icons/hicolor/scalable/apps/$(APP_ID).svg
	sed 's|^Exec=light-edit|Exec=$(PREFIX)/bin/light-edit|' data/$(APP_ID).desktop \
		> $(APP_ID).desktop.tmp
	install -Dm644 $(APP_ID).desktop.tmp $(PREFIX)/share/applications/$(APP_ID).desktop
	rm -f $(APP_ID).desktop.tmp
	-gtk-update-icon-cache -q -t $(PREFIX)/share/icons/hicolor
	-update-desktop-database -q $(PREFIX)/share/applications

uninstall:
	rm -f $(PREFIX)/bin/light-edit
	rm -f $(PREFIX)/share/icons/hicolor/scalable/apps/$(APP_ID).svg
	rm -f $(PREFIX)/share/applications/$(APP_ID).desktop

run: $(BIN)
	./$(BIN)

clean:
	rm -rf build

distclean: clean
	rm -rf vendor

-include $(OBJ:.o=.d)
