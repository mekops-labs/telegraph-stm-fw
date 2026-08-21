/* SPDX-License-Identifier: Apache-2.0 */

/* MQTT 3.1.1 over one socket of the engine. */

#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mqtt.h"

/* The packet types this client uses, in the high nibble of the first byte. */

#define MQTT_CONNECT 0x10u
#define MQTT_CONNACK 0x20u
#define MQTT_PUBLISH 0x30u
#define MQTT_SUBSCRIBE 0x82u /* the low nibble is reserved as 0b0010 */
#define MQTT_SUBACK 0x90u
#define MQTT_PINGREQ 0xc0u
#define MQTT_PINGRESP 0xd0u

#define MQTT_RETAIN 0x01u

/* CONNECT flags: a clean session, and a will at QoS 0. */

#define CONNECT_CLEAN 0x02u
#define CONNECT_WILL 0x04u
#define CONNECT_WILL_RETAIN 0x20u
#define CONNECT_PASS 0x40u
#define CONNECT_USER 0x80u

#define PROTOCOL_LEVEL 4u

static uint64_t now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

/* ---- encoding ---------------------------------------------------------- */

/* A length prefix of two bytes, then the bytes. */

static size_t put_str(uint8_t *buf, const char *s) {
    size_t n = strlen(s);

    buf[0] = (uint8_t)(n >> 8);
    buf[1] = (uint8_t)(n & 0xffu);
    memcpy(&buf[2], s, n);
    return n + 2;
}

/* The remaining length, seven bits per byte, low group first. */

static size_t put_varint(uint8_t *buf, size_t value) {
    size_t n = 0;

    do {
        uint8_t byte = (uint8_t)(value % 128u);

        value /= 128u;
        if (value > 0) {
            byte |= 0x80u;
        }

        buf[n++] = byte;
    } while (value > 0);

    return n;
}

/* Write a whole buffer, since one write of a socket may take a part. */

static int write_all(int fd, const uint8_t *buf, size_t len) {
    size_t done = 0;

    while (done < len) {
        ssize_t n = write(fd, &buf[done], len - done);

        if (n <= 0) {
            return -1;
        }

        done += (size_t)n;
    }

    return 0;
}

/* Frame a payload of `len` bytes behind its fixed header, and send it. */

static int send_packet(struct mqtt_s *m, uint8_t type, const uint8_t *body,
                       size_t len) {
    uint8_t head[5];
    size_t hlen = 1;

    head[0] = type;
    hlen += put_varint(&head[1], len);
    if (write_all(m->fd, head, hlen) < 0) {
        return -1;
    }

    if (len > 0 && write_all(m->fd, body, len) < 0) {
        return -1;
    }

    m->last_sent_ms = now_ms();
    return 0;
}

/* ---- reading ----------------------------------------------------------- */

/* Take what the socket holds into the buffer. Answers the bytes added, 0 when
 * nothing was ready, and -1 when the session is over.
 */

static int fill(struct mqtt_s *m) {
    if (m->rx_len >= sizeof(m->rx)) {
        return -1; /* a message larger than the buffer cannot be parsed */
    }

    ssize_t n = read(m->fd, &m->rx[m->rx_len], sizeof(m->rx) - m->rx_len);

    if (n > 0) {
        m->rx_len += (size_t)n;
        return (int)n;
    }

    if (n == 0) {
        return -1; /* the broker closed the session */
    }

    return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
}

/* Decode the remaining length at `buf`. Answers the bytes it took, or 0 when
 * the buffer holds no complete value.
 */

static size_t get_varint(const uint8_t *buf, size_t len, size_t *value) {
    size_t v = 0;
    size_t mult = 1;
    size_t i;

    for (i = 0; i < 4 && i < len; i++) {
        v += (size_t)(buf[i] & 0x7fu) * mult;
        if ((buf[i] & 0x80u) == 0) {
            *value = v;
            return i + 1;
        }

        mult *= 128u;
    }

    return 0;
}

/* Drop the first `n` bytes of the buffer. */

static void consume(struct mqtt_s *m, size_t n) {
    if (n >= m->rx_len) {
        m->rx_len = 0;
        return;
    }

    memmove(m->rx, &m->rx[n], m->rx_len - n);
    m->rx_len -= n;
}

/* Wait for one packet of `want` type, discarding anything else. */

static int await_packet(struct mqtt_s *m, uint8_t want, unsigned int wait_ms) {
    uint64_t deadline = now_ms() + wait_ms;

    while (now_ms() < deadline) {
        if (fill(m) < 0) {
            return -1;
        }

        while (m->rx_len >= 2) {
            size_t rem = 0;
            size_t hlen = get_varint(&m->rx[1], m->rx_len - 1, &rem);

            if (hlen == 0 || m->rx_len < 1 + hlen + rem) {
                break; /* the packet is not whole yet */
            }

            uint8_t type = m->rx[0] & 0xf0u;
            bool hit = type == (want & 0xf0u);

            consume(m, 1 + hlen + rem);
            if (hit) {
                return 0;
            }
        }

        struct timespec ts = {.tv_sec = 0, .tv_nsec = 10000000};

        nanosleep(&ts, NULL);
    }

    return -1;
}

/* ---- the interface ----------------------------------------------------- */

int mqtt_connect(struct mqtt_s *m, int fd, const char *client_id,
                 const char *user, const char *pass, const char *will_topic,
                 const char *will_payload, unsigned int keepalive_s) {
    uint8_t body[MQTT_BUF_MAX];
    size_t n = 0;
    uint8_t flags = CONNECT_CLEAN;

    m->fd = fd;
    m->packet_id = 1;
    m->rx_len = 0;
    m->pending = 0;
    m->keepalive_s = keepalive_s;

    n += put_str(&body[n], "MQTT");
    body[n++] = PROTOCOL_LEVEL;

    if (will_topic != NULL && will_topic[0] != '\0') {
        flags |= CONNECT_WILL | CONNECT_WILL_RETAIN;
    }

    if (user != NULL && user[0] != '\0') {
        flags |= CONNECT_USER;
        if (pass != NULL && pass[0] != '\0') {
            flags |= CONNECT_PASS;
        }
    }

    body[n++] = flags;
    body[n++] = (uint8_t)(keepalive_s >> 8);
    body[n++] = (uint8_t)(keepalive_s & 0xffu);
    n += put_str(&body[n], client_id);

    if (flags & CONNECT_WILL) {
        n += put_str(&body[n], will_topic);
        n += put_str(&body[n], will_payload != NULL ? will_payload : "");
    }

    if (flags & CONNECT_USER) {
        n += put_str(&body[n], user);
    }

    if (flags & CONNECT_PASS) {
        n += put_str(&body[n], pass);
    }

    if (send_packet(m, MQTT_CONNECT, body, n) < 0) {
        return -1;
    }

    return await_packet(m, MQTT_CONNACK, 5000u);
}

int mqtt_publish(struct mqtt_s *m, const char *topic, const char *payload,
                 bool retain) {
    uint8_t body[MQTT_BUF_MAX];
    size_t n = put_str(body, topic);
    size_t plen = payload != NULL ? strlen(payload) : 0;

    if (n + plen > sizeof(body)) {
        return -1;
    }

    if (plen > 0) {
        memcpy(&body[n], payload, plen);
        n += plen;
    }

    return send_packet(m, (uint8_t)(MQTT_PUBLISH | (retain ? MQTT_RETAIN : 0)),
                       body, n);
}

int mqtt_subscribe(struct mqtt_s *m, const char *filter) {
    uint8_t body[MQTT_TOPIC_MAX + 8];
    size_t n = 0;

    body[n++] = (uint8_t)(m->packet_id >> 8);
    body[n++] = (uint8_t)(m->packet_id & 0xffu);
    m->packet_id++;
    n += put_str(&body[n], filter);
    body[n++] = 0; /* QoS 0 */

    if (send_packet(m, MQTT_SUBSCRIBE, body, n) < 0) {
        return -1;
    }

    return await_packet(m, MQTT_SUBACK, 5000u);
}

int mqtt_poll(struct mqtt_s *m, struct mqtt_msg_s *msg) {
    /* The payload of the last message pointed into the buffer, thus its
     * packet is dropped here and not when it was handed over.
     */

    if (m->pending > 0) {
        consume(m, m->pending);
        m->pending = 0;
    }

    if (fill(m) < 0) {
        return -1;
    }

    while (m->rx_len >= 2) {
        size_t rem = 0;
        size_t hlen = get_varint(&m->rx[1], m->rx_len - 1, &rem);

        if (hlen == 0 || m->rx_len < 1 + hlen + rem) {
            return 0; /* the packet is not whole yet */
        }

        uint8_t type = m->rx[0] & 0xf0u;
        uint8_t qos = (m->rx[0] >> 1) & 0x03u;
        const uint8_t *body = &m->rx[1 + hlen];
        size_t total = 1 + hlen + rem;

        if (type != MQTT_PUBLISH || rem < 2) {
            consume(m, total);
            continue;
        }

        size_t tlen = ((size_t)body[0] << 8) | body[1];
        size_t off = 2 + tlen;

        /* A message above QoS 0 carries an identifier this client never asked
         * for; skipping it keeps the payload at the right offset.
         */

        if (qos > 0) {
            off += 2;
        }

        if (tlen >= sizeof(msg->topic) || off > rem) {
            consume(m, total);
            continue;
        }

        memcpy(msg->topic, &body[2], tlen);
        msg->topic[tlen] = '\0';
        msg->payload = &body[off];
        msg->payload_len = rem - off;
        m->pending = total;
        return 1;
    }

    return 0;
}

int mqtt_keepalive(struct mqtt_s *m) {
    if (m->keepalive_s == 0) {
        return 0;
    }

    uint64_t due = m->last_sent_ms + (uint64_t)m->keepalive_s * 1000u / 2u;

    if (now_ms() < due) {
        return 0;
    }

    return send_packet(m, MQTT_PINGREQ, NULL, 0);
}
