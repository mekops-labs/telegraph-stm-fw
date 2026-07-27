############################################################################
# boards/hazk03-stm32f105rb/src/Makefile
#
# SPDX-License-Identifier: Apache-2.0
#
############################################################################

include $(TOPDIR)/Make.defs

CSRCS  = stm32_boot.c stm32_bringup.c stm32_clockconfig.c
CSRCS += stm32_display.c stm32_rtc.c tm1629a.c sm1626d.c ds3231.c

# The library of the protocol is outside the board directory. Both MCUs use
# the same source.

IPCDIR := $(BOARD_DIR)$(DELIM)..$(DELIM)..$(DELIM)ipc

CFLAGS += ${INCDIR_PREFIX}"$(IPCDIR)$(DELIM)include"

# The UART of the edge MCU also carries the serial console. Thus a build with
# a console gets no server for the protocol.

ifeq ($(CONFIG_NO_SERIAL_CONSOLE),y)
VPATH   += :$(IPCDIR)$(DELIM)src
DEPPATH += --dep-path $(IPCDIR)$(DELIM)src
CSRCS   += stm32_ipc.c font5x7.c sm1626d_text.c
CSRCS   += ipc_crc16.c ipc_encode.c ipc_parser.c
endif

include $(TOPDIR)/boards/Board.mk
