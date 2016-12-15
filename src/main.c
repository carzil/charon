#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"

void usage()
{
    fprintf(stderr, "Usage: charon port\n");
    exit(0);
}

struct my_data {
    struct list_node node;

    int a;
    char c;
    short b;
};

int main(int argc, char *argv[])
{
    // struct list l;
    // list_init(&l);
    // struct my_data arr[10];

    // for (size_t i = 0; i < 10; i++) {
    //     arr[i].a = 100 * i;
    //     arr[i].c = '0' + i;
    //     arr[i].b = 42 + i;
    //     list_append(&l, &arr[i].node);
    // }

    // list_foreach(l, ptr) {
    //     struct my_data* data = list_data(ptr, struct my_data);
    //     charon_debug("%d %c %d", data->a, data->c, data->b);
    // }
    argv += 1;
    argc -= 1;
    return server_main(argc, argv);
}
