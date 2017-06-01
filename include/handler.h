#ifndef _CHARON_HANDLER_H_
#define _CHARON_HANDLER_H_

#include "conf.h"

struct connection_s;
struct worker_s;

typedef void (*connection_destroyer_t)(struct connection_s*);

typedef struct handler_s {
    struct connection_s* (*create_connection)(struct worker_s*, struct handler_s*, int fd);
    connection_destroyer_t destroy_connection;
    int (*on_config_done)(struct handler_s*);

    conf_section_def_t* conf_def;
    void* conf;
} handler_t;

#endif
