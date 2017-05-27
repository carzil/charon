#include <stdlib.h>
#include <linux/limits.h>

#include "defs.h"
#include "http/vhost.h"

int vhost_init(vhost_t* vhost)
{
    vhost->name = STRING_EMPTY;
    vhost->root = STRING_EMPTY;
    vhost->upstream.uri = STRING_EMPTY;
    buffer_malloc(&vhost->path, PATH_MAX);
    list_head_init(vhost->locations);
    return CHARON_OK;
}

void vhost_destroy(vhost_t* v)
{
    free(v->name.start);
    free(v->root.start);
    free(v->path.start);
    http_upstream_destroy(&v->upstream);
}
