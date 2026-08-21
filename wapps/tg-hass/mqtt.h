/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __TG_HASS_MQTT_H
#define __TG_HASS_MQTT_H

/* A client of MQTT 3.1.1, over one socket of the engine. QoS 0 alone: the
 * display shows the last message and a redelivery would change nothing.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The largest message this client sends or takes. The discovery document of
 * the device is the longest of them.
 */

#define MQTT_BUF_MAX 2048u

/* The longest topic of a subscription or a message. */

#define MQTT_TOPIC_MAX 160u

struct mqtt_s {
    int fd;
    uint16_t packet_id;
    uint64_t last_sent_ms;
    unsigned int keepalive_s;
    uint8_t rx[MQTT_BUF_MAX];
    size_t rx_len;
    /* The packet handed over last, dropped at the next poll so its payload
     * stays valid until then. */
    size_t pending;
};

/* A message the broker delivered. The payload is not terminated. */

struct mqtt_msg_s {
    char topic[MQTT_TOPIC_MAX];
    const uint8_t *payload;
    size_t payload_len;
};

/* Connect and take the acknowledgement. The will is published by the broker
 * when this session ends without a DISCONNECT. Returns 0, or -1.
 */

int mqtt_connect(struct mqtt_s *m, int fd, const char *client_id,
                 const char *user, const char *pass, const char *will_topic,
                 const char *will_payload, unsigned int keepalive_s);

/* Publish a payload. A retained message is what a client that subscribes
 * later reads first. Returns 0, or -1.
 */

int mqtt_publish(struct mqtt_s *m, const char *topic, const char *payload,
                 bool retain);

/* Subscribe to one filter at QoS 0. Returns 0, or -1. */

int mqtt_subscribe(struct mqtt_s *m, const char *filter);

/* Take the next message, or nothing. Answers 1 with *msg filled, 0 when
 * nothing arrived, and -1 when the session is over. The payload points into
 * the buffer of `m` and lives until the next call.
 */

int mqtt_poll(struct mqtt_s *m, struct mqtt_msg_s *msg);

/* Send a ping when the keepalive needs one. Returns 0, or -1. */

int mqtt_keepalive(struct mqtt_s *m);

#endif /* __TG_HASS_MQTT_H */
