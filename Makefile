# SPDX-License-Identifier: Apache-2.0

BOARD       := hazk03-stm32f105rb
CONFIG      := nsh
NUTTX       := third_party/nuttx
APPS        := third_party/nuttx-apps
IMAGE       := telegraph-fw
CONTAINER   ?= podman

# The board lives out of tree; NuttX finds it through ARCH_BOARD_CUSTOM_DIR in
# the defconfig, so the submodule is never modified.
BOARD_DIR   := boards/$(BOARD)

.PHONY: help image shell configure build clean distclean menuconfig savedefconfig

help:
	@echo "Targets:"
	@echo "  image         build the toolchain container image"
	@echo "  configure     configure NuttX for $(BOARD):$(CONFIG)"
	@echo "  build         build the firmware (configures first if needed)"
	@echo "  menuconfig    open the NuttX configuration UI"
	@echo "  savedefconfig write the current config back to the board defconfig"
	@echo "  clean         remove build outputs, keep the configuration"
	@echo "  distclean     remove the configuration too"
	@echo "  shell         interactive shell in the toolchain container"

image:
	$(CONTAINER) build -t $(IMAGE) -f Containerfile .

# Everything below runs inside the container; RUN is a no-op when already in it.
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
