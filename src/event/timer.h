#ifndef _CBOX_TIMER_H_2023_
#define _CBOX_TIMER_H_2023_

#include <time.h>
#include <cbox/event/loop.h>

#if defined (__cplusplus)
extern "C" {
#endif

typedef struct cbox_timer cbox_timer_t;

cbox_timer_t *cbox_timer_new(cbox_loop_t * /*loop*/, struct timespec */*time*/, int /*repeat*/, cbox_timeout_func_t /*cb*/, void * /*user*/);
void cbox_timer_delete(cbox_timer_t * /*timer*/);

int cbox_timer_enable(cbox_timer_t * /*timer*/);
int cbox_timer_enabled(cbox_timer_t * /*timer*/);
int cbox_timer_disable(cbox_timer_t * /*timer*/);

#if defined (__cplusplus)
}
#endif

#endif
