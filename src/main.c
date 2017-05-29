#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "conf.h"
#include "worker.h"
#include "utils/list.h"
#include "timer.h"
#include "defs.h"

worker_t* global_server = NULL;
char* pidfile = NULL;

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
    { "pidfile", required_argument, NULL, 'p' },
    { NULL, 0, NULL, 0 }
};

int spawn_workers(int n)
{
    for (int i = 0; i < n; i++) {
        pid_t wpid = fork();
        if (wpid == 0) {
            global_server->worker_pid = getpid();
            worker_loop(global_server);
            worker_destroy(global_server);
            exit(0);
        }
    }

    return CHARON_OK;
}

void remove_pidfile()
{
    unlink(pidfile);
}

int create_pidfile()
{
    int fd = open(pidfile, O_RDWR | O_EXCL | O_CREAT, 0644);

    if (fd < 0) {
        if (errno == EEXIST) {
            charon_error("pidfile already exists");
            return -CHARON_ERR;
        } else {
            charon_perror("cannot open pidfile: ");
            return -CHARON_ERR;
        }
    }

    FILE* out = fdopen(fd, "w");
    if (fprintf(out, "%d\n", getpid()) < 0) {
        charon_perror("cannot write to pidfile: ");
        fclose(out);
        unlink(pidfile);
        return -CHARON_ERR;
    }
    fclose(out);

    return CHARON_OK;
}

int main(int argc, char* argv[])
{
    int option_index = 0, c;
    char* config_name = NULL;
    int err = 0;

    signal(SIGPIPE, SIG_IGN);

    for (;;) {
        c = getopt_long(argc, argv, "c:p:", charon_options, &option_index);

        if (c == -1) {
            break;
        }

        switch (c) {
        case 'c':
            config_name = optarg;
            break;
        case 'p':
            pidfile = optarg;
            break;
        default:
            err = 1;
            goto error;
        }
    }

    if (optind >= argc) {
        charon_error("no port provided");
        err = 1;
        goto error;
    }

    if (config_name == NULL) {
        charon_error("no config file provided");
        err = 1;
        goto error;
    }

    if (pidfile == NULL) {
        charon_error("no pidfile provided");
        err = 1;
        goto error;
    }

    if (create_pidfile() != CHARON_OK) {
        err = 1;
        goto error;
    }

    global_server = worker_create(config_name);

    signal(SIGINT, sigint_handler);

    atexit(remove_pidfile);

    if (worker_start(global_server, atoi(argv[optind])) == 0) {
        global_server->worker_pid = getpid();
        worker_loop(global_server);
        worker_destroy(global_server);
        // spawn_workers(2);
    } else {
        err = 1;
        goto error;
    }


    // for (int i = 0; i < 2; i++) {
    //     wait(NULL);
    // }
    return 0;

error:
    charon_error("cannot start charon");

    return err;
}
