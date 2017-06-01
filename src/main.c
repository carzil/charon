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
volatile int term_signal = 0;
volatile int alarm_signal = 0;

void usage()
{
    fprintf(stderr, "Usage: charon -c file PORT\n");
}

void stop_signal_handler(UNUSED int sig)
{
    term_signal = 1;
}

void alarm_signal_handler(UNUSED int sig)
{
    alarm_signal = 1;
}

static struct option charon_options[] = {
    { "config", required_argument, NULL, 'c' },
    { "pidfile", required_argument, NULL, 'p' },
    { NULL, 0, NULL, 0 }
};

void remove_pidfile()
{
    unlink(pidfile);
}

int create_pidfile()
{
    int fd = open(pidfile, O_RDWR | O_EXCL | O_CREAT, 0644);

    if (fd < 0) {
        if (errno == EEXIST) {
            charon_fatal("pidfile already exists");
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

void terminate_workers()
{
    charon_info("terminating children");
    worker_kill_all(global_server, SIGTERM);
    alarm(1);
    for (;;) {
        int res = worker_wait_all(global_server);
        if (res == CHARON_OK) {
            charon_info("all children exited");
            break;
        }
        if (alarm_signal) {
            charon_info("terminating timed out, killing children");
            worker_kill_all(global_server, SIGKILL);
        }
    }
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
            goto cleanup;
        }
    }

    if (config_name == NULL) {
        charon_error("no config file provided");
        err = 1;
        goto cleanup;
    }

    if (pidfile == NULL) {
        charon_error("no pidfile provided");
        err = 1;
        goto cleanup;
    }

    if (create_pidfile() != CHARON_OK) {
        err = 1;
        goto cleanup;
    }

    atexit(remove_pidfile);

    global_server = worker_create();
    if (global_server == NULL) {
        err = 1;
        goto cleanup;
    }

    if (worker_configure(global_server, config_name) != CHARON_OK) {
        err = 1;
        goto cleanup;
    }

    if (worker_start(global_server) != CHARON_OK) {
        err = 1;
        goto cleanup;
    }


    // worker_spawn_all(global_server);

    struct sigaction sigact = {
        .sa_flags = 0,
        .sa_handler = stop_signal_handler
    };
    // sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);

    sigact.sa_handler = alarm_signal_handler;
    sigaction(SIGALRM, &sigact, NULL);

    for (;;) {
        int res = worker_wait_all(global_server);
        if (res == CHARON_OK) {
            break;
        }
        if (term_signal) {
            charon_info("received stop signal");
            terminate_workers();
            break;
        }
    }

    worker_run(global_server);

cleanup:
    if (global_server != NULL) {
        worker_destroy(global_server);
    }

    if (err) {
        charon_fatal("cannot spawn workers");
    }

    return err;

}
