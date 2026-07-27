/* SPDX-License-Identifier: Apache-2.0 */

#include <telegraph/ipc.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* The table holds the CRC of each 4-bit value.
 *
 * Note: a table for 8 bits is faster, and it costs 512 bytes of flash. A loop
 * for each bit costs no flash, and it needs about 8 times more cycles. This
 * table of 32 bytes takes two steps for each byte.
 */

static const uint16_t g_crc16_nibble[16] =
{
  0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
  0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

uint16_t ipc_crc16(const void *data, size_t len)
{
  const uint8_t *p = (const uint8_t *)data;
  uint16_t crc = 0xffff;

  if (p == NULL)
    {
      return crc;
    }

  while (len-- > 0)
    {
      uint8_t byte = *p++;

      crc = (uint16_t)((crc << 4) ^
                       g_crc16_nibble[((crc >> 12) ^ (byte >> 4)) & 0x0f]);
      crc = (uint16_t)((crc << 4) ^
                       g_crc16_nibble[((crc >> 12) ^ (byte & 0x0f)) & 0x0f]);
    }

  return crc;
}
