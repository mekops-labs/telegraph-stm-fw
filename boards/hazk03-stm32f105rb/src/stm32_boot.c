/* SPDX-License-Identifier: Apache-2.0 */

#include <nuttx/config.h>

#include <debug.h>

#include <arch/board/board.h>
#include <nuttx/board.h>

#include "hazk03.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_boardinitialize
 *
 * Description:
 *   Do nothing.
 *
 *   Note: the boot sequence calls this function early. At that time the
 *   device drivers and the memory system are not available.
 *
 ****************************************************************************/

void stm32_boardinitialize(void) {}

/****************************************************************************
 * Name: board_late_initialize
 *
 * Description:
 *   Do the start-up steps for this board.
 *
 *   Note: the system calls this function after the OS start. Thus the
 *   start-up steps can use the drivers and the file system.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void) { stm32_bringup(); }
#endif

/****************************************************************************
 * Name: board_app_initialize
 ****************************************************************************/

#ifndef CONFIG_BOARD_LATE_INITIALIZE
int board_app_initialize(uintptr_t arg) { return stm32_bringup(); }
#endif
