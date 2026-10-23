ifndef OO_PS4_TOOLCHAIN
$(error OO_PS4_TOOLCHAIN is undefined; use the launcher Docker image)
endif
SDK := $(OO_PS4_TOOLCHAIN)
VERSION := $(shell cat ../VERSION)
OUT := ../build/launcher
PAYLOAD := ../build/control4free.elf
OBJECTS :=
# Diagnostic build: pages of everything that says why Control4Free does not
# start, in place of the normal screen, with the diagnostic payload inside.
ifdef C4F_DIAG
VERSION := $(VERSION)-diag
OUT := ../build/launcher-diag
PAYLOAD := ../build/control4free-diag.elf
DIAG_FLAGS := -DC4F_DIAG
OBJECTS += $(OUT)/diag.o
endif
PKGDIR := $(OUT)/pkg
CC := clang-18
CXX := clang++-18
LD := ld.lld-18
CFLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -O2 -Wall -Wextra -Werror -MMD -MP -D_DEFAULT_SOURCE -isysroot $(SDK) -isystem $(SDK)/include -I../vendor/jsmn -I../vendor/qrcodegen -I../vendor/stb -DC4F_LAUNCHER_VERSION='"$(VERSION)"' $(DIAG_FLAGS)
CFLAGS += -I../vendor/harfbuzz/src -I../vendor/sheenbidi/Headers
OBJECTS += $(OUT)/main.o $(OUT)/input.o $(OUT)/navigation.o $(OUT)/screen.o $(OUT)/tester.o $(OUT)/tester_draw.o $(OUT)/draw.o $(OUT)/language.o $(OUT)/service.o $(OUT)/autorun.o $(OUT)/sandbox.o $(OUT)/logo.o $(OUT)/qrcodegen.o $(OUT)/stb_truetype.o $(OUT)/fonts.o
OBJECTS += $(OUT)/text.o
OBJECTS += $(OUT)/message.o
TEXT_LIBS := ../build/text/libharfbuzz.a ../build/text/libsheenbidi.a
LIBS := -lc -lkernel -lSceVideoOut -lScePad -lSceUserService -lSceSystemService -lSceNetCtl
FONTS := ../vendor/roboto/Roboto-Light.ttf ../vendor/roboto/Roboto-Regular.ttf
FONTS += ../vendor/noto-sans-arabic/NotoSansArabic-Light.ttf ../vendor/noto-sans-arabic/NotoSansArabic-Regular.ttf
.PHONY: all package
.DEFAULT_GOAL := all
-include $(OBJECTS:.o=.d)
all: package
$(OUT):
	mkdir -p $@
# The version is compiled in; a new one must rebuild everything that shows it.
$(OUT)/%.o: %.c ../VERSION | $(OUT)
	$(CC) $(CFLAGS) -std=gnu11 -c $< -o $@
$(OUT)/main.o: main.cpp ../VERSION | $(OUT)
	$(CXX) $(CFLAGS) -std=c++17 -fno-exceptions -fno-rtti -c $< -o $@
$(OUT)/qrcodegen.o: ../vendor/qrcodegen/qrcodegen.c | $(OUT)
	$(CC) $(CFLAGS) -std=gnu11 -c $< -o $@
# Vendor code, built with its warnings off.
$(OUT)/stb_truetype.o: stb_truetype.c | $(OUT)
	$(CC) $(CFLAGS) -std=gnu11 -w -c $< -o $@
$(OUT)/fonts.c: $(FONTS) ../tools/embed_file.py | $(OUT)
	python3 ../tools/embed_file.py $@ c4fFontLight=../vendor/roboto/Roboto-Light.ttf c4fFontRegular=../vendor/roboto/Roboto-Regular.ttf c4fFontArabicLight=../vendor/noto-sans-arabic/NotoSansArabic-Light.ttf c4fFontArabicRegular=../vendor/noto-sans-arabic/NotoSansArabic-Regular.ttf
$(OUT)/fonts.o: $(OUT)/fonts.c
	$(CC) $(CFLAGS) -std=gnu11 -c $< -o $@
$(TEXT_LIBS) &: ../tools/build_text.py $(shell find ../vendor/harfbuzz/src ../vendor/sheenbidi -type f)
	python3 ../tools/build_text.py --out ../build/text
$(OUT)/launcher.elf: $(OBJECTS) $(TEXT_LIBS)
	$(LD) -m elf_x86_64 -pie --script $(SDK)/link.x --eh-frame-hdr -o $@ $(OBJECTS) $(TEXT_LIBS) -L$(SDK)/lib $(LIBS) $(SDK)/lib/crt1.o
package: $(OUT)/launcher.elf $(PAYLOAD)
	python3 ../tools/package_launcher.py --sdk $(SDK) $(if $(C4F_DIAG),--diag)
