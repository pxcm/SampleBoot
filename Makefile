# SampleBoot - boot selector for the Akai MPC Sample
# Needs an aarch64 Linux cross compiler, e.g. on Debian/Ubuntu:
#   apt install gcc-aarch64-linux-gnu
# The binary is static, so it does not depend on the MPC's libraries.

CROSS  ?= aarch64-linux-gnu-
CC     := $(CROSS)gcc
STRIP  := $(CROSS)strip
CFLAGS ?= -O2 -Wall -Wno-format-truncation

build/sampleboot-ui: src/sampleboot-ui.c src/font_data.h src/stb_image.h
	mkdir -p build
	$(CC) $(CFLAGS) -static -o $@ src/sampleboot-ui.c -lpthread -lm
	$(STRIP) $@

release: build/sampleboot-ui editor
	python3 tools/make_release.py

editor: tools/theme-editor.html

tools/theme-editor.html: tools/editor/editor.html tools/editor/images.json src/font_data.h card/sampleboot.conf card/splash-dark.png card/splash-light.png docs/images/logo-light.svg tools/build_editor.py
	python3 tools/build_editor.py

font:
	python3 tools/gen_font.py

clean:
	rm -rf build dist

.PHONY: release editor font clean
