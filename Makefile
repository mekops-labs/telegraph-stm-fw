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

.PHONY: help image shell configure build clean distclean menuconfig \
        savedefconfig test

help:
	@echo "Targets:"
	@echo "  image         build the container image with the toolchain"
	@echo "  configure     configure NuttX for $(BOARD):$(CONFIG)"
	@echo "  build         build the firmware, and configure it if necessary"
	@echo "  test          build and run the host tests of the IPC library"
	@echo "  menuconfig    start the NuttX configuration program"
	@echo "  savedefconfig write the configuration to the board defconfig"
	@echo "  clean         remove the build output, keep the configuration"
	@echo "  distclean     remove the build output and the configuration"
	@echo "  shell         start a shell in the container"

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

$(NUTTX)/.config:
	$(RUN) sh -c 'cd $(NUTTX) && ./tools/configure.sh -l $(CURDIR)/$(BOARD_DIR):$(CONFIG)'

configure: $(NUTTX)/.config

build: configure
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
