/*
 * vim: softtabstop=4 shiftwidth=4 cindent foldmethod=marker expandtab
 *
 * Copyright 2009-2010 Eric Connell 
 * Copyright 2012 Josh Hayes-Sheen <grevian@gmail.com>
 *
 * This file is part of Vumble, it was originally part of Mangler
 *
 * Mangler is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Mangler is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Mangler.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <strings.h>
#include <sys/types.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "ventrilo3.h"

void usage(char *argv[]);
void ctrl_c(int signum);
void *v3_feeder(void *connptr);
void *v3_consumer(void *connptr);

int debug = 2;
int keep_running = 1;

struct _conninfo {
    char *server;
    char *username;
    char *password;
    char *channelid;
};

void ctrl_c (int signum) {
    printf("disconnecting... ");
    v3_logout();
    keep_running = 0;
    printf("done\n");
    exit(0);
}

void usage(char *argv[]) {
    fprintf(stderr, "usage: %s -h hostname:port -u username [-p password] -c channelid\n", argv[0]);
    exit(1);
}

void main_loop(void *connptr) {
    struct _conninfo *conninfo;
    _v3_net_message *msg;
    v3_event *ev;

    conninfo = connptr;
    if (debug >= 2) {
        v3_debuglevel(V3_DEBUG_PACKET | V3_DEBUG_PACKET_PARSE | V3_DEBUG_INFO);
    }
    if (! v3_login(conninfo->server, conninfo->username, conninfo->password, "")) {
        fprintf(stderr, "could not log in to ventrilo server: %s\n", _v3_error(NULL));
    }

    while (keep_running) {

        // Handle incoming events
        msg = _v3_recv(V3_NONBLOCK);
        if ( msg != NULL ) {
            switch (_v3_process_message(msg)) {
                case V3_MALFORMED:
                    _v3_debug(V3_DEBUG_INFO, "received malformed packet");
                    break;
                case V3_NOTIMPL:
                    _v3_debug(V3_DEBUG_INFO, "packet type not implemented");
                    break;
                case V3_OK:
                    _v3_debug(V3_DEBUG_INFO, "packet processed");
                    break;
            }
            // free(msg); // Looks like process_message handles freeing the memory used
        }

        // Handle any outgoing Events
        ev = v3_get_event(V3_NONBLOCK);
        if ( ev != NULL ) {
            if (debug) {
                fprintf(stderr, "vumble: got event type %d\n", ev->type);
            }
            switch (ev->type) {
                case V3_EVENT_DISCONNECT:
                    keep_running = 0;
                    break;
                case V3_EVENT_LOGIN_COMPLETE:
                    v3_change_channel(atoi(conninfo->channelid), "");
                    fprintf(stderr, "***********************************************************************************\n");
                    fprintf(stderr, "Connected to Ventrilo Server\n");
                    fprintf(stderr, "***********************************************************************************\n");
                    v3_serverprop_open();
                    break;
            }
            free(ev);
        }
    }
}

int main(int argc, char *argv[]) {
    int opt;
    int rc;
    struct _conninfo conninfo;

    memset(&conninfo, 0, sizeof(struct _conninfo));
    while ((opt = getopt(argc, argv, "dh:p:u:c:")) != -1) {
        switch (opt) {
            case 'd':
                debug++;
                break;
            case 'h':
                conninfo.server = strdup(optarg);
                break;
            case 'u':
                conninfo.username = strdup(optarg);
                break;
            case 'c':
                conninfo.channelid = strdup(optarg);
                break;
            case 'p':
                conninfo.password = strdup(optarg);
                break;
        }
    }
    if (! conninfo.server)  {
        fprintf(stderr, "error: server hostname (-h) was not specified\n");
        usage(argv);
    }
    if (! conninfo.username)  {
        fprintf(stderr, "error: username (-u) was not specified\n");
        usage(argv);
    }
    if (! conninfo.channelid) {
        fprintf(stderr, "error: channel id (-c) was not specified\n");
        usage(argv);
    }
    if (! conninfo.password) {
        conninfo.password = "";
    }
    fprintf(stderr, "server: %s\nusername: %s\n", conninfo.server, conninfo.username);
    signal (SIGINT, ctrl_c);
    main_loop((void*)&conninfo);
    return(0);
}

