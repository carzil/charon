#include <stdio.h>
#include "timer.h"
#include "utils/logging.h"

#define PARENT(idx) ((idx - 1) >> 1)
#define LEFT(idx) ((idx << 1) + 1)
#define RIGHT(idx) ((idx << 1) + 2)

void timer_queue_init(timer_queue_t* q)
{
    q->heap = malloc(sizeof(event_t*) * 64);
    q->capacity = 64;
    q->size = 0;
}

void timer_queue_destroy(timer_queue_t* q)
{
    free(q->heap);
}

static inline void __swap(timer_queue_t* q, size_t a, size_t b)
{
    event_t* tmp = q->heap[a];
    q->heap[a]->timer_queue_idx = b;
    q->heap[b]->timer_queue_idx = a;
    q->heap[a] = q->heap[b];
    q->heap[b] = tmp;
}

static void __sift_up(timer_queue_t* q, size_t idx)
{
    while (idx > 0 && q->heap[idx]->expire < q->heap[PARENT(idx)]->expire) {
        __swap(q, idx, PARENT(idx));
        idx = PARENT(idx);
    }
}

static void __sift_down(timer_queue_t* q, size_t idx)
{
    while (LEFT(idx) < q->size) {
        size_t j = LEFT(idx);
        if (RIGHT(idx) < q->size &&
            q->heap[RIGHT(idx)]->expire < q->heap[LEFT(idx)]->expire) {
            j = RIGHT(idx);
        }
        if (q->heap[idx] <= q->heap[j]) {
            break;
        }
        __swap(q, idx, j);
        idx = j;
    }
}

int timer_queue_push(timer_queue_t* q, event_t* ev)
{
    if (q->size + 1 > q->capacity) {
        while (q->size + 1 > q->capacity) {
            q->capacity = 3 * q->capacity / 2;
        }
        event_t** new_heap = realloc(q->heap, q->capacity * sizeof(event_t*));
        if (new_heap == NULL) {
            return CHARON_NO_MEM;
        }
        q->heap = new_heap;
    }
    q->heap[q->size] = ev;
    ev->timer_queue_idx = q->size;
    __sift_up(q, q->size);
    q->size++;
    return CHARON_OK;
}

event_t* timer_queue_top(timer_queue_t* q)
{
    return q->heap[0];
}

void timer_queue_remove(timer_queue_t* q, event_t* ev)
{
    if (ev->timer_queue_idx == TIMER_QUEUE_INVALID_IDX) {
        return;
    }

    if (q->size == 1) {
        q->heap[0] = NULL;
        q->size--;
    } else {
        q->size--;
        __swap(q, ev->timer_queue_idx, q->size);
        __sift_down(q, ev->timer_queue_idx);
        q->heap[q->size] = NULL;
    }

    ev->timer_queue_idx = TIMER_QUEUE_INVALID_IDX;
}

void timer_queue_update(timer_queue_t* q, event_t* ev, msec_t expire)
{
    timer_queue_remove(q, ev);
    ev->expire = expire;
    timer_queue_push(q, ev);
}
