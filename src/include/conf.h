#ifndef _CONF_H_
#define _CONF_H_

#include "http/vhost.h"

struct config_s {
    LIST_HEAD_DECLARE(vhosts);
};

typedef struct config_s config_t;

void config_init(config_t* conf);
config_t* config_create();
void config_destroy(config_t* conf);
int config_open(char* filename, config_t** conf);

#endif
