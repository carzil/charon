#include <stdlib.h>
#include <linux/limits.h>

#include "defs.h"
#include "http/vhost.h"

vhost_t* vhost_create()
{
    vhost_t* vhost = malloc(sizeof(vhost_t));
    vhost->name = STRING_EMPTY;
    vhost->root = STRING_EMPTY;
    buffer_malloc(&vhost->path, PATH_MAX);
    list_head_init(vhost->locations);
    return vhost;
}

void vhost_destroy(vhost_t* v)
{
    free(v->name.start);
    free(v->root.start);
    free(v->path.start);
}
