/* SPDX-License-Identifier: Apache-2.0 */

#include <telegraph/ipc.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

uint8_t ipc_check_exttemp(const uint8_t *payload, uint16_t len, bool *set,
                          int16_t *tenths) {
    int16_t value;

    if (len == 0) {
        *set = false;
        return IPC_ERR_NONE;
    }

    if (len != IPC_SET_EXTTEMP_LEN) {
        return IPC_ERR_BAD_LENGTH;
    }

    value = (int16_t)ipc_get_u16(payload);

    if (value < IPC_EXTTEMP_MIN || value > IPC_EXTTEMP_MAX) {
        return IPC_ERR_BAD_PAYLOAD;
    }

    *set = true;
    *tenths = value;

    return IPC_ERR_NONE;
}

uint8_t ipc_check_release(const uint8_t *payload, uint16_t len,
                          uint8_t *panel) {
    if (len != IPC_RELEASE_LEN) {
        return IPC_ERR_BAD_LENGTH;
    }

    if (payload[0] != IPC_PANEL_MAIN && payload[0] != IPC_PANEL_SUB) {
        return IPC_ERR_BAD_PAYLOAD;
    }

    *panel = payload[0];

    return IPC_ERR_NONE;
}

uint8_t ipc_check_animstop(const uint8_t *payload, uint16_t len,
                           uint8_t *panel) {
    if (len == 0) {
        *panel = IPC_PANEL_BOTH;
        return IPC_ERR_NONE;
    }

    if (len != IPC_ANIMSTOP_LEN) {
        return IPC_ERR_BAD_LENGTH;
    }

    if (payload[0] != IPC_PANEL_MAIN && payload[0] != IPC_PANEL_SUB) {
        return IPC_ERR_BAD_PAYLOAD;
    }

    *panel = payload[0];

    return IPC_ERR_NONE;
}
