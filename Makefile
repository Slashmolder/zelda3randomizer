TARGET_EXEC:=zelda3
ROM:=tables/zelda3.sfc
SRCS:=$(wildcard src/*.c src/rando/*.c snes/*.c) third_party/gl_core/gl_core_3_1.c third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c third_party/sha256/sha256.c
OBJS:=$(SRCS:%.c=%.o)
PYTHON:=/usr/bin/env python3
CFLAGS:=$(if $(CFLAGS),$(CFLAGS),-O2 -Werror) -I .
CFLAGS:=${CFLAGS} $(shell sdl2-config --cflags) -DSYSTEM_VOLUME_MIXER_AVAILABLE=0

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
$(TARGET_EXEC): $(OBJS) $(RES)
	$(CC) $^ -o $@ $(LDFLAGS) $(SDLFLAGS)
%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

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
	@$(RM) $(OBJS) $(TARGET_EXEC)
clean_gen:
	@$(RM) $(RES) zelda3_assets.dat tables/zelda3_assets.dat tables/*.txt tables/*.png tables/sprites/*.png tables/*.yaml
	@rm -rf tables/__pycache__ tables/dungeon tables/img tables/overworld tables/sound
