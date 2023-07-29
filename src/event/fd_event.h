#ifndef _CBOX_FD_EVENT_H_202307_
#define _CBOX_FD_EVENT_H_202307_

#include <cbox/event/loop.h>

#if defined (__cplusplus)
extern "C" {
#endif

#define CBOX_EVENT_READ (1 << 0)
#define CBOX_EVENT_WRITE (1 << 1)
#define CBOX_EVENT_EXCEPTION (1 << 2)


typedef struct cbox_fd_event cbox_fd_event_t;
typedef void (*cbox_fd_event_func_t)(int fd, uint32_t /*events*/, void * /*user*/);

//fd event
cbox_fd_event_t *cbox_fd_event_new(cbox_loop_t *loop, int /*fd*/, uint32_t /*event_type*/, cbox_fd_event_func_t /*cb*/, int /*run_mode*/, void * /*user*/);
void cbox_fd_event_delete(cbox_fd_event_t * /*event*/);

int cbox_fd_event_enable(cbox_fd_event_t * /*event*/);
int cbox_fd_event_disable(cbox_fd_event_t * /*event*/);
int cbox_fd_event_enabled(cbox_fd_event_t * /*event*/);
int cbox_fd_event_modify(cbox_fd_event_t */*obj*/, uint32_t /*new_events*/);

int cbox_fd_event_fd(cbox_fd_event_t * /*event*/);
uint32_t cbox_fd_event_events(cbox_fd_event_t * /*event*/);

#if defined (__cplusplus)
}
#endif

#endif
