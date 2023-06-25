# ifndef _C_LOOP_H_20230703_
#define _C_LOOP_H_20230703_

#include <sys/time.h>
#include <stdint.h>
#include <stddef.h>

#if defined (__cplusplus)
extern "C" {
#endif

#define CBOX_LOOP_RUN_MODE_ONCE (1 << 0)
#define CBOX_LOOP_RUN_MODE_FOREVER (1 << 1)

#define CBOX_EVENT_READ (1 << 2)
#define CBOX_EVENT_WRITE (1 << 3)
#define CBOX_EVENT_EDGE (1 << 4)
#define CBOX_EVENT_BLOCKING (1 << 5)
#define CBOX_EVENT_ERROR (1 << 6)

typedef struct cbox_loop cbox_loop_t;
typedef struct cbox_fd_event cbox_fd_event_t;
typedef struct cbox_basic_timer cbox_basic_timer_t;

typedef void (*cbox_run_in_loop_func_t)(void * /*user*/);
typedef void (*cbox_fd_event_func_t)(uint32_t /*events*/, void * /*user*/);
typedef void (*cbox_timeout_func_t)(void * /*user*/);
//loop
cbox_loop_t *cbox_loop_new();
void cbox_loop_delete(cbox_loop_t *);

void cbox_loop_dispatch(cbox_loop_t * /*cbox_loop*/, uint32_t /*mode*/);
void cbox_loop_delegate(cbox_loop_t * /*cbox_loop*/, cbox_run_in_loop_func_t /*cb*/, void * /*user*/);
void cbox_loop_exit(cbox_loop_t *);

//fd event
cbox_fd_event_t *cbox_fd_event_new(int /*fd*/, uint32_t /*event_type*/, cbox_fd_event_func_t /*cb*/, void * /*user*/);
void cbox_fd_event_delete(cbox_fd_event_t * /*event*/);

int cbox_loop_fd_event_add(cbox_loop_t * /*cbox_loop*/, cbox_fd_event_t * /*event*/);
int cbox_loop_fd_event_mod(cbox_loop_t * /*cbox_loop*/, cbox_fd_event_t * /*event*/, uint32_t /*event_type*/);
int cbox_loop_fd_event_remove(cbox_loop_t * /*cbox_loop*/, cbox_fd_event_t * /*event*/);

cbox_fd_event_t *cbox_loop_fd_event(cbox_loop_t * /*cbox_loop*/, int /*fd*/);
int cbox_fd_event_fd(cbox_fd_event_t * /*event*/);


// timer event
cbox_basic_timer_t *cbox_basic_timer_new(uint64_t /*miliseconds*/, int /*repeat*/, cbox_timeout_func_t /* timeout handler*/, void * /*user*/);
void cbox_basic_timer_delete(cbox_basic_timer_t * /*timer*/);
int cbox_basic_timer_enable(cbox_loop_t * /*loop*/, cbox_basic_timer_t * /*timer*/);
int cbox_basic_timer_disable(cbox_loop_t * /*loop*/, cbox_basic_timer_t * /*timer*/);

#if defined (__cplusplus)
}
#endif

#endif
