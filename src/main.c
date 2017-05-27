#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conf.h"
#include "worker.h"
#include "utils/list.h"
#include "timer.h"
#include "defs.h"

worker_t* global_server;

void usage()
{
    fprintf(stderr, "Usage: charon -c file PORT\n");
}

void sigint_handler(UNUSED int sig)
{
    worker_stop(global_server);
}

static struct option charon_options[] = {
    { "config", required_argument, NULL, 'c' },
    { NULL, 0, NULL, 0 }
};

int main(int argc, char* argv[])
{
    int option_index = 0, c;
    char* config_name = NULL;

    signal(SIGPIPE, SIG_IGN);

    for (;;) {
        c = getopt_long(argc, argv, "c:", charon_options, &option_index);

        if (c == -1) {
            break;
        }

        switch (c) {
        case 'c':
            config_name = optarg;
            break;
        default:
            charon_debug("%c", c);
            return 1;
        }
    }

    if (config_name == NULL) {
        fprintf(stderr, "charon: no config file provided\n");
        return 1;
    }

    if (optind >= argc) {
        fprintf(stderr, "charon: no port provided\n");
        return 1;
    }



    global_server = worker_create(config_name);

    signal(SIGINT, sigint_handler);

    if (worker_start(global_server, atoi(argv[optind])) == 0) {
        worker_loop(global_server);
    } else {
        charon_error("cannot start charon server");
    }

    worker_destroy(global_server);

    return 0;
}
