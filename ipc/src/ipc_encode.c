/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <telegraph/ipc.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ipc_encode(void *dst, size_t dstlen, uint8_t opcode, uint16_t corr_id,
               const void *payload, uint16_t payload_len) {
    uint8_t *out = (uint8_t *)dst;
    size_t total = (size_t)payload_len + IPC_FRAME_OVERHEAD;
    uint16_t crc;

    if (out == NULL || (payload == NULL && payload_len > 0)) {
        return IPC_ERR_ARG;
    }

    if (payload_len > IPC_MAX_PAYLOAD) {
        return IPC_ERR_TOO_LARGE;
    }

    if (dstlen < total) {
        return IPC_ERR_SPACE;
    }

    out[0] = IPC_SOF;
    out[1] = (uint8_t)(payload_len & 0xff);
    out[2] = (uint8_t)(payload_len >> 8);
    out[3] = opcode;
    out[4] = (uint8_t)(corr_id & 0xff);
    out[5] = (uint8_t)(corr_id >> 8);

    if (payload_len > 0) {
        memcpy(&out[IPC_HEADER_LEN], payload, payload_len);
    }

    /* The CRC covers the bytes from LEN to the end of the payload. */

    crc = ipc_crc16(&out[1], IPC_HEADER_LEN - 1 + payload_len);

    out[IPC_HEADER_LEN + payload_len] = (uint8_t)(crc & 0xff);
    out[IPC_HEADER_LEN + payload_len + 1] = (uint8_t)(crc >> 8);

    return (int)total;
}

int ipc_encode_ack(void *dst, size_t dstlen, uint16_t corr_id,
                   uint8_t credits) {
    return ipc_encode(dst, dstlen, IPC_OP_ACK, corr_id, &credits, 1);
}

int ipc_encode_nack(void *dst, size_t dstlen, uint16_t corr_id, uint8_t error) {
    return ipc_encode(dst, dstlen, IPC_OP_NACK, corr_id, &error, 1);
}
