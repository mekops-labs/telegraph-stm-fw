/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <unity.h>

#include <telegraph/ipc.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

#define MAX_CAPTURED  8

struct capture_s
{
  unsigned int count;
  uint8_t      opcode[MAX_CAPTURED];
  uint16_t     corr_id[MAX_CAPTURED];
  uint16_t     payload_len[MAX_CAPTURED];
  uint8_t      payload[MAX_CAPTURED][64];
};

static struct capture_s g_cap;
static struct ipc_parser_s g_parser;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void on_frame(void *arg, const struct ipc_frame_s *frame)
{
  struct capture_s *cap = (struct capture_s *)arg;

  if (cap->count >= MAX_CAPTURED)
    {
      return;
    }

  cap->opcode[cap->count]      = frame->opcode;
  cap->corr_id[cap->count]     = frame->corr_id;
  cap->payload_len[cap->count] = frame->payload_len;

  if (frame->payload_len > 0 && frame->payload_len <= 64)
    {
      memcpy(cap->payload[cap->count], frame->payload, frame->payload_len);
    }

  cap->count++;
}

static unsigned int feed(const void *data, size_t len)
{
  return ipc_parser_push(&g_parser, data, len, on_frame, &g_cap);
}

void setUp(void)
{
  memset(&g_cap, 0, sizeof(g_cap));
  ipc_parser_init(&g_parser);
}

void tearDown(void)
{
}

/****************************************************************************
 * CRC
 ****************************************************************************/

/* The check value of CRC-16/CCITT-FALSE is 0x29b1 for the string "123456789".
 * This value comes from the catalogue of the CRC algorithms.
 */

static void test_crc16_check_value(void)
{
  TEST_ASSERT_EQUAL_HEX16(0x29b1, ipc_crc16("123456789", 9));
}

static void test_crc16_empty_buffer(void)
{
  TEST_ASSERT_EQUAL_HEX16(0xffff, ipc_crc16("", 0));
}

/****************************************************************************
 * Encoder
 ****************************************************************************/

static void test_encode_layout(void)
{
  const uint8_t payload[3] = { 0x11, 0x22, 0x33 };
  uint8_t buf[32];
  uint16_t crc;
  int n;

  n = ipc_encode(buf, sizeof(buf), IPC_OP_SET_LARGE, 0xbeef, payload, 3);

  TEST_ASSERT_EQUAL_INT(3 + IPC_FRAME_OVERHEAD, n);
  TEST_ASSERT_EQUAL_HEX8(IPC_SOF, buf[0]);
  TEST_ASSERT_EQUAL_HEX8(0x03, buf[1]);            /* LEN, low byte      */
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);            /* LEN, high byte     */
  TEST_ASSERT_EQUAL_HEX8(IPC_OP_SET_LARGE, buf[3]);
  TEST_ASSERT_EQUAL_HEX8(0xef, buf[4]);            /* CORR_ID, low byte  */
  TEST_ASSERT_EQUAL_HEX8(0xbe, buf[5]);            /* CORR_ID, high byte */
  TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, &buf[6], 3);

  crc = ipc_crc16(&buf[1], IPC_HEADER_LEN - 1 + 3);
  TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc & 0xff), buf[9]);
  TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8), buf[10]);
}

static void test_encode_empty_payload(void)
{
  uint8_t buf[32];

  TEST_ASSERT_EQUAL_INT(IPC_FRAME_OVERHEAD,
                        ipc_encode(buf, sizeof(buf), IPC_OP_GET_STATE,
                                   1, NULL, 0));
}

static void test_encode_rejects_bad_arguments(void)
{
  uint8_t buf[32];
  uint8_t payload[4] = { 0 };

  TEST_ASSERT_EQUAL_INT(IPC_ERR_ARG,
                        ipc_encode(NULL, sizeof(buf), IPC_OP_ACK, 1,
                                   payload, 4));
  TEST_ASSERT_EQUAL_INT(IPC_ERR_ARG,
                        ipc_encode(buf, sizeof(buf), IPC_OP_ACK, 1,
                                   NULL, 4));
  TEST_ASSERT_EQUAL_INT(IPC_ERR_SPACE,
                        ipc_encode(buf, 4, IPC_OP_ACK, 1, payload, 4));
  TEST_ASSERT_EQUAL_INT(IPC_ERR_TOO_LARGE,
                        ipc_encode(buf, sizeof(buf), IPC_OP_ACK, 1,
                                   payload, IPC_MAX_PAYLOAD + 1));
}

static void test_encode_ack_and_nack(void)
{
  uint8_t buf[32];

  TEST_ASSERT_EQUAL_INT(1 + IPC_FRAME_OVERHEAD,
                        ipc_encode_ack(buf, sizeof(buf), 0x1234, 7));
  TEST_ASSERT_EQUAL_HEX8(IPC_OP_ACK, buf[3]);
  TEST_ASSERT_EQUAL_HEX8(7, buf[6]);

  TEST_ASSERT_EQUAL_INT(1 + IPC_FRAME_OVERHEAD,
                        ipc_encode_nack(buf, sizeof(buf), 0x1234,
                                        IPC_ERR_BAD_OPCODE));
  TEST_ASSERT_EQUAL_HEX8(IPC_OP_NACK, buf[3]);
  TEST_ASSERT_EQUAL_HEX8(IPC_ERR_BAD_OPCODE, buf[6]);
}

/****************************************************************************
 * Parser
 ****************************************************************************/

static void test_parse_round_trip(void)
{
  const uint8_t payload[5] = { 'h', 'e', 'l', 'l', 'o' };
  uint8_t buf[32];
  int n;

  n = ipc_encode(buf, sizeof(buf), IPC_OP_SET_SMALL, 0x0042, payload, 5);

  TEST_ASSERT_EQUAL_UINT(1, feed(buf, (size_t)n));
  TEST_ASSERT_EQUAL_UINT(1, g_cap.count);
  TEST_ASSERT_EQUAL_HEX8(IPC_OP_SET_SMALL, g_cap.opcode[0]);
  TEST_ASSERT_EQUAL_HEX16(0x0042, g_cap.corr_id[0]);
  TEST_ASSERT_EQUAL_UINT16(5, g_cap.payload_len[0]);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, g_cap.payload[0], 5);
  TEST_ASSERT_EQUAL_UINT32(0, g_parser.stats.crc_errors);
}

/* The DMA of the UART gives the bytes in any quantity. Thus the parser must
 * hold a partial frame across the calls.
 */

static void test_parse_one_byte_at_a_time(void)
{
  const uint8_t payload[4] = { 1, 2, 3, 4 };
  uint8_t buf[32];
  int n;
  int i;

  n = ipc_encode(buf, sizeof(buf), IPC_OP_SET_TIME, 0x0001, payload, 4);

  for (i = 0; i < n - 1; i++)
    {
      TEST_ASSERT_EQUAL_UINT(0, feed(&buf[i], 1));
    }

  TEST_ASSERT_EQUAL_UINT(1, feed(&buf[n - 1], 1));
  TEST_ASSERT_EQUAL_UINT16(4, g_cap.payload_len[0]);
}

static void test_parse_two_frames_in_one_block(void)
{
  uint8_t buf[64];
  int a;
  int b;

  a = ipc_encode(buf, sizeof(buf), IPC_OP_GET_STATE, 0x0007, NULL, 0);
  b = ipc_encode(&buf[a], sizeof(buf) - a, IPC_OP_ACK, 0x0007,
                 "\x05", 1);

  TEST_ASSERT_EQUAL_UINT(2, feed(buf, (size_t)(a + b)));
  TEST_ASSERT_EQUAL_HEX8(IPC_OP_GET_STATE, g_cap.opcode[0]);
  TEST_ASSERT_EQUAL_HEX8(IPC_OP_ACK, g_cap.opcode[1]);
  TEST_ASSERT_EQUAL_HEX8(5, g_cap.payload[1][0]);
}

static void test_parse_rejects_bad_crc(void)
{
  uint8_t buf[32];
  int n;

  n = ipc_encode(buf, sizeof(buf), IPC_OP_SET_LARGE, 0x0003, "ab", 2);
  buf[n - 1] ^= 0xff;

  TEST_ASSERT_EQUAL_UINT(0, feed(buf, (size_t)n));
  TEST_ASSERT_EQUAL_UINT32(1, g_parser.stats.crc_errors);
}

/* Line noise before a frame must not stop the link. */

static void test_parse_skips_leading_garbage(void)
{
  const uint8_t noise[3] = { 0x00, 0xff, 0x5a };
  uint8_t buf[32];
  int n;

  n = ipc_encode(buf, sizeof(buf), IPC_OP_STATE, 0x0009, "z", 1);

  TEST_ASSERT_EQUAL_UINT(0, feed(noise, sizeof(noise)));
  TEST_ASSERT_EQUAL_UINT(1, feed(buf, (size_t)n));
  TEST_ASSERT_EQUAL_UINT32(3, g_parser.stats.dropped);
}

/* A payload byte with the value of the SOF must not make the parser lose the
 * next frame. The parser starts the search after the false SOF.
 */

static void test_parse_recovers_after_false_sof(void)
{
  const uint8_t payload[3] = { IPC_SOF, IPC_SOF, IPC_SOF };
  uint8_t buf[64];
  int bad;
  int good;

  bad = ipc_encode(buf, sizeof(buf), IPC_OP_SET_LARGE, 0x0011, payload, 3);
  buf[bad - 1] ^= 0x01;                      /* make the CRC incorrect */

  good = ipc_encode(&buf[bad], sizeof(buf) - bad, IPC_OP_STATE, 0x0012,
                    "ok", 2);

  TEST_ASSERT_EQUAL_UINT(1, feed(buf, (size_t)(bad + good)));
  TEST_ASSERT_EQUAL_UINT32(1, g_parser.stats.crc_errors);
  TEST_ASSERT_EQUAL_HEX8(IPC_OP_STATE, g_cap.opcode[0]);
  TEST_ASSERT_EQUAL_HEX16(0x0012, g_cap.corr_id[0]);
}

/* A LEN above the maximum comes from corrupt data. The parser must reject it
 * without a wait for the bytes that this length asks for.
 */

static void test_parse_rejects_oversize_length(void)
{
  uint8_t buf[64];
  int n;

  memset(buf, 0, sizeof(buf));
  buf[0] = IPC_SOF;
  buf[1] = 0xff;
  buf[2] = 0xff;

  TEST_ASSERT_EQUAL_UINT(0, feed(buf, IPC_HEADER_LEN));
  TEST_ASSERT_EQUAL_UINT32(1, g_parser.stats.bad_length);

  n = ipc_encode(buf, sizeof(buf), IPC_OP_STATE, 0x0013, "y", 1);
  TEST_ASSERT_EQUAL_UINT(1, feed(buf, (size_t)n));
}

/* The largest payload must pass through the parser. */

static void test_parse_maximum_payload(void)
{
  static uint8_t payload[IPC_MAX_PAYLOAD];
  static uint8_t buf[IPC_FRAME_MAX];
  size_t i;
  int n;

  for (i = 0; i < sizeof(payload); i++)
    {
      payload[i] = (uint8_t)i;
    }

  n = ipc_encode(buf, sizeof(buf), IPC_OP_FLASH, 0x0014, payload,
                 IPC_MAX_PAYLOAD);

  TEST_ASSERT_EQUAL_INT(IPC_FRAME_MAX, n);
  TEST_ASSERT_EQUAL_UINT(1, feed(buf, (size_t)n));
  TEST_ASSERT_EQUAL_UINT16(IPC_MAX_PAYLOAD, g_cap.payload_len[0]);
}

/* A stream of SOF bytes fills the buffer, and it holds no frame. The parser
 * must make space for the data that comes after.
 */

static void test_parse_survives_a_full_buffer(void)
{
  static uint8_t noise[IPC_FRAME_MAX * 2];

  memset(noise, IPC_SOF, sizeof(noise));

  TEST_ASSERT_EQUAL_UINT(0, feed(noise, sizeof(noise)));
  TEST_ASSERT_TRUE(g_parser.stats.bad_length > 0);
  TEST_ASSERT_TRUE(g_parser.len < IPC_FRAME_MAX);
}

/* A false SOF with a length that is possible makes the parser wait for a
 * frame that no sender transmits. A good frame behind it stays in the buffer.
 */

static void test_parse_stalls_on_a_possible_length(void)
{
  const uint8_t noise[5] = {
    IPC_SOF, IPC_SOF, IPC_SOF, IPC_SOF, IPC_SOF
  };
  uint8_t buf[32];
  int n;

  n = ipc_encode(buf, sizeof(buf), IPC_OP_STATE, 0x0015, "r", 1);

  TEST_ASSERT_EQUAL_UINT(0, feed(noise, sizeof(noise)));
  TEST_ASSERT_EQUAL_UINT(0, feed(buf, (size_t)n));
  TEST_ASSERT_TRUE(ipc_parser_pending(&g_parser));

  /* The idle line makes the parser discard the false candidate. Thus the
   * good frame comes out.
   */

  TEST_ASSERT_EQUAL_UINT(1, ipc_parser_timeout(&g_parser, on_frame, &g_cap));
  TEST_ASSERT_EQUAL_HEX8(IPC_OP_STATE, g_cap.opcode[0]);
  TEST_ASSERT_EQUAL_HEX16(0x0015, g_cap.corr_id[0]);
  TEST_ASSERT_FALSE(ipc_parser_pending(&g_parser));
}

/* A timeout on a partial frame discards it, and it keeps the parser ready. */

static void test_timeout_discards_a_partial_frame(void)
{
  uint8_t buf[32];
  int n;

  n = ipc_encode(buf, sizeof(buf), IPC_OP_SET_TIME, 0x0016, "abcd", 4);

  TEST_ASSERT_EQUAL_UINT(0, feed(buf, (size_t)n - 3));
  TEST_ASSERT_TRUE(ipc_parser_pending(&g_parser));
  TEST_ASSERT_EQUAL_UINT(0, ipc_parser_timeout(&g_parser, on_frame, &g_cap));
  TEST_ASSERT_FALSE(ipc_parser_pending(&g_parser));

  TEST_ASSERT_EQUAL_UINT(1, feed(buf, (size_t)n));
  TEST_ASSERT_EQUAL_HEX16(0x0016, g_cap.corr_id[0]);
}

static void test_push_rejects_bad_arguments(void)
{
  uint8_t buf[8] = { 0 };

  TEST_ASSERT_EQUAL_UINT(0, ipc_parser_push(NULL, buf, sizeof(buf),
                                            on_frame, &g_cap));
  TEST_ASSERT_EQUAL_UINT(0, ipc_parser_push(&g_parser, NULL, 4,
                                            on_frame, &g_cap));
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_crc16_check_value);
  RUN_TEST(test_crc16_empty_buffer);

  RUN_TEST(test_encode_layout);
  RUN_TEST(test_encode_empty_payload);
  RUN_TEST(test_encode_rejects_bad_arguments);
  RUN_TEST(test_encode_ack_and_nack);

  RUN_TEST(test_parse_round_trip);
  RUN_TEST(test_parse_one_byte_at_a_time);
  RUN_TEST(test_parse_two_frames_in_one_block);
  RUN_TEST(test_parse_rejects_bad_crc);
  RUN_TEST(test_parse_skips_leading_garbage);
  RUN_TEST(test_parse_recovers_after_false_sof);
  RUN_TEST(test_parse_rejects_oversize_length);
  RUN_TEST(test_parse_maximum_payload);
  RUN_TEST(test_parse_survives_a_full_buffer);
  RUN_TEST(test_parse_stalls_on_a_possible_length);
  RUN_TEST(test_timeout_discards_a_partial_frame);
  RUN_TEST(test_push_rejects_bad_arguments);

  return UNITY_END();
}
