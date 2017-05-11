#ifndef _CONF_H_
#define _CONF_H_

#include "http/vhost.h"

enum {
    CONF_STRING = 1,
    CONF_TIME_INTERVAL = 2,
    CONF_ALLOW_MULTIPLE = 4
};

typedef struct {
    char* name;
    int flags;
    size_t offset;
} conf_field_def_t;

typedef struct {
    char* name;
    int flags;
    conf_field_def_t* allowed_fields;
    size_t type_size;
    size_t offset;
} conf_section_def_t;

int config_open(char* filename, void* conf, conf_section_def_t* conf_def);

#endif
