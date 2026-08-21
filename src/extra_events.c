#define _GNU_SOURCE
#include "extra_events.h"
#include "constants.h"
#include "ipc.h"
#include "chromeos_menu.h"
#include "chromeos_popup.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

#define MAX_EXTRA_CLIENTS 10

void *extra_events_thread_func(void *data) {
    AppState *state = (AppState *)data;
    const char *extra_sock_path = EXTRA_EVENTS_SOCK_PATH;

    int extra_listen_fd = -1;
    int extra_clients[MAX_EXTRA_CLIENTS];
    for (int i = 0; i < MAX_EXTRA_CLIENTS; i++) extra_clients[i] = -1;

    while (1) {
        if (extra_listen_fd < 0) {
            unlink(extra_sock_path);
            extra_listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (extra_listen_fd >= 0) {
                struct sockaddr_un extra_addr;
                memset(&extra_addr, 0, sizeof(extra_addr));
                extra_addr.sun_family = AF_UNIX;
                strncpy(extra_addr.sun_path, extra_sock_path, sizeof(extra_addr.sun_path)-1);
                if (bind(extra_listen_fd, (struct sockaddr *)&extra_addr, sizeof(extra_addr)) < 0 ||
                    listen(extra_listen_fd, 5) < 0) {
                    close(extra_listen_fd);
                    extra_listen_fd = -1;
                }
            }
        }

        if (extra_listen_fd < 0) {
            usleep(500000);
            continue;
        }

        struct pollfd fds[1 + MAX_EXTRA_CLIENTS];
        int nfds = 0;

        fds[nfds].fd = extra_listen_fd;
        fds[nfds].events = POLLIN;
        nfds++;

        for (int i = 0; i < MAX_EXTRA_CLIENTS; i++) {
            if (extra_clients[i] >= 0) {
                fds[nfds].fd = extra_clients[i];
                fds[nfds].events = POLLIN;
                nfds++;
            }
        }

        int ret = poll(fds, nfds, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            usleep(IPC_RECONNECT_DELAY_US);
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                if (fds[i].fd == extra_listen_fd) {
                    int new_fd = accept4(extra_listen_fd, NULL, NULL, SOCK_CLOEXEC);
                    if (new_fd >= 0) {
                        int added = 0;
                        for (int j = 0; j < MAX_EXTRA_CLIENTS; j++) {
                            if (extra_clients[j] < 0) {
                                extra_clients[j] = new_fd;
                                added = 1;
                                break;
                            }
                        }
                        if (!added) close(new_fd);
                    }
                } else {
                    /* Data from a client */
                    for (int j = 0; j < MAX_EXTRA_CLIENTS; j++) {
                        if (extra_clients[j] == fds[i].fd) {
                            char buffer[8192];
                            ssize_t n = read(fds[i].fd, buffer, sizeof(buffer)-1);
                            if (n <= 0) {
                                close(extra_clients[j]);
                                extra_clients[j] = -1;
                            } else {
                                buffer[n] = '\0';
                                char *saveptr, *line = strtok_r(buffer, "\n", &saveptr);
                                while (line) {
                                    handle_ipc_line(state, line);
                                    line = strtok_r(NULL, "\n", &saveptr);
                                }
                            }
                            break;
                        }
                    }
                }
            } else if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                for (int j = 0; j < MAX_EXTRA_CLIENTS; j++) {
                    if (extra_clients[j] == fds[i].fd) {
                        close(extra_clients[j]);
                        extra_clients[j] = -1;
                        break;
                    }
                }
            }
        }
    }
    return NULL;
}
