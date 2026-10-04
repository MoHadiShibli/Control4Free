# Control4Free -- system-wide virtual controllers for the PS4 (GoldHEN payload).
#
# Build:
#   make            # build/control4free.elf
#   make clean
#
# A build needs the ps4-payload-sdk; docker/Dockerfile has it, and
# tools/build.ps1 runs this inside the container.

ifdef PS4_PAYLOAD_SDK
    include $(PS4_PAYLOAD_SDK)/toolchain/orbis.mk
else
    $(error PS4_PAYLOAD_SDK is undefined -- build in Docker, or point it at an SDK)
endif

VERSION := $(shell cat VERSION)

NAME    := control4free
ELF     := build/$(NAME).elf
OBJDIR  := build/payload
SOURCES := src/main.c src/log.c src/vda.c src/klog_line.c src/web.c src/net.c
OBJECTS := $(SOURCES:src/%.c=$(OBJDIR)/%.o) $(OBJDIR)/client.o $(OBJDIR)/assets.o

CFLAGS  += -std=gnu11 -Wall -Wextra -Wpointer-arith -g -O2
CFLAGS  += -MMD -MP
CFLAGS  += -Iinclude -Ivendor/jsmn -DC4F_VERSION='"$(VERSION)"'
LDFLAGS += -lScePad -lSceUserService -ldl -lpthread

.PHONY: all clean
.DEFAULT_GOAL := all

-include $(OBJECTS:.o=.d)

all: $(ELF)

$(OBJDIR) build:
	mkdir -p $@

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

build/client.c: client/index.html tools/embed_client.py | build
	python3 tools/embed_client.py $< $@

$(OBJDIR)/client.o: build/client.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# The manifest and icon the page points at, so it can live on a home screen.
build/assets.c: client/manifest.webmanifest client/icon-192.png tools/embed_file.py | build
	python3 tools/embed_file.py $@ c4fManifest=client/manifest.webmanifest c4fIcon=client/icon-192.png

$(OBJDIR)/assets.o: build/assets.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJECTS) | build
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)
	@echo "built $@ ($(VERSION))"

clean:
	rm -rf build
