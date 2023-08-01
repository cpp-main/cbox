#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "worker.h"
#include "base/macros.h"
#include "base/dqueue.h"

typedef struct
{
    cbox_work_func_t func;
    cbox_work_func_t done;
    struct list_head node;
    void *arg;
} cbox_task_t;

struct cbox_worker
{
    cbox_loop_t *loop;
    struct list_head task_list;
    pthread_t *threads;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int max_worker;
    int exit;
};

static void *cbox_worker_thread_func(void *arg);
static void *cbox_worker_perform_thread_func(void *arg);


cbox_worker_t *cbox_worker_new(cbox_loop_t *loop, unsigned int max_worker)
{
    unsigned int i = 0;
    cbox_worker_t *worker = (cbox_worker_t *)malloc(sizeof(cbox_worker_t));
    if (worker == NULL)
        goto CLEANUP;

    worker->exit = 0;
    worker->loop = loop;
    worker->max_worker = max_worker;
    DQUEUE_CREATE(&worker->task_list);

    if (worker->max_worker <= 0)
        goto CLEANUP;

    pthread_mutex_init(&worker->mutex, NULL);
    pthread_cond_init(&worker->cond, NULL);

    worker->threads = (pthread_t *)malloc(sizeof(pthread_t) * max_worker);
    if (worker->threads == NULL)
        goto CLEANUP;


    for (i = 0; i < max_worker; i++)
        pthread_create(&worker->threads[i], NULL, cbox_worker_thread_func, worker);

    return worker;

CLEANUP:
    CBOX_SAFETY_FREE(worker->threads);
    CBOX_SAFETY_FREE(worker);
    return NULL;
}

void cbox_worker_delete(cbox_worker_t *worker)
{
   int i = 0;
    if (worker == NULL)
        return;

    worker->exit = 1;

    pthread_cond_broadcast(&worker->cond);

    for (i = 0; i < worker->max_worker; ++i)
        pthread_join(worker->threads[i], NULL);

    CBOX_SAFETY_FREE(worker->threads);
    pthread_cond_destroy(&worker->cond);
    pthread_mutex_destroy(&worker->mutex);

    while(!DQUEUE_EMPTY(&worker->task_list)) {
        cbox_task_t *task = DQUEUE_POP_FRONT(&worker->task_list, cbox_task_t, node);
        CBOX_SAFETY_FREE(task);
    }

    CBOX_SAFETY_FREE(worker);
}

void cbox_worker_enqueue_task(cbox_worker_t *worker, cbox_work_func_t func, cbox_work_func_t done, void *user)
{
    pthread_mutex_lock(&worker->mutex);
    cbox_task_t *task = (cbox_task_t *)malloc(sizeof(cbox_task_t));
    if (task == NULL)
        goto END;

    task->arg = user;
    task->done = done;
    task->func = func;

    DQUEUE_PUSH_BACK(&task->node, &worker->task_list);
    pthread_cond_signal(&worker->cond);

END:
    pthread_mutex_unlock(&worker->mutex);

}

cbox_perform_task_t *cbox_worker_perform_task(cbox_work_func_t func, void *user)
{
    cbox_task_t *task = (cbox_task_t *)malloc(sizeof(cbox_task_t));
    task->arg = user;
    task->func = func;
    task->done = NULL;

    pthread_t *thread = (pthread_t *)malloc(sizeof(pthread_t));
    if (thread == NULL)
        return NULL;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(thread, NULL, cbox_worker_perform_thread_func, (void *)task);
    pthread_attr_destroy(&attr);
    return thread;
}

void cbox_worker_perform_wait_task_done(cbox_perform_task_t *task)
{
    if (task == NULL)
        return;

    pthread_t *thread = (pthread_t *)task;
    pthread_join(*thread, NULL);
}

static void *cbox_worker_thread_func(void *arg)
{
    cbox_worker_t *worker = (cbox_worker_t *)arg;
    while (!worker->exit) {
        pthread_mutex_lock(&worker->mutex);
        if (DQUEUE_EMPTY(&worker->task_list))
            pthread_cond_wait(&worker->cond, &worker->mutex);

        if (DQUEUE_EMPTY(&worker->task_list) || worker->exit) {
            pthread_mutex_unlock(&worker->mutex);
            return NULL;
        }

        cbox_task_t *task = DQUEUE_POP_FRONT(&worker->task_list, cbox_task_t, node);
        pthread_mutex_unlock(&worker->mutex);

        if (task && task->func) {
            task->func(task->arg);
            if (task->done)
                cbox_loop_delegate(worker->loop, task->done, task->arg);
        }
        CBOX_SAFETY_FREE(task);
    }

    return NULL;
}

static void *cbox_worker_perform_thread_func(void *arg)
{
    cbox_task_t *task = (cbox_task_t *)arg; 
    if (task && task->func)
        task->func(task->arg);

    CBOX_SAFETY_FREE(task);
    return NULL;
}
