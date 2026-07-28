# SPDX-License-Identifier: Apache-2.0

BOARD       := hazk03-stm32f105rb
CONFIG      := nsh
NUTTX       := third_party/nuttx
APPS        := third_party/nuttx-apps
IMAGE       := telegraph-fw
CONTAINER   ?= podman

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

.PHONY: help image shell configure build clean distclean menuconfig \
        savedefconfig test version

help:
	@echo "Targets:"
	@echo "  image         build the container image with the toolchain"
	@echo "  configure     configure NuttX for $(BOARD):$(CONFIG)"
	@echo "  build         build the firmware, and configure it if necessary"
	@echo "  version       write the version header from the git tags"
	@echo "  test          build and run the host tests of the IPC library"
	@echo "  menuconfig    start the NuttX configuration program"
	@echo "  savedefconfig write the configuration to the board defconfig"
	@echo "  clean         remove the build output, keep the configuration"
	@echo "  distclean     remove the build output and the configuration"
	@echo "  shell         start a shell in the container"
	@echo
	@echo "Configurations: nsh (the interactive shell), ipc (the protocol)."
	@echo "Select one with CONFIG=<name>. Do a distclean before a change."

image:
	$(CONTAINER) build -t $(IMAGE) -f Containerfile .

# All targets below run in the container.
#
# Note: if the shell is already in the container, the RUN variable is empty.
ifdef INSIDE_CONTAINER
RUN :=
else
RUN := $(CONTAINER) run --rm -it --userns=keep-id --security-opt label=disable \
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

# The IPC library is portable C99. Thus the host compiler builds it, and the
# tests run without the hardware.
IPC_SRC   := $(wildcard ipc/src/*.c)
IPC_CF    := -std=c99 -Wall -Wextra -Werror -Iipc/include
TEST_BIN  := $(BUILD)/test_ipc

test:
	$(RUN) sh -c 'mkdir -p $(BUILD) && \
	  cc $(IPC_CF) -I$(UNITY)/src $(IPC_SRC) ipc/tests/test_ipc.c \
	     $(UNITY)/src/unity.c -o $(TEST_BIN) && $(TEST_BIN)'

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
