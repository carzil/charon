#ifndef _VECTOR_H_
#define _VECTOR_H_

#include "utils/logging.h"
#include "utils/array.h"
#include "errdefs.h"

/* TODO: vector_foreach macro */

struct vector {
    struct array buf;
    size_t size;
};

static inline int __vector_init(struct vector* v, size_t type_size, size_t initial_size) {
    int res = array_init(&v->buf, initial_size * type_size);
    if (res < 0) {
        return res;
    }
    v->size = 0;
    return CHARON_OK;
}

static inline struct vector* __vector_create(size_t type_size, size_t initial_size) {
    struct vector* v = (struct vector*) malloc(sizeof(struct vector));
    if (!v) {
        return NULL;
    }
    __vector_init(v, type_size, initial_size);
}

static inline int __vector_push(struct vector* v, size_t type_size, char* mem) {
    int res = array_append(&v->buf, mem, type_size);
    if (res < 0) {
        return res;
    }
    v->size++;
    return CHARON_OK;
}

#define vector_data(v, idx, type) (type*)(array_data(&(v)->buf) + (idx) * sizeof(type))
#define vector_push(v, ptr, type) __vector_push(v, sizeof(type), (char*)ptr)
#define vector_size(v) ((v)->size)
#define vector_init(v, type, sz) __vector_init(v, sizeof(type), sz)
#define vector_create(type, sz) __vector_create(sizeof(type), sz)

#endif
