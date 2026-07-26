TARGET_EXEC:=zelda3
ROM:=tables/zelda3.sfc
SRCS:=$(wildcard src/*.c src/rando/*.c snes/*.c) third_party/gl_core/gl_core_3_1.c third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c third_party/sha256/sha256.c
OBJS:=$(SRCS:%.c=%.o)
# Python runner for the asset-extraction and rando codegen tooling (which needs
# Pillow + PyYAML, see requirements.txt). On macOS/Linux, prefer `uv` when it is
# installed: `uv run` executes the scripts in an isolated, auto-provisioned
# environment seeded from requirements.txt, so contributors never have to manage
# a venv or `pip install` by hand. Fall back to the system python3 when uv is
# absent (the scripts still work if Pillow/PyYAML are already importable).
# Override explicitly with `make PYTHON=...`.
#
# Windows is intentionally NOT covered here: its builds invoke `python` directly
# via extract_assets.bat and the .vcxproj pre-build Exec, neither of which uses
# this variable — so Windows needs no uv.
UV:=$(shell command -v uv 2>/dev/null)
ifeq ($(UV),)
    PYTHON:=/usr/bin/env python3
else
    PYTHON:=$(UV) run --no-project --with-requirements requirements.txt python
endif
CFLAGS:=$(if $(CFLAGS),$(CFLAGS),-O2 -Werror) -I .
CFLAGS:=${CFLAGS} $(shell sdl2-config --cflags) -DSYSTEM_VOLUME_MIXER_AVAILABLE=0

# --- Native settings window (Z3R_NATIVE_SETTINGS_WINDOW): Dear ImGui (C++) ---
# PC builds (this Makefile + MSBuild) define the guard; the Switch Makefile does
# NOT, and its non-recursive src/rando/*.c glob excludes src/rando/rando_window/.
CFLAGS:=${CFLAGS} -DZ3R_NATIVE_SETTINGS_WINDOW=1
CXX:=$(if $(CXX),$(CXX),$(if $(filter clang%,$(CC)),clang++,g++))
IMGUI_DIR:=third_party/imgui
# ImGui is vendored third-party C++: build it C++17, no exceptions/rtti, and
# WITHOUT -Werror (it emits warnings under -O2). Reuse CFLAGS' includes/optims.
CXXFLAGS:=$(filter-out -Werror,$(CFLAGS)) -std=c++17 -fno-exceptions -fno-rtti -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends
IMGUI_SRCS:=$(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp \
            $(IMGUI_DIR)/imgui_widgets.cpp $(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp \
            $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp \
            src/rando/rando_window/rando_window.cpp \
            src/rando/rando_window/game_config_panels.cpp \
            src/rando/rando_window/game_cheats.cpp \
            src/rando/rando_window/dbg_watch.cpp \
            src/rando/rando_window/dbg_snapshots.cpp \
            src/rando/rando_window/dbg_timecontrol.cpp \
            src/rando/rando_window/dbg_warp.cpp \
            src/rando/rando_window/dbg_flags.cpp \
            src/rando/rando_window/rando_reach_panel.cpp \
            src/rando/rando_window/rando_hints_panel.cpp \
            src/rando/rando_window/panels_selftest.cpp \
            src/rando/rando_window/imgui_host.cpp \
            src/rando/rando_window/tracker_windows.cpp \
            src/rando/rando_window/file_dialog.cpp
CPP_OBJS:=$(IMGUI_SRCS:%.cpp=%.o)
# The bridge .c lives under src/rando/rando_window/ — the src/rando/*.c glob is
# non-recursive and does NOT pick it up, so add it explicitly here (PC only).
# src/rando/logic_data.c is a codegen OUTPUT (see RANDO_GEN_OUTPUTS below): on a
# fresh checkout it does not exist when the wildcard above is expanded, so the
# glob misses it and it would never link. List it explicitly; $(sort) dedupes
# the object in case a prior codegen run left the .c for the wildcard to catch.
SRCS:=$(SRCS) src/rando/rando_window/rando_window_bridge.c src/rando/logic_data.c
OBJS:=$(sort $(SRCS:%.c=%.o))
DEPS:=$(OBJS:.o=.d) $(CPP_OBJS:.o=.d)

# Rando codegen artifacts (tasks.md §3.5 / §3.6 / §6.3). The Python script reads
# the YAML registries under assets/rando/ and emits these four files. The
# wildcard above picks up src/rando/logic_data.c automatically; the headers are
# referenced by code via #include directives. assets/chest_data.py supplies
# the ALTTPR chest-name catalog consumed by chest_lookup.h emission (§6.3); the
# vanilla chest TABLE itself is read from the generated
# assets/rando/chest_table.gen.bin artifact produced by asset extraction.
# NOTE: recurse into assets/rando/ — the logic graph is assembled from
# logic.yaml + macros.yaml + every logic_parts/**/*.yaml (incl. logic_parts/inverted/**).
# A non-recursive `$(wildcard assets/rando/*.yaml)` misses the logic_parts/ subtree,
# so editing a logic_parts file would NOT retrigger codegen on an incremental build
# (stale logic_data.c → silent logic regression). `find` recurses portably.
# The find covers only *.yaml — the door-shuffle Vm-pred manifest
# assets/rando/door_predicates.gen.json (a COMMITTED artifact emitted by
# gen_door_tables.py from a reference checkout, not by this build) is also read
# by rando_logic_gen.py, so list it explicitly or editing it ships a stale
# logic_data.c on an incremental build. (door_portals.yaml is caught by the find.)
# Local pot registries (assets/rando/pots.gen.yaml and pot_key_depth.gen.yaml)
# are gitignored ROM-derived artifacts. The recursive find includes them when a
# local pot-codegen run has produced them; public CI builds without them.
RANDO_GEN_SRCS:=$(shell find assets/rando -name '*.yaml') assets/rando/hint_metadata.lock.json assets/rando/hint_registry_contract.json assets/rando/door_predicates.gen.json assets/rando_logic_gen.py assets/chest_data.py src/rando/rando_hints.c
RANDO_GEN_OUTPUTS:=src/rando/logic_data.c src/rando/location_ids.h src/rando/item_ids.h src/rando/hint_metadata.h src/rando/chest_lookup.h src/rando/pot_lookup.h src/rando/terrain_lookup.h src/rando/bonk_lookup.h src/rando/enemy_drop_lookup.h src/rando/enemy_check_lookup.h src/rando/pot_nonpot_drop_counts.h src/rando/icon_atlas.h src/rando/direct_grant_icons.h

ifeq (${OS},Windows_NT)
    WINDRES:=windres
    RES:=zelda3.res
    SDLFLAGS:=-Wl,-Bstatic $(shell sdl2-config --static-libs)
else
    SDLFLAGS:=$(shell sdl2-config --libs) -lm
endif

.PHONY: all clean clean_obj clean_gen rando-codegen rando-codegen-force rando-local-prepare rando-local-checks check-assets-signature force-assets-check

all: $(TARGET_EXEC) zelda3_assets.dat
# Link through $(CXX) to pull in libstdc++ for the vendored ImGui C++ objects.
$(TARGET_EXEC): | check-assets-signature
$(TARGET_EXEC): $(OBJS) $(CPP_OBJS) $(RES)
	$(CXX) $^ -o $@ $(LDFLAGS) $(SDLFLAGS)
%.o : %.c
	$(CC) -c $(CFLAGS) -MMD -MP -MF $(@:.o=.d) $< -o $@
%.o : %.cpp
	$(CXX) -c $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) $< -o $@

# Rando codegen rule. ONE invocation regenerates all outputs.
#
# All generated outputs come from a single run of the script. Apple's /usr/bin/make is
# GNU Make 3.81, which has no grouped-target `&:` syntax — so `$(OUTPUTS): deps`
# would run the recipe once PER output, and under -j those invocations race,
# writing the same files concurrently. Express the single-invocation contract
# portably (works on 3.81 and on Linux's 4.x): logic_data.c carries the recipe;
# the headers depend on it with an empty recipe (the `;`), since that one python
# run emits them as a side effect. Building any header forces logic_data.c
# first, so the script runs exactly once.
#
# This intentionally runs every build: the local pot YAMLs are gitignored and may
# be deleted, which cannot be modeled by a normal prerequisite list because the
# missing file disappears from `find`. rando_logic_gen.py skips replacing outputs
# whose contents are unchanged, so the common no-op path does not churn mtimes.
RANDO_GEN_HEADERS:=$(filter-out src/rando/logic_data.c,$(RANDO_GEN_OUTPUTS))
$(RANDO_GEN_HEADERS): src/rando/logic_data.c ;
src/rando/logic_data.c: rando-codegen-force $(RANDO_GEN_SRCS)
	@echo "Regenerating rando codegen: src/rando/{logic_data.c, location_ids.h, item_ids.h, hint_metadata.h, chest_lookup.h, pot_lookup.h, terrain_lookup.h, bonk_lookup.h, enemy_drop_lookup.h, enemy_check_lookup.h, pot_nonpot_drop_counts.h, icon_atlas.h, direct_grant_icons.h}"
	$(PYTHON) assets/rando_logic_gen.py

rando-codegen-force:

rando-codegen: $(RANDO_GEN_OUTPUTS)

check-assets-signature:
	@if [ -f zelda3_assets.dat ]; then \
	  $(PYTHON) assets/scripts/check_assets_signature.py --quiet; \
	fi

RANDO_LOCAL_CHECK_BINARY?=./$(TARGET_EXEC)
rando-local-prepare: all
	$(PYTHON) assets/scripts/run_rando_local_checks.py --binary=$(RANDO_LOCAL_CHECK_BINARY) --prepare-only

rando-local-checks: rando-local-prepare
	$(MAKE) clean_obj
	$(MAKE) $(TARGET_EXEC)
	$(PYTHON) assets/scripts/run_rando_local_checks.py --binary=$(RANDO_LOCAL_CHECK_BINARY) --skip-prepare

# Make every object wait on the codegen outputs and the asset-hash header
# before it compiles. Order-only (the `|`): they gate presence, not timestamps.
# Normal include relationships, including generated headers once present, are
# tracked by the compiler-emitted depfiles included at the end of this file.
# This is what lets a
# fresh checkout — including CI, which has NO ROM and never extracts assets —
# build with a bare `make zelda3`. (The Windows vcxproj runs the same codegen
# as a pre-build Exec; this is the Make equivalent.)
$(OBJS) $(CPP_OBJS): | $(RANDO_GEN_OUTPUTS) src/rando/vanilla_assets_hash.h

# vanilla_assets_hash.h holds the SHA-256 the rando code compares assets
# against. The asset pipeline (assets/restool.py) bakes the REAL hash here when
# it (re)builds zelda3_assets.dat. This rule covers the gap: it fires ONLY when
# the header is absent (no prerequisites), and then — if a blob already exists —
# bakes the real hash from it, otherwise (ROM-less CI, fresh checkout) drops an
# inert all-zeros placeholder (kVanillaAssetsHashKnown=0) so the code compiles.
# Either way it never clobbers an existing header.
src/rando/vanilla_assets_hash.h:
	@if [ -f zelda3_assets.dat ]; then \
	  echo "Baking src/rando/vanilla_assets_hash.h from existing zelda3_assets.dat"; \
	  $(PYTHON) assets/scripts/dump_vanilla_assets_hash.py; \
	else \
	  echo "Generating placeholder src/rando/vanilla_assets_hash.h (no assets extracted yet)"; \
	  $(PYTHON) assets/scripts/dump_vanilla_assets_hash.py --placeholder; \
	fi

$(RES): src/platform/win32/zelda3.rc
	@echo "Generating Windows resources"
	@$(WINDRES) $< -O coff -o $@

zelda3_assets.dat: force-assets-check
	@if [ -f zelda3_assets.dat ] && $(PYTHON) assets/scripts/check_assets_signature.py --quiet; then \
	  :; \
	else \
	  echo "Extracting game resources"; \
	  $(PYTHON) assets/restool.py --extract-from-rom; \
	fi

force-assets-check:

clean: clean_obj clean_gen
clean_obj:
	@$(RM) $(OBJS) $(CPP_OBJS) $(TARGET_EXEC)
	@$(RM) $(DEPS)
clean_gen:
	@$(RM) $(RES) zelda3_assets.dat tables/zelda3_assets.dat tables/*.txt tables/*.png tables/sprites/*.png tables/*.yaml
	@rm -rf tables/__pycache__ tables/dungeon tables/img tables/overworld tables/sound

# Missing depfiles are expected on a fresh checkout and after clean_obj.
# -MP emits harmless phony header targets so removing/renaming a header does not
# make a stale depfile prevent Make from reaching the real compile diagnostic.
-include $(DEPS)
