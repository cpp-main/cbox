#ifndef _CBOX_EVENT_WORKER_H_
#define _CBOX_EVENT_WORKER_H_

#include "event/loop.h"

#if defined (__cplusplus)
extern "C" {
#endif
typedef struct cbox_worker cbox_worker_t;
typedef void cbox_perform_task_t;

typedef void (*cbox_work_func_t)(void *user);

cbox_worker_t *cbox_worker_new(cbox_loop_t *loop, unsigned int max_worker);
void cbox_worker_delete(cbox_worker_t *worker);

/*
 *@brief push the task to thread pool and execute it
 *@param worker - the worker object
 *@param task - the task to excute
 *@param done - the callback to be excuted in loop thread when the task is done
 *@param user - the user data
 */
void cbox_worker_enqueue_task(cbox_worker_t *worker, cbox_work_func_t task, cbox_work_func_t done, void *user);

/*
 *@brief create an new threaed to excute the task
 *@param task - the task to excute
 *@param user - the user data
 */
cbox_perform_task_t *cbox_worker_perform_task(cbox_work_func_t task, void *user);
void cbox_worker_perform_wait_task_done(cbox_perform_task_t *);

#if defined (__cplusplus)
}
#endif
#endif
