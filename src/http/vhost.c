#include <stdlib.h>

#include "defs.h"
#include "http/vhost.h"

vhost_t* vhost_create()
{
    vhost_t* vhost = malloc(sizeof(vhost_t));
    vhost->name = STRING_EMPTY;
    LIST_HEAD_INIT(vhost->locations);
    return vhost;
}

void vhost_destroy(vhost_t* v)
{
    free(v->name.start);
}
