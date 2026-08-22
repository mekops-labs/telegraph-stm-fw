/* SPDX-License-Identifier: Apache-2.0 */

#include <nuttx/config.h>

#include <debug.h>

#include <arch/board/board.h>
#include <nuttx/board.h>

#include "hazk03.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* Nothing: this runs before the drivers and the memory system exist. */

void stm32_boardinitialize(void) {}

/* The start-up steps of this board, after the OS starts, thus they may use
 * the drivers and the file system.
 */

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void) { stm32_bringup(); }
#endif

/****************************************************************************
 * Name: board_app_initialize
 ****************************************************************************/

#ifndef CONFIG_BOARD_LATE_INITIALIZE
int board_app_initialize(uintptr_t arg) { return stm32_bringup(); }
#endif
