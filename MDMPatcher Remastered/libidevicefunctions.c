//
//  libidevicefunctions.c
//  MagicRepairManage
//
//  Created by Jan Fabel on 18.01.21.
//

#include "libidevicefunctions.h"


/*
 * iproxy.c -- proxy that enables tcp service access to iOS devices
 *
 * Copyright (C) 2014    Martin Szulecki <m.szulecki@libimobiledevice.org>
 * Copyright (C) 2009    Nikias Bassen <nikias@gmx.li>
 * Copyright (C) 2009    Paul Sladen <libiphone@paul.sladen.org>
 *
 * Based upon iTunnel source code, Copyright (c) 2008 Jing Su.
 * http://www.cs.toronto.edu/~jingsu/itunnel/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netinet/in.h>
#include <signal.h>
#include "common/socket.h"
#include "usbmuxd.h"

#ifndef ETIMEDOUT
#define ETIMEDOUT 138
#endif

static uint16_t listen_port = 0;
static uint16_t device_port = 0;
static char* device_udid = NULL;

struct client_data {
    int fd;
    int sfd;
    volatile int stop_ctos;
    volatile int stop_stoc;
};

static void *run_stoc_loop(void *arg)
{
    struct client_data *cdata = (struct client_data*)arg;
    int recv_len;
    int sent;
    char buffer[131072];

    printf("%s: fd = %d\n", __func__, cdata->fd);

    while (!cdata->stop_stoc && cdata->fd > 0 && cdata->sfd > 0) {
        recv_len = socket_receive_timeout(cdata->sfd, buffer, sizeof(buffer), 0, 5000);
        if (recv_len <= 0) {
            if (recv_len == 0 || recv_len == -ETIMEDOUT) {
                // try again
                continue;
            } else {
                fprintf(stderr, "recv failed: %s\n", strerror(-recv_len));
                break;
            }
        } else {
            // send to socket
            sent = socket_send(cdata->fd, buffer, recv_len);
            if (sent < recv_len) {
                if (sent <= 0) {
                    fprintf(stderr, "send failed: %s\n", strerror(errno));
                    break;
                } else {
                    fprintf(stderr, "only sent %d from %d bytes\n", sent, recv_len);
                }
            }
        }
    }

    socket_close(cdata->fd);

    cdata->fd = -1;
    cdata->stop_ctos = 1;

    return NULL;
}

static void *run_ctos_loop(void *arg)
{
    struct client_data *cdata = (struct client_data*)arg;
    int recv_len;
    int sent;
    char buffer[131072];
#ifdef WIN32
    HANDLE stoc = NULL;
#else
    pthread_t stoc;
#endif

    printf("%s: fd = %d\n", __func__, cdata->fd);

    cdata->stop_stoc = 0;
#ifdef WIN32
    stoc = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)run_stoc_loop, cdata, 0, NULL);
#else
    pthread_create(&stoc, NULL, run_stoc_loop, cdata);
#endif

    while (!cdata->stop_ctos && cdata->fd>0 && cdata->sfd>0) {
        recv_len = socket_receive_timeout(cdata->fd, buffer, sizeof(buffer), 0, 5000);
        if (recv_len <= 0) {
            if (recv_len == 0 || recv_len == -ETIMEDOUT) {
                // try again
                continue;
            } else {
                fprintf(stderr, "recv failed: %s\n", strerror(-recv_len));
                break;
            }
        } else {
            // send to local socket
            sent = socket_send(cdata->sfd, buffer, recv_len);
            if (sent < recv_len) {
                if (sent <= 0) {
                    fprintf(stderr, "send failed: %s\n", strerror(errno));
                    break;
                } else {
                    fprintf(stderr, "only sent %d from %d bytes\n", sent, recv_len);
                }
            }
        }
    }

    socket_close(cdata->fd);

    cdata->fd = -1;
    cdata->stop_stoc = 1;

#ifdef WIN32
    WaitForSingleObject(stoc, INFINITE);
#else
    pthread_join(stoc, NULL);
#endif

    return NULL;
}

static void *acceptor_thread(void *arg)
{
    struct client_data *cdata;
    usbmuxd_device_info_t *dev_list = NULL;
#ifdef WIN32
    HANDLE ctos = NULL;
#else
    pthread_t ctos;
#endif
    int count;

    if (!arg) {
        fprintf(stderr, "invalid client_data provided!\n");
        return NULL;
    }

    cdata = (struct client_data*)arg;

    if ((count = usbmuxd_get_device_list(&dev_list)) < 0) {
        printf("Connecting to usbmuxd failed, terminating.\n");
        free(dev_list);
        if (cdata->fd > 0) {
            socket_close(cdata->fd);
        }
        free(cdata);
        return NULL;
    }

    fprintf(stdout, "Number of available devices == %d\n", count);

    if (dev_list == NULL || dev_list[0].handle == 0) {
        printf("No connected device found, terminating.\n");
        free(dev_list);
        if (cdata->fd > 0) {
            socket_close(cdata->fd);
        }
        free(cdata);
        return NULL;
    }

    usbmuxd_device_info_t *dev = NULL;
    if (device_udid) {
        int i;
        for (i = 0; i < count; i++) {
            if (strncmp(dev_list[i].udid, device_udid, sizeof(dev_list[0].udid)) == 0) {
                dev = &(dev_list[i]);
                break;
            }
        }
    } else {
        dev = &(dev_list[0]);
    }

    if (dev == NULL || dev->handle == 0) {
        printf("No connected/matching device found, disconnecting client.\n");
        free(dev_list);
        if (cdata->fd > 0) {
            socket_close(cdata->fd);
        }
        free(cdata);
        return NULL;
    }

    fprintf(stdout, "Requesting connecion to device handle == %d (serial: %s), port %d\n", dev->handle, dev->udid, device_port);

    cdata->sfd = usbmuxd_connect(dev->handle, device_port);
    free(dev_list);
    if (cdata->sfd < 0) {
        fprintf(stderr, "Error connecting to device!\n");
    } else {
        cdata->stop_ctos = 0;

#ifdef WIN32
        ctos = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)run_ctos_loop, cdata, 0, NULL);
        WaitForSingleObject(ctos, INFINITE);
#else
        pthread_create(&ctos, NULL, run_ctos_loop, cdata);
        pthread_join(ctos, NULL);
#endif
    }

    if (cdata->fd > 0) {
        socket_close(cdata->fd);
    }
    if (cdata->sfd > 0) {
        socket_close(cdata->sfd);
    }
    free(cdata);

    return NULL;
}

int main_iproxy()
{
    int mysock = -1;



    listen_port = 2222;
    device_port = 44;


    if (!listen_port) {
        fprintf(stderr, "Invalid listen_port specified!\n");
        return -EINVAL;
    }

    if (!device_port) {
        fprintf(stderr, "Invalid device_port specified!\n");
        return -EINVAL;
    }

#ifndef WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    // first create the listening socket endpoint waiting for connections.
    mysock = socket_create(listen_port);
    if (mysock < 0) {
        fprintf(stderr, "Error creating socket: %s\n", strerror(errno));
        return -errno;
    } else {
#ifdef WIN32
        HANDLE acceptor = NULL;
#else
        pthread_t acceptor;
#endif
        struct client_data *cdata;
        int c_sock;
        while (1) {
            printf("waiting for connection\n");
            c_sock = socket_accept(mysock, listen_port);
            if (c_sock) {
                printf("accepted connection, fd = %d\n", c_sock);
                cdata = (struct client_data*)malloc(sizeof(struct client_data));
                if (!cdata) {
                    socket_close(c_sock);
                    fprintf(stderr, "ERROR: Out of memory\n");
                    return -1;
                }
                cdata->fd = c_sock;
#ifdef WIN32
                acceptor = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)acceptor_thread, cdata, 0, NULL);
                CloseHandle(acceptor);
#else
                pthread_create(&acceptor, NULL, acceptor_thread, cdata);
                pthread_detach(acceptor);
#endif
            } else {
                break;
            }
        }
        socket_close(c_sock);
        socket_close(mysock);
    }

    return 0;
}

