TARGET_EXEC:=zelda3
ROM:=tables/zelda3.sfc
SRCS:=$(wildcard src/*.c src/rando/*.c snes/*.c) third_party/gl_core/gl_core_3_1.c third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c third_party/sha256/sha256.c
OBJS:=$(SRCS:%.c=%.o)
PYTHON:=/usr/bin/env python3
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
            src/rando/rando_window/imgui_host.cpp \
            src/rando/rando_window/tracker_windows.cpp
CPP_OBJS:=$(IMGUI_SRCS:%.cpp=%.o)
# The bridge .c lives under src/rando/rando_window/ — the src/rando/*.c glob is
# non-recursive and does NOT pick it up, so add it explicitly here (PC only).
SRCS:=$(SRCS) src/rando/rando_window/rando_window_bridge.c
OBJS:=$(SRCS:%.c=%.o)

# Rando codegen artifacts (tasks.md §3.5 / §3.6 / §6.3). The Python script reads
# the YAML registries under assets/rando/ and emits these four files. The
# wildcard above picks up src/rando/logic_data.c automatically; the headers are
# referenced by code via #include directives. assets/chest_data.py supplies
# the generated chest table + ALTTPR chest-name snapshot consumed
# by chest_lookup.h emission (§6.3).
RANDO_GEN_SRCS:=$(wildcard assets/rando/*.yaml) assets/rando_logic_gen.py assets/chest_data.py
RANDO_GEN_OUTPUTS:=src/rando/logic_data.c src/rando/location_ids.h src/rando/item_ids.h src/rando/chest_lookup.h src/rando/icon_atlas.h src/rando/direct_grant_icons.h

ifeq (${OS},Windows_NT)
    WINDRES:=windres
    RES:=zelda3.res
    SDLFLAGS:=-Wl,-Bstatic $(shell sdl2-config --static-libs)
else
    SDLFLAGS:=$(shell sdl2-config --libs) -lm
endif

.PHONY: all clean clean_obj clean_gen rando-codegen

all: $(TARGET_EXEC) zelda3_assets.dat
# Link through $(CXX) to pull in libstdc++ for the vendored ImGui C++ objects.
$(TARGET_EXEC): $(OBJS) $(CPP_OBJS) $(RES)
	$(CXX) $^ -o $@ $(LDFLAGS) $(SDLFLAGS)
%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@
%.o : %.cpp
	$(CXX) -c $(CXXFLAGS) $< -o $@

# Rando codegen rule. Touch any input → regenerate all four outputs.
# Building src/rando/logic_data.c triggers the rule (and emits the headers as a
# side-effect). Compilation depends on the headers via #include.
$(RANDO_GEN_OUTPUTS): $(RANDO_GEN_SRCS)
	@echo "Regenerating rando codegen: src/rando/{logic_data.c, location_ids.h, item_ids.h, chest_lookup.h, icon_atlas.h, direct_grant_icons.h}"
	$(PYTHON) assets/rando_logic_gen.py

rando-codegen: $(RANDO_GEN_OUTPUTS)

$(RES): src/platform/win32/zelda3.rc
	@echo "Generating Windows resources"
	@$(WINDRES) $< -O coff -o $@

zelda3_assets.dat:
	@echo "Extracting game resources"
	$(PYTHON) assets/restool.py --extract-from-rom

clean: clean_obj clean_gen
clean_obj:
	@$(RM) $(OBJS) $(CPP_OBJS) $(TARGET_EXEC)
clean_gen:
	@$(RM) $(RES) zelda3_assets.dat tables/zelda3_assets.dat tables/*.txt tables/*.png tables/sprites/*.png tables/*.yaml
	@rm -rf tables/__pycache__ tables/dungeon tables/img tables/overworld tables/sound
