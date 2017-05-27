#ifndef _CONF_H_
#define _CONF_H_

#include <stddef.h>
#include "utils/string.h"

enum {
    CONF_ALLOW_MULTIPLE = 1
};

typedef int (*conf_type_init_t)(void*);

typedef struct {
    char* name;
    enum {
        CONF_STRING,
        CONF_TIME_INTERVAL,
        CONF_SIZE,
    } type;
    size_t offset;
} conf_field_def_t;

typedef struct {
    char* name;
    int flags;
    conf_field_def_t* allowed_fields;
    conf_type_init_t type_init;
    size_t type_size;
    size_t offset;
} conf_section_def_t;

int config_open(char* filename, void* conf, conf_section_def_t* conf_def);

#endif
