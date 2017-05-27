#ifndef _VECTOR_H_
#define _VECTOR_H_

#include "utils/logging.h"
#include "utils/array.h"
#include "defs.h"

enum {
    VECTOR_INITIAL_CAPACITY = 8,
};

typedef struct {
    size_t capacity;
    size_t size;
} vector_header_t;

#define vector_header(v) (vector_header_t*)(((char*)(v)) - sizeof(vector_header_t))
#define vector_size(v) ((vector_header(*((void**)v)))->size)

/*
 * Vector is defined in user code like: VECTOR_DEFINE(connections, connection_t).
 * It can be be then access like a regular array:
 * connection[i] will return i-th connection in array.
 * Vector consists of header and vector continuously located data. Header is located
 * down from vector data:
 *      +-----------------------+
 *      |   header   |   data   |
 *      +-----------------------+
 *                   ^----------v
 * In user code, vector points here.
 */

static inline void vector_init(void** v)
{
    vector_header_t* h = malloc(sizeof(vector_header_t));
    h->size = 0;
    h->capacity = 0;
    *v = h + 1;
}

static inline int vector_ensure_capacity(void** v, size_t needed, size_t elem_size)
{
    vector_header_t* h = vector_header(*v);
    if (needed > h->capacity) {
        if (h->capacity == 0) {
            h->capacity = VECTOR_INITIAL_CAPACITY;
        }
        while (needed > h->capacity) {
            h->capacity = 3 * h->capacity / 2;
        }
        char* new_ptr = realloc(h, h->capacity * elem_size + sizeof(vector_header_t));
        if (new_ptr == NULL) {
            return -CHARON_NO_MEM;
        }
        *v = new_ptr + sizeof(vector_header_t);
    }
    return CHARON_OK;
}

static inline void vector_set_elem(void** v, size_t where, void* mem, size_t elem_size)
{
    memcpy(((char*)*v) + where * elem_size, mem, elem_size);
}

static inline int __vector_push(void** v, void* mem, size_t elem_size)
{
    vector_header_t* h = vector_header(*v);
    int res = vector_ensure_capacity(v, h->size + 1, elem_size);
    if (res != CHARON_OK) {
        return res;
    }
    h = vector_header(*v);
    vector_set_elem(v, h->size++, mem, elem_size);
    return CHARON_OK;
}

static inline int __vector_resize(void** v, size_t new_size, size_t elem_size)
{
    vector_header_t* h = vector_header(*v);
    if (new_size > h->capacity) {
        int res = vector_ensure_capacity(v, new_size, elem_size);
        if (res != CHARON_OK) {
            return res;
        }
    }
    h = vector_header(*v);
    h->size = new_size;
    return CHARON_OK;
}

static inline int __vector_set(void** v, size_t where, void* mem, size_t elem_size)
{
    int res = vector_ensure_capacity(v, where + 1, elem_size);
    if (res != CHARON_OK) {
        return res;
    }
    vector_set_elem(v, where, mem, elem_size);
    return CHARON_OK;
}

static inline void vector_clean(void** v)
{
    vector_header_t* h = vector_header(*v);
    h->size = 0;
}

static inline void vector_destroy(void** v)
{
    free(vector_header(*v));
}

#define vector_push(v, ptr, type) __vector_push((void**)v, (void*)ptr, sizeof(type))
#define vector_init(v) vector_init((void**)(v))
#define vector_destroy(v) vector_destroy((void**)v)
#define vector_set(v, where, what, type) __vector_set((void**)v, where, what, sizeof(type))
#define vector_resize(v, new_size, type) __vector_resize((void**)v, new_size, sizeof(type))
#define vector_clean(v) vector_clean((void**)v)

#define VECTOR_DEFINE(name, type) type* name


#endif
