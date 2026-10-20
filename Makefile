# RE4OGCabbyCodes - a 32-bit proxy-DLL mod for Resident Evil 4 (2005), the Ultimate HD Edition on Steam
# (app 254700, Bin32/bio4.exe). Cross-compiled on Linux with mingw-w64 (no MSVC / no Windows needed).
#
# The mod ships as steam_api.dll, standing in front of the Steam API DLL the
# game already ships (renamed steam_api_orig.dll by `make install`). Every
# function export of the original is forwarded by a jump thunk, the one data
# export by a PE forwarder (steam_api.def + src/proxy_exports.inc are generated
# by tools/gen_proxy.py). bio4.exe imports steam_api.dll statically, and it is
# not a Wine builtin, so Proton loads ours with no launch options - and
# dinput8.dll stays free for re4_tweaks.

CXX      := i686-w64-mingw32-g++

# The project name (used for the distributable), the DLL name (fixed by the
# game, which imports steam_api.dll) and the game executable are deliberately
# separate things.
PROJECT  := RE4OGCabbyCodes
NAME     := steam_api
ORIG     := steam_api_orig
EXE      := bio4.exe

# VERSION holds the single source of truth. Change it with `make version X.Y.Z`.
# With no VERSION file the build reports 0.0.0.
VERSION  := $(shell cat VERSION 2>/dev/null || echo 0.0.0)
BUILD    := build
TARGET   := $(BUILD)/$(NAME).dll
DEF      := steam_api.def

# Per-machine config (game install path), kept out of git. Copy
# config.mk.example to config.mk and set GAME_DIR there. The path contains
# spaces, so it is only ever used quoted inside shell recipes below.
-include config.mk

ifneq (,$(filter version rev,$(MAKECMDGOALS)))
REV := $(strip $(filter-out version rev,$(MAKECMDGOALS)))
$(eval $(REV):;@:)
endif

SRCS := $(wildcard src/*.cpp)

# Dear ImGui (vendored, MIT) supplies the overlay panel: the core plus the
# Win32 and DX9 backends - bio4.exe draws through Direct3D 9.
IMGUI_DIR  := contrib/imgui
IMGUI_SRCS := $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp \
              $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp \
              $(IMGUI_DIR)/backends/imgui_impl_dx9.cpp \
              $(IMGUI_DIR)/backends/imgui_impl_win32.cpp

OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRCS)) \
        $(patsubst $(IMGUI_DIR)/%.cpp,$(BUILD)/imgui/%.o,$(IMGUI_SRCS))

# -MMD -MP: emit .d header-dependency files so editing a header recompiles every
# .cpp that includes it.
CXXFLAGS := -m32 -std=c++20 -O2 -Wall -Wextra -Wno-cast-function-type \
            -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD \
            -I$(IMGUI_DIR) \
            -ffunction-sections -fdata-sections \
            -MMD -MP

# -static*: fold the C/C++ runtime in so the DLL has no external mingw runtime
# deps. Exports come from the .def, so no --exclude-all-symbols here.
LDFLAGS  := -m32 -shared \
            -static -static-libgcc -static-libstdc++ \
            -Wl,--gc-sections

# Only DLLs every Windows (and Wine) has are imported. d3d9 and dinput8 are the
# game's: the mod only ever calls the objects the game made with them.
LDLIBS   := -luser32 -lshell32 -lole32 -lgdi32 -ldwmapi -lwinmm

.PHONY: all clean install uninstall dist package rev version proxy sigs
all: $(TARGET)

$(TARGET): $(OBJS) $(DEF) | $(BUILD)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(DEF) $(LDLIBS)
	@echo ">> Built $@"

MOD_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRCS))

# Rebuild the mod's own objects when the version changes (the vendored ImGui
# objects do not use it, so they are left alone). No VERSION file: nothing to track.
$(MOD_OBJS): $(wildcard VERSION)

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -DRE4CC_VERSION='"$(VERSION)"' -c $< -o $@

$(BUILD)/imgui/%.o: $(IMGUI_DIR)/%.cpp | $(BUILD)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)

$(BUILD):
	mkdir -p $(BUILD) $(BUILD)/imgui/backends

# Regenerate the export list from the stock DLL (the preserved original if the
# mod is installed, else the one the game shipped) and check it against the
# exe's import table.
proxy:
	@set -e; \
	if [ -z "$(GAME_DIR)" ]; then echo "ERROR: GAME_DIR is not set (see config.mk.example)."; exit 1; fi; \
	src="$(GAME_DIR)/$(ORIG).dll"; [ -f "$$src" ] || src="$(GAME_DIR)/$(NAME).dll"; \
	python3 tools/gen_proxy.py --exe "$(GAME_DIR)/$(EXE)" --dll $(NAME).dll \
	  --from-dll "$$src" --def $(DEF) --inc src/proxy_exports.inc --prefix steam

# Every signature src/game.cpp uses, checked against the exe on disk (bio4.exe is
# not packed: what is on disk is what runs).
sigs:
	@set -e; \
	if [ -z "$(GAME_DIR)" ]; then echo "ERROR: GAME_DIR is not set (see config.mk.example)."; exit 1; fi; \
	python3 tools/check_sigs.py --exe "$(GAME_DIR)/$(EXE)"

# `make version` prints the version; `make version X.Y.Z` sets it (`rev` is an
# older spelling of the same thing). Make has no argument syntax, so the new
# version arrives as a second goal; the block near the top defines a do-nothing
# target for it so make does not fail trying to build "1.2.3".
version rev:
	@v='$(REV)'; \
	if [ -z "$$v" ] && [ "$@" = version ]; then echo "$(VERSION)"; exit 0; fi; \
	if ! echo "$$v" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$$'; then \
	  echo "usage: make $@ X.Y.Z        (for example: make $@ 1.2.0)"; \
	  echo "current version: $(VERSION)"; \
	  exit 1; \
	fi; \
	printf '%s\n' "$$v" > VERSION; \
	echo ">> version $(VERSION) -> $$v"; \
	echo ">> run 'make package' to build $(PROJECT)_v$$v.zip"

clean:
	rm -rf $(BUILD) $(DIST)

# --- Deploy -----------------------------------------------------------------
# The mod ships AS steam_api.dll, so the stock one must first be moved aside
# to steam_api_orig.dll (which our proxy jumps into). That rename happens
# exactly once and is guarded: if the live DLL is already our proxy but the
# original is missing, we refuse rather than enshrine the mod as the "stock"
# copy, which would permanently destroy the real DLL.
#
# Atomic install: write to a temp name in the SAME directory then rename(2) over
# the target, so a running game keeps its old intact mapping.
install: $(TARGET)
	@set -e; \
	if [ -z "$(GAME_DIR)" ]; then \
	  echo "ERROR: GAME_DIR is not set."; \
	  echo "  Copy config.mk.example to config.mk and set GAME_DIR,"; \
	  echo "  or run: make install GAME_DIR=\"/path/to/Resident Evil 4/Bin32\""; \
	  exit 1; \
	fi; \
	if [ ! -f "$(GAME_DIR)/$(EXE)" ]; then \
	  echo "ERROR: no '$(EXE)' in '$(GAME_DIR)' - is that really the game's Bin32 folder?"; exit 1; \
	fi; \
	if pgrep -x $(EXE) >/dev/null 2>&1; then \
	  echo "ERROR: $(EXE) is running - close the game first."; exit 1; \
	fi; \
	orig="$(GAME_DIR)/$(ORIG).dll"; \
	live="$(GAME_DIR)/$(NAME).dll"; \
	if [ -f "$$orig" ]; then \
	  echo ">> stock original already preserved at $$orig"; \
	else \
	  stock=$$(ls "$(GAME_DIR)" | grep -ix '$(NAME)\.dll' | head -1); \
	  if [ -z "$$stock" ]; then echo "ERROR: no $(NAME).dll in '$(GAME_DIR)'."; exit 1; fi; \
	  if grep -aq "$(PROJECT)" "$(GAME_DIR)/$$stock" 2>/dev/null; then \
	    echo "ERROR: $(GAME_DIR)/$$stock is already our proxy, but $$orig is missing."; \
	    echo "       Refusing to save the mod as the 'stock' original."; \
	    echo "       Restore the real $(NAME).dll (Steam: Verify integrity of game files),"; \
	    echo "       then re-run make install."; \
	    exit 1; \
	  fi; \
	  mv -f "$(GAME_DIR)/$$stock" "$$orig"; \
	  echo ">> preserved stock $$stock -> $(ORIG).dll"; \
	fi; \
	t="$$live.re4cc-new.$$$$"; cp -f $(TARGET) "$$t"; mv -f "$$t" "$$live"; \
	echo ">> installed (atomic) $$live"; \
	for stray in $$(ls "$(GAME_DIR)" | grep -ix '$(NAME)\.dll' | grep -vx '$(NAME).dll'); do \
	  if grep -aq "$(PROJECT)" "$(GAME_DIR)/$$stray" 2>/dev/null; then \
	    rm -f "$(GAME_DIR)/$$stray"; \
	    echo ">> removed stale copy $$stray - it shadowed this install"; \
	  else \
	    echo ">> WARNING: $$stray exists and is not ours - it may shadow the mod"; \
	  fi; \
	done; \
	echo ">> NOTE: Steam 'Verify integrity of game files' restores the stock DLL;"; \
	echo ">>       just re-run 'make install' if that happens."

# Put the stock DLL back and remove the mod entirely.
uninstall:
	@set -e; \
	if [ -z "$(GAME_DIR)" ]; then echo "ERROR: GAME_DIR is not set."; exit 1; fi; \
	orig="$(GAME_DIR)/$(ORIG).dll"; \
	if [ ! -f "$$orig" ]; then echo "Nothing to do: $$orig not present."; exit 0; fi; \
	for stray in $$(ls "$(GAME_DIR)" | grep -ix '$(NAME)\.dll'); do \
	  if grep -aq "$(PROJECT)" "$(GAME_DIR)/$$stray" 2>/dev/null; then \
	    rm -f "$(GAME_DIR)/$$stray"; \
	  fi; \
	done; \
	mv -f "$$orig" "$(GAME_DIR)/$(NAME).dll"; \
	rm -f "$(GAME_DIR)/$(PROJECT).log"; \
	echo ">> restored stock $(NAME).dll and removed the mod"

# --- Distributable ----------------------------------------------------------
# `make dist` assembles dist/RE4OGCabbyCodes/ - the mod DLL plus the docs and
# an INSTALL.txt. It deliberately does NOT contain steam_api_orig.dll: that is
# the game's own file, which the user renames themselves (or `make install`
# does).
DIST    ?= dist
PAYLOAD  = $(DIST)/$(PROJECT)

dist: $(TARGET)
	@rm -rf "$(PAYLOAD)" && mkdir -p "$(PAYLOAD)"
	@cp $(TARGET) "$(PAYLOAD)/$(NAME).dll"
	@cp README.md LICENSE "$(PAYLOAD)/"
	@printf '%s\n' \
	  "$(PROJECT) $(VERSION) - pause-menu cheat panel for Resident Evil 4 (2005)" \
	  "" \
	  "  For the Steam version of the game (Resident Evil 4 (2005), the Ultimate" \
	  "  HD Edition, build 10112090)." \
	  "" \
	  "INSTALL" \
	  "  1. Open your Resident Evil 4 folder, then its 'Bin32' folder" \
	  "     (the one containing '$(EXE)')." \
	  "  2. Rename the existing  $(NAME).dll  to  $(ORIG).dll" \
	  "  3. Copy  $(NAME).dll  from this folder in beside it." \
	  "" \
	  "  That is all, on Windows and on Linux / Steam Deck (Proton): no launch" \
	  "  options. The mod leaves dinput8.dll alone, so it can sit beside re4_tweaks." \
	  "" \
	  "UNINSTALL" \
	  "  Delete $(NAME).dll and rename $(ORIG).dll back to $(NAME).dll." \
	  "" \
	  "USE" \
	  "  Pause the game (its Options screen) while playing and a panel appears" \
	  "  beside it: God mode, Ashley takes no damage, One hit kills, Infinite ammo," \
	  "  Freeze play time, Freeze countdown timer, editors for the pesetas, the" \
	  "  play time, the countdown and the results counters, the outfit, the" \
	  "  difficulty, an attache case editor and a limitless item box of the mod's" \
	  "  own, kept outside the game. Beside the game's load and save lists it" \
	  "  manages your save files (delete a save, copy one into another slot). F7" \
	  "  hides/shows the panel. Settings and the log live in $(PROJECT).ini / .log" \
	  "  beside the DLL." \
	  > "$(PAYLOAD)/INSTALL.txt"
	@echo ">> assembled $(PAYLOAD)"

# <project>_v<version>.zip, e.g. RE4OGCabbyCodes_v1.0.0.zip
PKG ?= $(DIST)/$(PROJECT)_v$(VERSION).zip
package: dist
	@command -v zip >/dev/null || { echo "ERROR: zip required"; exit 1; }
	@rm -f "$(PKG)"
	@cd "$(PAYLOAD)" && zip -r -q "$(abspath $(PKG))" .
	@echo ">> packaged -> $(PKG) ($$(du -h "$(PKG)" | cut -f1))"
