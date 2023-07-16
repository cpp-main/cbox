#ifndef _CBOX_SIGNAL_EVENT_H_
#define _CBOX_SIGNAL_EVENT_H_

#include <cbox/event/loop.h>

#if defined (__cplusplus)
extern "C" {
#endif

typedef struct cbox_signal_event cbox_signal_event_t;

typedef void (*cbox_signal_function_t)(int signo, void *user);

cbox_signal_event_t *cbox_signal_event_new(cbox_loop_t *loop, const int *signo_array, size_t array_len, cbox_signal_function_t cb, int run_mode, void *user);
void cbox_signal_event_delete(cbox_signal_event_t *obj);

#if defined (__cplusplus)
}
#endif

#endif
