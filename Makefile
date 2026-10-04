# Control4Free -- system-wide virtual controllers for the PS4 (GoldHEN payload).
#
# Build:
#   make                                  # browser controller: control4free.elf
#   make C4F_STAGE=1                       # diagnostic probe
#   make C4F_STAGE=4                      # create a virtual pad and press PS
#   make C4F_STAGE=4 C4F_VDA_USER=1       # same, created for user 1 (-> ...-stage4-u1.elf)
#   make clean
#
# A build needs the ps4-payload-sdk; dev/scripts/build.ps1 runs this inside Docker.

ifdef PS4_PAYLOAD_SDK
    include $(PS4_PAYLOAD_SDK)/toolchain/orbis.mk
else
    $(error PS4_PAYLOAD_SDK is undefined -- use dev/scripts/build.ps1, or point it at an SDK)
endif

# Spike stage (see dev/notes/vda.md). Each stage does everything the lower ones do:
#   1 probe only (no scePad call)   2 pad service   3 add a virtual device
#   4 press PS on it   5 raise the selection screen and confirm with Cross
#   6 map the button bits by reading injected samples back
#   7 visual direction test on the selection screen (fallback for 6)
#   8 resident command server on port 4264 (dev/scripts/console.ps1)
C4F_STAGE ?= 0

# Optional: the userId the virtual device is created for. Unset = foreground
# user mapped to a local-user id (0x10000000). Each value gets its own ELF.
C4F_VDA_USER ?=

NAME    := control4free
TAG     := $(if $(filter 0,$(C4F_STAGE)),web,stage$(C4F_STAGE)$(if $(C4F_VDA_USER),-u$(C4F_VDA_USER)))
ELF     := build/$(NAME)$(if $(filter 0,$(C4F_STAGE)),,-$(TAG)).elf
OBJDIR  := build/$(TAG)
SOURCES := src/main.c src/log.c src/vda.c src/server.c
OBJECTS := $(SOURCES:src/%.c=$(OBJDIR)/%.o)
ifeq ($(C4F_STAGE),0)
    SOURCES += src/web.c src/net.c
    OBJECTS := $(SOURCES:src/%.c=$(OBJDIR)/%.o) $(OBJDIR)/client.o
endif

CFLAGS  += -std=gnu11 -Wall -Wextra -Wpointer-arith -g -O2
CFLAGS  += -MMD -MP
CFLAGS  += -Iinclude -Ivendor/jsmn -DC4F_STAGE=$(C4F_STAGE)
CFLAGS  += $(if $(C4F_VDA_USER),-DC4F_VDA_USER=$(C4F_VDA_USER))
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

$(ELF): $(OBJECTS) | build
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)
	@echo "built $@"

clean:
	rm -rf build
