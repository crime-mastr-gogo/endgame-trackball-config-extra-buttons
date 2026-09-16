#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define ARG_UNUSED(x) (void)(x)
#define K_FOREVER (-1)
#define K_MSEC(x) (x)
#define K_MUTEX_DEFINE(n) int n
#define K_THREAD_STACK_DEFINE(n, size) char n[size]
#define K_THREAD_STACK_SIZEOF(n) sizeof(n)
struct k_work { int unused; };
struct k_work_q { int unused; };
struct k_work_delayable { void (*handler)(struct k_work *); bool pending; };
#define K_WORK_DELAYABLE_DEFINE(n, fn) struct k_work_delayable n = {.handler = fn}
static int64_t fake_now, fake_delay;
static inline int64_t k_uptime_get(void) { return fake_now; }
static inline bool k_is_in_isr(void) { return false; }
static inline void k_mutex_lock(int *m, int timeout) { (void)m; (void)timeout; }
static inline void k_mutex_unlock(int *m) { (void)m; }
static inline int k_work_cancel_delayable(struct k_work_delayable *w) { w->pending = false; return 0; }
static inline int k_work_reschedule_for_queue(struct k_work_q *q, struct k_work_delayable *w, int64_t delay) {
    (void)q; fake_delay = delay; w->pending = true; return 0;
}
static inline void k_work_queue_start(struct k_work_q *q, char *stack, size_t size, int priority, void *cfg) {
    (void)q; (void)stack; (void)size; (void)priority; (void)cfg;
}
