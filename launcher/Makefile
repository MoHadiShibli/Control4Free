ifndef OO_PS4_TOOLCHAIN
$(error OO_PS4_TOOLCHAIN is undefined; use the launcher Docker image)
endif
SDK := $(OO_PS4_TOOLCHAIN)
OUT := ../build/launcher
PKGDIR := $(OUT)/pkg
CC := clang-18
CXX := clang++-18
LD := ld.lld-18
CFLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -O2 -Wall -Wextra -Werror -MMD -MP -D_DEFAULT_SOURCE -isysroot $(SDK) -isystem $(SDK)/include -I../vendor/jsmn -I../vendor/qrcodegen -I../vendor/stb
OBJECTS := $(OUT)/main.o $(OUT)/screen.o $(OUT)/draw.o $(OUT)/service.o $(OUT)/qrcodegen.o $(OUT)/stb_truetype.o $(OUT)/fonts.o
LIBS := -lc -lkernel -lSceVideoOut -lScePad -lSceUserService -lSceSystemService -lSceNetCtl
FONTS := ../vendor/roboto/Roboto-Light.ttf ../vendor/roboto/Roboto-Regular.ttf
.PHONY: all package
.DEFAULT_GOAL := all
-include $(OBJECTS:.o=.d)
all: package
$(OUT):
	mkdir -p $@
$(OUT)/%.o: %.c | $(OUT)
	$(CC) $(CFLAGS) -std=gnu11 -c $< -o $@
$(OUT)/main.o: main.cpp | $(OUT)
	$(CXX) $(CFLAGS) -std=c++17 -fno-exceptions -fno-rtti -c $< -o $@
$(OUT)/qrcodegen.o: ../vendor/qrcodegen/qrcodegen.c | $(OUT)
	$(CC) $(CFLAGS) -std=gnu11 -c $< -o $@
# Vendor code, built with its warnings off.
$(OUT)/stb_truetype.o: stb_truetype.c | $(OUT)
	$(CC) $(CFLAGS) -std=gnu11 -w -c $< -o $@
$(OUT)/fonts.c: $(FONTS) ../tools/embed_file.py | $(OUT)
	python3 ../tools/embed_file.py $@ c4fFontLight=../vendor/roboto/Roboto-Light.ttf c4fFontRegular=../vendor/roboto/Roboto-Regular.ttf
$(OUT)/fonts.o: $(OUT)/fonts.c
	$(CC) $(CFLAGS) -std=gnu11 -c $< -o $@
$(OUT)/launcher.elf: $(OBJECTS)
	$(LD) -m elf_x86_64 -pie --script $(SDK)/link.x --eh-frame-hdr -o $@ $(OBJECTS) -L$(SDK)/lib $(LIBS) $(SDK)/lib/crt1.o
package: $(OUT)/launcher.elf ../build/control4free.elf
	python3 ../tools/package_launcher.py --sdk $(SDK)
