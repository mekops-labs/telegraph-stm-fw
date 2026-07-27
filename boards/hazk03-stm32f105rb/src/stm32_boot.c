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
 *   Called early in the boot sequence, before any device drivers are
 *   initialised and before the memory subsystem is up.
 *
 ****************************************************************************/

void stm32_boardinitialize(void)
{
}

/****************************************************************************
 * Name: board_late_initialize
 *
 * Description:
 *   Runs after the OS is fully up, so bringup can use driver registration
 *   and the filesystem.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void)
{
  stm32_bringup();
}
#endif

/****************************************************************************
 * Name: board_app_initialize
 ****************************************************************************/

#ifndef CONFIG_BOARD_LATE_INITIALIZE
int board_app_initialize(uintptr_t arg)
{
  return stm32_bringup();
}
#endif
