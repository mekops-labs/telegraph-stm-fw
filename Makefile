# SPDX-License-Identifier: Apache-2.0

BOARD       := hazk03-stm32f105rb
CONFIG      := nsh
NUTTX       := third_party/nuttx
APPS        := third_party/nuttx-apps
IMAGE       := telegraph-fw
ENGINE      ?= podman

# The board directory is outside the NuttX tree.
#
# Note: NuttX finds the board through ARCH_BOARD_CUSTOM_DIR in the defconfig.
# Thus the build does not modify the submodule.
BOARD_DIR   := boards/$(BOARD)

UNITY       := third_party/unity
BUILD       := build

# The version comes from the git tags. The header is generated, thus the
# repository does not hold it.
VERSION_H   := $(BOARD_DIR)/src/version.h

# The path of the extended font on the board.
FONT_PATH   := /assets/fonts/default.tgf

# The flasher is the firmware of the edge MCU. It builds with PlatformIO in
# its own container image.
FLASHER_DIR   := tools/hazk-flasher
FLASHER_IMAGE := hazk-pio
FLASHER_ENV   := xiao_esp32s3

.PHONY: help image shell configure build all clean distclean menuconfig \
        savedefconfig test version font compactfont sprites \
        flasher flasher-image flasher-ota wapps wapp-images wapp-test \
        lint-format format-fix tidy cppcheck

help:
	@echo "Targets:"
	@echo "  image         build the container image with the toolchain"
	@echo "  configure     configure NuttX for $(BOARD):$(CONFIG)"
	@echo "  build         build the STM32 firmware, and configure it if necessary"
	@echo "  flasher       build the edge MCU firmware in $(FLASHER_DIR)"
	@echo "  flasher-ota   send the edge MCU firmware over the air"
	@echo "                (UPLOAD_PORT=<address> when mDNS does not resolve)"
	@echo "  wapps         build the wapps of the edge MCU"
	@echo "  wapp-images   package each wapp for the registry of the engine"
	@echo "  wapp-test     run the round trip of the broker (WANTED=<wanted-cli>)"
	@echo "  all           build both firmware images"
	@echo "  version       write the version header from the git tags"
	@echo "  font          build the extended font for the flash"
	@echo "  compactfont   build the compact font, for two lines"
	@echo "  sprites       build the sprites of the animation"
	@echo "  test          build and run the host tests of the IPC library"
	@echo "  lint-format   reject any clang-format drift in boards/ and ipc/"
	@echo "  format-fix    reformat boards/ and ipc/ in place with clang-format"
	@echo "  tidy          clang-tidy the host-buildable ipc/ sources"
	@echo "  cppcheck      cppcheck boards/ and ipc/"
	@echo "  menuconfig    start the NuttX configuration program"
	@echo "  savedefconfig write the configuration to the board defconfig"
	@echo "  clean         remove the build output, keep the configuration"
	@echo "  distclean     remove the build output and the configuration"
	@echo "  shell         start a shell in the container"
	@echo
	@echo "Configurations: nsh (the interactive shell), ipc (the protocol)."
	@echo "Select one with CONFIG=<name>. Do a distclean before a change."

image:
	$(ENGINE) build -t $(IMAGE) -f Containerfile .

# All targets below run in the container.
#
# Note: if the shell is already in the container, the RUN variable is empty.
ifdef INSIDE_CONTAINER
RUN :=
else
# -t needs a real terminal on stdin. CI has none, thus this keeps -i only there.
TTY_FLAG := $(shell test -t 0 && echo -t)
RUN := $(ENGINE) run --rm -i $(TTY_FLAG) --userns=keep-id --security-opt label=disable \
       -v "$(CURDIR):/src" -w /src -e INSIDE_CONTAINER=1 $(IMAGE)
endif

# Give configure.sh the full path of the configuration directory. The form
# with a colon is for a board in the NuttX tree only.
#
# Note: the path is absolute, and the container gives the source a different
# path than the host. Thus the shell resolves it.

$(NUTTX)/.config:
	$(RUN) sh -c 'top=$$PWD; cd $(NUTTX) && \
	  ./tools/configure.sh -l "$$top/$(BOARD_DIR)/configs/$(CONFIG)"'

configure: $(NUTTX)/.config

# The header changes with the git state, thus this target runs every time. The
# script keeps the file untouched when the version is the same.
version:
	@sh tools/genversion.sh $(VERSION_H)

build: configure version
	$(RUN) $(MAKE) -C $(NUTTX) -j$(shell nproc)
	@echo
	@$(RUN) sh -c 'size $(NUTTX)/nuttx' || true

# The extended font of the panels. The board reads it from its flash, thus
# this file is not part of the firmware image.
FONT_FILE := $(BUILD)/default.tgf

font:
	@mkdir -p $(BUILD)
	@python3 tools/mkfont.py -o $(FONT_FILE)
	@echo "Send it with: curl -s --get --data-urlencode \"cmd=putfile \
$(FONT_PATH) $$(xxd -p -c 100000 $(FONT_FILE))\" http://<address>/ipc"

COMPACT_FILE := $(BUILD)/compact.tgf
COMPACT_PATH := /assets/fonts/compact.tgf

compactfont:
	@mkdir -p $(BUILD)
	@python3 tools/mkcompactfont.py -o $(COMPACT_FILE)
	@echo "Send it with: curl -s --get --data-urlencode \"cmd=putfile \
$(COMPACT_PATH) $$(xxd -p -c 100000 $(COMPACT_FILE))\" http://<address>/ipc"

# The sprites of the animation. The board reads them from its own flash, thus
# no pixels go over the link while one plays.
SPRITES := heart sun

sprites:
	@mkdir -p $(BUILD)
	@for s in $(SPRITES); do python3 tools/mksprite.py $$s \
	  -o $(BUILD)/$$s.tgs; done
	@echo "Send each with:"
	@for s in $(SPRITES); do echo "  curl -s --get --data-urlencode \
'cmd=putfile /assets/animations/$$s.tgs '\"\$$(xxd -p -c 100000 \
$(BUILD)/$$s.tgs)\" http://<address>/ipc"; done

# The flasher has its own toolchain, thus it has its own image. Both of these
# targets start a container, thus they run on the host only.
flasher-image:
	$(ENGINE) build -t $(FLASHER_IMAGE) -f $(FLASHER_DIR)/Containerfile \
	  $(FLASHER_DIR)

# The whole repository is the mount, because the flasher compiles the shared
# IPC sources through a relative path, and its version comes from git.
#
# Note: podman does not create a missing bind-mount source, thus this target
# creates the PlatformIO cache directory first.
flasher:
	@mkdir -p "$(HOME)/.cache/$(FLASHER_IMAGE)"
	$(ENGINE) run --rm --userns=keep-id --security-opt label=disable \
	  -v "$(CURDIR):/src" -v "$(HOME)/.cache/$(FLASHER_IMAGE):/pio" \
	  -w /src $(FLASHER_IMAGE) pio run -d $(FLASHER_DIR) -e $(FLASHER_ENV)

# Send the flasher to the edge MCU over the air. The default address is the
# mDNS name. A network without mDNS needs UPLOAD_PORT=<address>.
#
# Note: this path updates the device that carries the link. A bad image thus
# costs the link, and the recovery needs a USB cable.
ifdef UPLOAD_PORT
OTA_ADDR := -e PLATFORMIO_UPLOAD_PORT=$(UPLOAD_PORT)
endif

flasher-ota:
	@mkdir -p "$(HOME)/.cache/$(FLASHER_IMAGE)"
	$(ENGINE) run --rm --userns=keep-id --security-opt label=disable \
	  --network host $(OTA_ADDR) \
	  -v "$(CURDIR):/src" -v "$(HOME)/.cache/$(FLASHER_IMAGE):/pio" \
	  -w /src $(FLASHER_IMAGE) \
	  pio run -d $(FLASHER_DIR) -e $(FLASHER_ENV)_ota -t upload

all: build flasher

# The wapps of the edge MCU. They compile to wasm32-wasi in the wapp SDK image
# of the engine, and each of them packages as a ustar of app.wasm.
WAPP_IMAGE ?= registry.gitlab.com/mekops/wanted/wanted-engine/wapp-sdk
WAPP_DIR   := wapps
WAPP_NAMES := $(patsubst $(WAPP_DIR)/%/Makefile,%,$(wildcard $(WAPP_DIR)/*/Makefile))
WAPP_OUT   := $(BUILD)/wapps

WAPP_RUN := $(ENGINE) run --rm --userns=keep-id --security-opt label=disable \
            -v "$(CURDIR):/src" -w /src --entrypoint=/bin/sh $(WAPP_IMAGE) -c

wapps:
	$(WAPP_RUN) 'make -C $(WAPP_DIR)'

# The round trip of the broker on a host build of the engine. That build needs
# CONFIG_WANTED_VFS_UART=y, and WANTED gives the path of its wanted-cli.
wapp-test: wapps
	WANTED="$(WANTED)" $(WAPP_DIR)/tests/roundtrip.sh

# The registry takes the name and the version from the filename of the image.
wapp-images: wapps
	@mkdir -p $(WAPP_OUT)
	@v=$$(sh tools/wappversion.sh); \
	for w in $(WAPP_NAMES); do \
	  s=$$(mktemp -d); cp $(WAPP_DIR)/$$w/$$w.wasm $$s/app.wasm; \
	  tar --format=ustar -C $$s -cf $(WAPP_OUT)/$$w@$$v-1.wapp app.wasm; \
	  rm -rf $$s; echo "$(WAPP_OUT)/$$w@$$v-1.wapp"; \
	done

# The IPC library is portable C99. Thus the host compiler builds it, and the
# tests run without the hardware.
IPC_SRC   := $(wildcard ipc/src/*.c)
IPC_CF    := -std=c99 -Wall -Wextra -Werror -Iipc/include
TEST_BIN  := $(BUILD)/test_ipc

test:
	$(RUN) sh -c 'mkdir -p $(BUILD) && \
	  cc $(IPC_CF) -I$(UNITY)/src $(IPC_SRC) ipc/tests/test_ipc.c \
	     $(UNITY)/src/unity.c -o $(TEST_BIN) && $(TEST_BIN)'

# The formatter covers this repository's own C sources only: boards/ is the
# board port, ipc/ is the shared framing library. third_party/ is vendored
# NuttX and stays as upstream ships it.
FMT_DIRS  := boards ipc wapps

lint-format:
	$(RUN) sh -c 'find $(FMT_DIRS) \( -name "*.c" -o -name "*.h" \) -print0 \
	  | xargs -0 clang-format --dry-run --Werror'

format-fix:
	$(RUN) sh -c 'find $(FMT_DIRS) \( -name "*.c" -o -name "*.h" \) -print0 \
	  | xargs -0 clang-format -i'

# clang-tidy needs a real compile command. Only ipc/ compiles for the host
# (the same way `make test` does); boards/ needs the ARM cross toolchain and
# the full NuttX header tree, which clang-tidy cannot resolve without a
# generated compilation database this repository does not yet produce.
# No --config-file: clang-tidy discovers the nearest .clang-tidy by walking
# up from each source file, so ipc/.clang-tidy's stricter checks apply to
# ipc/ and the root .clang-tidy applies to anything outside it.
tidy:
	$(RUN) clang-tidy --warnings-as-errors='*' \
	  $(IPC_SRC) -- $(IPC_CF)

# cppcheck parses source directly, so it needs no compile database. `configure`
# gives it the generated nuttx/config.h and the arch/board/chip symlinks that
# a NuttX source file's includes resolve through.
cppcheck: configure
	$(RUN) cppcheck --enable=warning,style,performance,portability \
	  --suppress=missingIncludeSystem --suppress=normalCheckLevelMaxBranches \
	  --suppress=toomanyconfigs --suppress='*:third_party/*' --quiet \
	  --inline-suppr --error-exitcode=1 \
	  -I$(NUTTX)/include -Iipc/include -I$(BOARD_DIR)/include \
	  -I$(BOARD_DIR)/src \
	  boards ipc

menuconfig: configure
	$(RUN) $(MAKE) -C $(NUTTX) menuconfig

savedefconfig: configure
	$(RUN) $(MAKE) -C $(NUTTX) savedefconfig
	cp $(NUTTX)/defconfig $(BOARD_DIR)/configs/$(CONFIG)/defconfig

clean:
	$(RUN) $(MAKE) -C $(NUTTX) clean

distclean:
	$(RUN) $(MAKE) -C $(NUTTX) distclean

shell:
	$(RUN) bash
