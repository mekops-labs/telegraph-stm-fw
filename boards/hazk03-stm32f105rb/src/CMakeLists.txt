# SPDX-License-Identifier: Apache-2.0

set(SRCS stm32_boot.c stm32_bringup.c stm32_clockconfig.c)

target_sources(board PRIVATE ${SRCS})

set_property(GLOBAL PROPERTY LD_SCRIPT "${NUTTX_BOARD_DIR}/scripts/ld.script")
