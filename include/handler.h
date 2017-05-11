#ifndef _CHARON_HANDLER_H_
#define _CHARON_HANDLER_H_

#include "conf.h"

struct connection_s;

typedef struct handler_s {
    int (*on_connection_init)(struct connection_s*);
    void (*on_connection_end)(struct connection_s*);
    void (*on_config_done)(struct handler_s*);

    conf_section_def_t* conf_def;
    void* conf;
} handler_t;

#endif
