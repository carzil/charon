#ifndef _CHARON_ARRAY_
#define _CHARON_ARRAY_

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <memory.h>

#include "defs.h"
#include "utils/logging.h"

/*
 * Array is implementation of dynamic arrays.
 */

struct array {
    size_t size;
    size_t capacity;
    char* data;
};

static inline int array_init(struct array* array, size_t capacity)
{
    array->data = (char*) malloc(capacity);
    array->size = 0;
    array->capacity = capacity;
    if (!array->data) {
        return -CHARON_NO_MEM;
    }
    return CHARON_OK;
}

static inline struct array* array_create(size_t capacity)
{
    struct array* array = (struct array*) malloc(sizeof(struct array));
    array_init(array, capacity);
    return array;
}

static inline int array_ensure_capacity(struct array* array, size_t capacity)
{
    if (capacity > array->capacity) {
        charon_debug("ensure capacity: have %zu bytes, requested %zu bytes", array->capacity, capacity);
        size_t new_capacity = array->capacity;
        while (capacity > new_capacity) {
            new_capacity = 3 * new_capacity / 2;
        }
        char* new_data = malloc(new_capacity);
        if (!new_data) {
            return -CHARON_NO_MEM;
        }
        memcpy(new_data, array->data, array->size);
        free(array->data);
        array->data = new_data;
        array->capacity = new_capacity;
    }
    return CHARON_OK;
}

static inline int array_append(struct array* array, char* ptr, size_t size)
{
    int res;
    if ((res = array_ensure_capacity(array, size + array->size)) < 0) {
        return res;
    }
    memcpy(array->data + array->size, ptr, size);
    array->size += size;
    return CHARON_OK;
}

static inline void array_consume(struct array* array, size_t to_consume)
{
    assert(array->size >= to_consume);
    memmove(array->data, array->data + to_consume, array->size - to_consume);
    array->size -= to_consume;
}

static inline void array_destroy(struct array* array)
{
    free(array->data);
}

static inline void array_clean(struct array* array)
{
    array->size = 0;
}

typedef struct array array_t;

#define array_size(array) ((array)->size)
#define array_capacity(array) ((array)->capacity)
#define array_data(array) ((array)->data)

#endif
