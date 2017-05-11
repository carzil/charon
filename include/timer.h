#ifndef _TIMER_H_
#define _TIMER_H_

#include <time.h>
#include <inttypes.h>
#include "event.h"
#include "client.h"
#include "utils/list.h"

typedef struct timer_queue_s timer_queue_t;

struct timer_queue_s {
    event_t** heap;
    size_t capacity;
    size_t size;
};

void timer_queue_init(timer_queue_t* q);
void timer_queue_destroy(timer_queue_t* q);

int timer_queue_push(timer_queue_t* q, event_t* ev);
event_t* timer_queue_top(timer_queue_t* q);

inline static int timer_queue_empty(timer_queue_t* q)
{
    return q->size == 0;
}

void timer_queue_remove(timer_queue_t* q, event_t* ev);
void timer_queue_update(timer_queue_t* q, event_t* ev, msec_t expire);

inline static msec_t get_current_msec()
{
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);
    return spec.tv_sec * 1000 + spec.tv_nsec / 1e6;
}

#endif
