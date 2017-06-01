#ifndef _CONF_H_
#define _CONF_H_

#include <stddef.h>
#include "utils/string.h"

enum {
    CONF_ALLOW_MULTIPLE = 1,
    CONF_REQUIRED = 2,
};

typedef int (*conf_type_init_t)(void*);

typedef struct {
    char* name;
    enum {
        CONF_STRING,
        CONF_TIME_INTERVAL,
        CONF_SIZE,
        CONF_INTEGER,
    } type;
    int flags;
    size_t offset;

    unsigned parsed:1;
} conf_field_def_t;

typedef struct {
    char* name;
    int flags;
    conf_field_def_t* allowed_fields;
    conf_type_init_t type_init;
    size_t type_size;
    size_t offset;

    unsigned parsed:1;
} conf_section_def_t;

#define CONF_SECTION(name, flags, allowed_fields, type_init, type_size, offset) \
    ((conf_section_def_t) { name, flags, allowed_fields, (conf_type_init_t) type_init, type_size, offset, 0 } )

#define CONF_END_SECTIONS() \
    ((conf_section_def_t) { NULL, 0, NULL, NULL, 0, 0, 0})

#define CONF_FIELD(name, type, flags, offset) \
    ((conf_field_def_t) { name, type, flags, offset, 0 })

#define CONF_END_FIELDS() \
    ((conf_field_def_t) { NULL, 0, 0, 0, 0 })

typedef struct {
    void* conf;
    conf_section_def_t* sections;
} conf_def_t;

int config_open(char* filename, conf_def_t* c);

#endif
