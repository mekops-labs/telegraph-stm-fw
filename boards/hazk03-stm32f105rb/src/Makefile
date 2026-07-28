############################################################################
# boards/hazk03-stm32f105rb/src/Makefile
#
# SPDX-License-Identifier: Apache-2.0
#
############################################################################

include $(TOPDIR)/Make.defs

CSRCS  = stm32_boot.c stm32_bringup.c stm32_clockconfig.c stm32_hse.c
CSRCS += stm32_display.c stm32_rtc.c tm1629a.c sm1626d.c ds3231.c

# Every configuration shows the version on the main panel at start.
#
# The font of 5x7 is the fallback in the firmware. The extended font comes
# from the flash, and it holds the letters outside the ASCII table.

CSRCS += font5x7.c fontext.c sm1626d_text.c assets.c

ifeq ($(CONFIG_MTD_W25),y)
CSRCS += stm32_spi.c stm32_flash.c
endif

# The library of the protocol is outside the board directory. Both MCUs use
# the same source.

IPCDIR := $(BOARD_DIR)$(DELIM)..$(DELIM)..$(DELIM)ipc

CFLAGS += ${INCDIR_PREFIX}"$(IPCDIR)$(DELIM)include"

VPATH   += :$(IPCDIR)$(DELIM)src
DEPPATH += --dep-path $(IPCDIR)$(DELIM)src

# The store of the settings gives the same check value to each record. Thus
# every configuration builds this file.

CSRCS += ipc_crc16.c

# The UART of the edge MCU also carries the serial console. Thus a build with
# a console gets no server for the protocol.

ifeq ($(CONFIG_NO_SERIAL_CONSOLE),y)
CSRCS   += stm32_ipc.c
CSRCS   += ipc_encode.c ipc_parser.c
endif

include $(TOPDIR)/boards/Board.mk
