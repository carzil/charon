#ifndef _CHARON_VHOST_
#define _CHARON_VHOST_

#include "utils/string.h"
#include "utils/list.h"
#include "utils/buffer.h"
#include "http/upstream.h"

struct location_s {
    string_t loc;

    struct list_node lnode;
};

struct vhost_s {
    string_t name;
    string_t root;
    http_upstream_t upstream;

    buffer_t path;

    LIST_HEAD_DECLARE(locations);
    struct list_node lnode;
};

typedef struct vhost_s vhost_t;
typedef struct location_s location_t;

int vhost_init(vhost_t* vhost);
void vhost_destroy(vhost_t* v);

#endif
