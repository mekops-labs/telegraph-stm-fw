/* SPDX-License-Identifier: Apache-2.0 */

/* The display in Home Assistant: an outbound socket to an MQTT broker, the
 * discovery of the entities, and their commands as display requests. It
 * reaches neither the broker of the link nor the STM32.
 */

#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <telegraph/display.h>

#include "mqtt.h"

/****************************************************************************
 * Definitions
 ****************************************************************************/

/* The launch config carries these; each has a default that works on a bench
 * with an open broker.
 */

#define ENV_CLIENT "TELEGRAPH_CLIENT"  /* the name of this display client   */
#define ENV_SOCKET "TELEGRAPH_SOCKET"  /* the socket grant of the broker    */
#define ENV_DEVICE "HASS_DEVICE"       /* the device identifier of HA       */
#define ENV_NAME "HASS_NAME"           /* what HA calls the device          */
#define ENV_PREFIX "HASS_PREFIX"       /* the prefix of the topics          */
#define ENV_DISCOVERY "HASS_DISCOVERY" /* the discovery prefix of HA        */
#define ENV_USER "HASS_USER"
#define ENV_PASS "HASS_PASS"

#define CLIENT_DEFAULT "hass"
#define SOCKET_DEFAULT "mqtt"
#define DEVICE_DEFAULT "telegraph"
#define NAME_DEFAULT "Telegraph"
#define PREFIX_DEFAULT "telegraph"
#define DISCOVERY_DEFAULT "homeassistant"

#define KEEPALIVE_S 60u
#define OPEN_MS 10000u
#define POLL_US 10000u
#define RECONNECT_MS 5000u

/* Every sample crosses the link the scan loop of the STM32 shares, thus the
 * cadence is a load decision. The environment variables are spent, thus it
 * comes from a config mount.
 */

#define CONFIG_PATH "/etc/tg-hass.conf"
#define INTERVAL_DEFAULT_S 30u
#define INTERVAL_MIN_S 5u

/* The panels hold this many characters of the compiled-in font. */

#define MAIN_CHARS 40u
#define SUB_CHARS 10u

#define TOPIC_MAX 160u
#define VALUE_MAX 128u

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct tg_dsp_client_s g_display;
static struct mqtt_s g_mqtt;

static const char *g_device = DEVICE_DEFAULT;
static const char *g_name = NAME_DEFAULT;
static const char *g_prefix = PREFIX_DEFAULT;
static const char *g_discovery = DISCOVERY_DEFAULT;
static const char *g_user;
static const char *g_pass;
static char g_socket_path[64];

static char g_avail[TOPIC_MAX];
static char g_state[TOPIC_MAX];
static unsigned int g_interval_s = INTERVAL_DEFAULT_S;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void emit(const char *text) { write(STDOUT_FILENO, text, strlen(text)); }

static void emitf(const char *fmt, ...) {
    char line[192];
    va_list args;

    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    emit(line);
}

static void nap(void) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = POLL_US * 1000};

    nanosleep(&ts, NULL);
}

static uint64_t now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static const char *env_or(const char *name, const char *fallback) {
    const char *v = getenv(name);

    return (v != NULL && v[0] != '\0') ? v : fallback;
}

/* "<prefix>/<device>/<leaf>". */

static void topic(char *out, size_t cap, const char *leaf) {
    snprintf(out, cap, "%s/%s/%s", g_prefix, g_device, leaf);
}

/****************************************************************************
 * The display
 ****************************************************************************/

/* Text on a panel, and the text HA shows for that entity. */

static void set_text(uint8_t panel, const char *body, size_t len) {
    uint8_t payload[VALUE_MAX + TG_DSP_TEXT_BODY];

    if (len > VALUE_MAX) {
        len = VALUE_MAX;
    }

    payload[TG_DSP_TEXT_PANEL] = panel;
    payload[TG_DSP_TEXT_ATTRS] = IPC_ALIGN_CENTRE;
    memcpy(&payload[TG_DSP_TEXT_BODY], body, len);

    int rc = tg_dsp_ask(&g_display, TG_DSP_OP_TEXT, payload,
                        (uint16_t)(TG_DSP_TEXT_BODY + len));
    if (rc != TG_DSP_OK) {
        emitf("hass: the panel %u refused the text (%d)\n", panel, rc);
    }
}

/* One brightness level, for the digits or for the panels. The request carries
 * both, thus the level of the other device is kept.
 */

static uint8_t g_digits = 4;
static uint8_t g_panels = 4;

static void set_bright(bool digits, unsigned int level) {
    uint8_t payload[2];

    if (level > IPC_BRIGHT_MAX) {
        level = IPC_BRIGHT_MAX;
    }

    if (digits) {
        g_digits = (uint8_t)level;
    } else {
        g_panels = (uint8_t)level;
    }

    payload[TG_DSP_BRIGHT_DIGITS] = g_digits;
    payload[TG_DSP_BRIGHT_PANELS] = g_panels;

    int rc = tg_dsp_ask(&g_display, TG_DSP_OP_BRIGHT, payload, sizeof(payload));
    if (rc != TG_DSP_OK) {
        emitf("hass: the brightness was refused (%d)\n", rc);
    }
}

/****************************************************************************
 * The readings
 ****************************************************************************/

/* The cadence of the readings, from the config mount. A board with no mount
 * keeps the default, thus the deployment names one only to change it.
 */

static void read_config(void) {
    char buf[128];
    int fd = open(CONFIG_PATH, O_RDONLY);
    ssize_t n;
    const char *key = "interval_s=";
    const char *at;

    if (fd < 0) {
        return;
    }

    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return;
    }

    buf[n] = '\0';
    at = strstr(buf, key);
    if (at == NULL) {
        return;
    }

    unsigned int v = (unsigned int)strtoul(at + strlen(key), NULL, 10);

    g_interval_s = v < INTERVAL_MIN_S ? INTERVAL_MIN_S : v;
}

/* One reading of the board, as the document the entities read. The display
 * answers the same state frame the REST surface serves.
 */

static void publish_state(void) {
    char body[256];
    const uint8_t *st = g_display.reply;

    int rc = tg_dsp_ask(&g_display, TG_DSP_OP_GET_STATE, NULL, 0);

    if (rc != TG_DSP_OK || g_display.reply_op != TG_DSP_OP_STATE ||
        g_display.reply_len < IPC_STATE_LEN) {
        emitf("hass: no reading (rc %d, op 0x%02x, len %u)\n", rc,
              g_display.reply_op, g_display.reply_len);
        return;
    }

    unsigned int vlen = g_display.reply_len > IPC_STATE_FWVER
                            ? g_display.reply_len - IPC_STATE_FWVER
                            : 0;
    int temp = (int16_t)ipc_get_u16(&st[IPC_STATE_TEMP]);

    int n = snprintf(body, sizeof(body),
                     "{\"temperature\":%d.%d,\"frames\":%u,"
                     "\"crc_errors\":%u,\"resyncs\":%u,\"firmware\":\"%.*s\"}",
                     temp / 10, (temp < 0 ? -temp : temp) % 10,
                     ipc_get_u16(&st[IPC_STATE_FRAMES]),
                     ipc_get_u16(&st[IPC_STATE_CRC_ERR]), st[IPC_STATE_RESYNC],
                     (int)vlen, (const char *)&st[IPC_STATE_FWVER]);

    if (n > 0 && (size_t)n < sizeof(body)) {
        mqtt_publish(&g_mqtt, g_state, body, true);
    }
}

/****************************************************************************
 * Home Assistant
 ****************************************************************************/

/* The discovery of the device, as one document.
 *
 * Note: the identifier is the one the control plane publishes for this
 * device, thus Home Assistant shows one Telegraph and not two.
 */

static int publish_discovery(void) {
    /* Static: a wapp holds 8 KiB of stack, and the document with every
     * component of the device is the largest thing this wapp builds. */
    static char doc[MQTT_BUF_MAX];
    char t[TOPIC_MAX];
    char main_cmd[TOPIC_MAX];
    char main_stat[TOPIC_MAX];
    char sub_cmd[TOPIC_MAX];
    char sub_stat[TOPIC_MAX];
    char dig_cmd[TOPIC_MAX];
    char dig_stat[TOPIC_MAX];
    char pan_cmd[TOPIC_MAX];
    char pan_stat[TOPIC_MAX];

    topic(main_cmd, sizeof(main_cmd), "main/set");
    topic(main_stat, sizeof(main_stat), "main");
    topic(sub_cmd, sizeof(sub_cmd), "sub/set");
    topic(sub_stat, sizeof(sub_stat), "sub");
    topic(dig_cmd, sizeof(dig_cmd), "brightness/digits/set");
    topic(dig_stat, sizeof(dig_stat), "brightness/digits");
    topic(pan_cmd, sizeof(pan_cmd), "brightness/panels/set");
    topic(pan_stat, sizeof(pan_stat), "brightness/panels");

    int n = snprintf(
        doc, sizeof(doc),
        "{\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"MekOps\","
        "\"mdl\":\"Telegraph\"},"
        "\"o\":{\"name\":\"tg-hass\"},"
        "\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":"
        "\"offline\","
        "\"cmps\":{"
        "\"main\":{\"p\":\"text\",\"name\":\"Main panel\",\"cmd_t\":\"%s\","
        "\"stat_t\":\"%s\",\"max\":%u,\"uniq_id\":\"%s_main\"},"
        "\"sub\":{\"p\":\"text\",\"name\":\"Sub panel\",\"cmd_t\":\"%s\","
        "\"stat_t\":\"%s\",\"max\":%u,\"uniq_id\":\"%s_sub\"},"
        "\"digits\":{\"p\":\"number\",\"name\":\"Digit brightness\","
        "\"cmd_t\":\"%s\",\"stat_t\":\"%s\",\"min\":0,\"max\":%u,\"step\":1,"
        "\"uniq_id\":\"%s_digits\"},"
        "\"panels\":{\"p\":\"number\",\"name\":\"Panel brightness\","
        "\"cmd_t\":\"%s\",\"stat_t\":\"%s\",\"min\":0,\"max\":%u,\"step\":1,"
        "\"uniq_id\":\"%s_panels\"},"
        /* The readings. One document on the state topic serves them all, and
         * each component reads its own field of it. */
        "\"temperature\":{\"p\":\"sensor\",\"name\":\"Temperature\","
        "\"stat_t\":\"%s\",\"val_tpl\":\"{{value_json.temperature}}\","
        "\"dev_cla\":\"temperature\",\"unit_of_meas\":\"\u00b0C\","
        "\"stat_cla\":\"measurement\",\"uniq_id\":\"%s_temp\"},"
        "\"frames\":{\"p\":\"sensor\",\"name\":\"Frames\",\"stat_t\":\"%s\","
        "\"val_tpl\":\"{{value_json.frames}}\",\"stat_cla\":\"total_"
        "increasing\","
        "\"ent_cat\":\"diagnostic\",\"uniq_id\":\"%s_frames\"},"
        "\"crc\":{\"p\":\"sensor\",\"name\":\"CRC errors\",\"stat_t\":\"%s\","
        "\"val_tpl\":\"{{value_json.crc_errors}}\","
        "\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\","
        "\"uniq_id\":\"%s_crc\"},"
        "\"resyncs\":{\"p\":\"sensor\",\"name\":\"Resyncs\",\"stat_t\":\"%s\","
        "\"val_tpl\":\"{{value_json.resyncs}}\","
        "\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\","
        "\"uniq_id\":\"%s_resyncs\"},"
        "\"fw\":{\"p\":\"sensor\",\"name\":\"Display firmware\","
        "\"stat_t\":\"%s\",\"val_tpl\":\"{{value_json.firmware}}\","
        "\"ent_cat\":\"diagnostic\",\"uniq_id\":\"%s_fw\"}}}",
        g_device, g_name, g_avail, main_cmd, main_stat, MAIN_CHARS, g_device,
        sub_cmd, sub_stat, SUB_CHARS, g_device, dig_cmd, dig_stat,
        IPC_BRIGHT_MAX, g_device, pan_cmd, pan_stat, IPC_BRIGHT_MAX, g_device,
        g_state, g_device, g_state, g_device, g_state, g_device, g_state,
        g_device, g_state, g_device);

    if (n < 0 || (size_t)n >= sizeof(doc)) {
        emit("hass: the discovery document does not fit\n");
        return -1;
    }

    snprintf(t, sizeof(t), "%s/device/%s/config", g_discovery, g_device);
    return mqtt_publish(&g_mqtt, t, doc, true);
}

/* Subscribe to every command topic. */

static int subscribe_all(void) {
    static const char *const leaves[] = {
        "main/set",
        "sub/set",
        "brightness/digits/set",
        "brightness/panels/set",
    };
    size_t i;

    for (i = 0; i < sizeof(leaves) / sizeof(leaves[0]); i++) {
        char t[TOPIC_MAX];

        topic(t, sizeof(t), leaves[i]);
        if (mqtt_subscribe(&g_mqtt, t) < 0) {
            return -1;
        }
    }

    return 0;
}

/* The state of an entity, as HA reads it back. */

static void echo_state(const char *leaf, const char *value) {
    char t[TOPIC_MAX];

    topic(t, sizeof(t), leaf);
    mqtt_publish(&g_mqtt, t, value, true);
}

/* Does `topic` end with "<prefix>/<device>/<leaf>"? */

static bool is_topic(const char *got, const char *leaf) {
    char want[TOPIC_MAX];

    topic(want, sizeof(want), leaf);
    return strcmp(got, want) == 0;
}

static void on_message(const struct mqtt_msg_s *msg) {
    char value[VALUE_MAX + 1];
    size_t len = msg->payload_len;

    if (len > VALUE_MAX) {
        len = VALUE_MAX;
    }

    memcpy(value, msg->payload, len);
    value[len] = '\0';

    if (is_topic(msg->topic, "main/set")) {
        set_text(TG_DSP_PANEL_MAIN, value, len);
        echo_state("main", value);
    } else if (is_topic(msg->topic, "sub/set")) {
        set_text(TG_DSP_PANEL_SUB, value, len);
        echo_state("sub", value);
    } else if (is_topic(msg->topic, "brightness/digits/set")) {
        set_bright(true, (unsigned int)strtoul(value, NULL, 10));
        echo_state("brightness/digits", value);
    } else if (is_topic(msg->topic, "brightness/panels/set")) {
        set_bright(false, (unsigned int)strtoul(value, NULL, 10));
        echo_state("brightness/panels", value);
    }
}

/****************************************************************************
 * The session
 ****************************************************************************/

/* Connect, announce the entities, and take their commands. Returns when the
 * session ends.
 */

static int session(void) {
    char client_id[64];
    int fd = open(g_socket_path, O_RDWR | O_NONBLOCK);

    if (fd < 0) {
        emitf("hass: %s is out of reach\n", g_socket_path);
        return -1;
    }

    snprintf(client_id, sizeof(client_id), "tg-hass-%s", g_device);
    if (mqtt_connect(&g_mqtt, fd, client_id, g_user, g_pass, g_avail, "offline",
                     KEEPALIVE_S) < 0) {
        emit("hass: the broker took no connection\n");
        close(fd);
        return -1;
    }

    if (mqtt_publish(&g_mqtt, g_avail, "online", true) < 0 ||
        publish_discovery() < 0 || subscribe_all() < 0) {
        emit("hass: the session ended before the entities were announced\n");
        close(fd);
        return -1;
    }

    emitf("hass: %s serves the entities of %s every %u s\n", g_socket_path,
          g_device, g_interval_s);

    uint64_t next = 0; /* the first reading goes out at once */

    for (;;) {
        struct mqtt_msg_s msg;
        int rc = mqtt_poll(&g_mqtt, &msg);

        if (rc < 0) {
            emit("hass: the session ended\n");
            close(fd);
            return -1;
        }

        if (rc > 0) {
            on_message(&msg);
            continue;
        }

        if (now_ms() >= next) {
            publish_state();
            next = now_ms() + (uint64_t)g_interval_s * 1000u;
        }

        if (mqtt_keepalive(&g_mqtt) < 0) {
            emit("hass: the ping did not go out\n");
            close(fd);
            return -1;
        }

        nap();
    }
}

int main(void) {
    const char *client = env_or(ENV_CLIENT, CLIENT_DEFAULT);
    const char *socket_name = env_or(ENV_SOCKET, SOCKET_DEFAULT);

    g_device = env_or(ENV_DEVICE, DEVICE_DEFAULT);
    g_name = env_or(ENV_NAME, NAME_DEFAULT);
    g_prefix = env_or(ENV_PREFIX, PREFIX_DEFAULT);
    g_discovery = env_or(ENV_DISCOVERY, DISCOVERY_DEFAULT);
    g_user = env_or(ENV_USER, "");
    g_pass = env_or(ENV_PASS, "");

    snprintf(g_socket_path, sizeof(g_socket_path), "/net/%s", socket_name);
    topic(g_avail, sizeof(g_avail), "availability");
    topic(g_state, sizeof(g_state), "state");
    read_config();

    if (tg_dsp_open(&g_display, client, OPEN_MS) < 0) {
        emitf("hass: the pipes of the display stayed closed for %s\n", client);
        return 1;
    }

    /* A broker that is not there yet, or a session that drops, is ordinary.
     * Thus this wapp reconnects instead of exiting and leaving the entities
     * of Home Assistant without a device.
     */

    for (;;) {
        session();

        struct timespec ts = {.tv_sec = RECONNECT_MS / 1000, .tv_nsec = 0};

        nanosleep(&ts, NULL);
    }
}
