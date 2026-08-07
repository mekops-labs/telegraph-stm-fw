/* SPDX-License-Identifier: Apache-2.0 */

/* The logs of every wapp, over HTTP.
 *
 * Note: a board of a deployment has no console. This wapp takes a log mount of
 * the engine and a listening socket, thus what a wapp printed is readable from
 * the network.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/****************************************************************************
 * Definitions
 ****************************************************************************/

/* The mount of the logs and the listening socket, both from the launch
 * config. The defaults are what the deployment of this device grants.
 */

#define MOUNT_ENV "TELEGRAPH_LOGS"
#define MOUNT_DEFAULT "/logs"

#define SOCKET_ENV "TELEGRAPH_SOCKET"
#define SOCKET_DEFAULT "http"

#define REQUEST_MAX 512u
#define NAME_MAX_LEN 32u
/* The node of a log gives its whole ring in one read and then EOF, thus this
 * buffer holds a ring of the engine rather than a convenient chunk. A smaller
 * one returns the oldest bytes alone, which reads as a log that never moves.
 */

#define CHUNK 4096u
#define POLL_US 10000u

/****************************************************************************
 * Private Data
 ****************************************************************************/

static char g_mount[64] = MOUNT_DEFAULT;

/* The ring of a log, and the request. Both are static: a wapp holds 8 KiB of
 * stack, and a buffer of this size on it traps the wapp instead of serving.
 */

static char g_body[CHUNK];
static char g_request[REQUEST_MAX + 1];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void emit(const char *text) { write(STDOUT_FILENO, text, strlen(text)); }

static void nap(void) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = POLL_US * 1000};

    nanosleep(&ts, NULL);
}

static void head(int fd, const char *status, const char *type) {
    char buf[128];
    int n = snprintf(buf, sizeof(buf),
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\n"
                     "Connection: close\r\n\r\n",
                     status, type);

    if (n > 0) {
        write(fd, buf, (size_t)n);
    }
}

/* The names of the wapps that have a log. */

static void route_index(int fd) {
    DIR *dir = opendir(g_mount);
    struct dirent *e;
    bool first = true;

    if (dir == NULL) {
        head(fd, "503 Service Unavailable", "application/json");
        write(fd, "{\"error\":\"no log mount\"}\n", 25);
        return;
    }

    head(fd, "200 OK", "application/json");
    write(fd, "[", 1);
    while ((e = readdir(dir)) != NULL) {
        char item[NAME_MAX_LEN + 8];
        int n;

        if (e->d_name[0] == '.') {
            continue;
        }

        n = snprintf(item, sizeof(item), "%s\"%s\"", first ? "" : ",",
                     e->d_name);
        if (n > 0) {
            write(fd, item, (size_t)n);
        }

        first = false;
    }

    write(fd, "]\n", 2);
    closedir(dir);
}

/* One wapp's ring, as it stands. */

static void route_log(int fd, const char *name) {
    char path[128];
    int log;
    ssize_t n;

    if (name[0] == '\0' || strchr(name, '/') != NULL ||
        strlen(name) > NAME_MAX_LEN) {
        head(fd, "400 Bad Request", "application/json");
        write(fd, "{\"error\":\"name\"}\n", 17);
        return;
    }

    snprintf(path, sizeof(path), "%s/%s", g_mount, name);
    log = open(path, O_RDONLY);
    if (log < 0) {
        head(fd, "404 Not Found", "application/json");
        write(fd, "{\"error\":\"no such log\"}\n", 24);
        return;
    }

    head(fd, "200 OK", "text/plain");
    while ((n = read(log, g_body, sizeof(g_body))) > 0) {
        write(fd, g_body, (size_t)n);
    }

    close(log);
}

/* One request: the index, or one wapp's log. */

static void serve(int fd) {
    char *buf = g_request;
    size_t len = 0;
    const char *sep;
    const char *sp1;
    const char *sp2;
    char path[NAME_MAX_LEN + 2];

    for (;;) {
        ssize_t n = read(fd, &buf[len], REQUEST_MAX - len);

        if (n <= 0) {
            break;
        }

        len += (size_t)n;
        buf[len] = '\0';
        sep = strstr(buf, "\r\n\r\n");
        if (sep != NULL || len == REQUEST_MAX) {
            break;
        }
    }

    if (len == 0) {
        return;
    }

    buf[len] = '\0';
    sp1 = memchr(buf, ' ', len);
    sp2 = (sp1 != NULL) ? memchr(sp1 + 1, ' ', len - (size_t)(sp1 + 1 - buf))
                        : NULL;
    if (sp1 == NULL || sp2 == NULL || (size_t)(sp2 - sp1) >= sizeof(path)) {
        head(fd, "400 Bad Request", "application/json");
        write(fd, "{\"error\":\"request\"}\n", 20);
        return;
    }

    memcpy(path, sp1 + 1, (size_t)(sp2 - sp1 - 1));
    path[sp2 - sp1 - 1] = '\0';

    if (strcmp(path, "/") == 0) {
        route_index(fd);
    } else {
        route_log(fd, &path[1]);
    }
}

int main(void) {
    const char *mount = getenv(MOUNT_ENV);
    const char *sock = getenv(SOCKET_ENV);
    char path[64];
    int lfd;

    if (mount != NULL && mount[0] != '\0') {
        snprintf(g_mount, sizeof(g_mount), "%s", mount);
    }

    snprintf(path, sizeof(path), "/net/%s",
             (sock != NULL && sock[0] != '\0') ? sock : SOCKET_DEFAULT);
    lfd = open(path, O_RDWR);
    if (lfd < 0) {
        emit("logs: the listening socket is out of reach\n");
        return 1;
    }

    emit("logs: serving\n");

    for (;;) {
        int cfd = accept(lfd, NULL, NULL);

        if (cfd < 0) {
            nap();
            continue;
        }

        serve(cfd);
        close(cfd);
    }
}
