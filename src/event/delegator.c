#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <cbox/base/list.h>
#include <cbox/base/macros.h>

#include "delegator.h"

#define DQUEUE_POP_FRONT(head, type_of_struct, member_name) ({\
        type_of_struct *temp = list_entry(((head)->next), type_of_struct, member_name); \
        struct list_head *list = (head)->next; \
        list_del((head)->next); \
        INIT_LIST_HEAD(list); \
        (temp); \
        })

typedef struct
{
    struct list_head node;
    cbox_run_in_loop_func_t callback;
    void *arg;
} run_in_loop_func_queue_node_t;

struct cbox_delegator
{
    cbox_loop_t *loop;
    pthread_mutex_t mutex;
    cbox_fd_event_t *fd_event;
    struct list_head function_queue;
    int has_commit_run_req;
};

static void cbox_on_fd_event(uint32_t events, void *user);
static void cbox_commit_run_request(cbox_delegator_t *);
static void cbox_finish_run_request(cbox_delegator_t *de);

cbox_delegator_t *cbox_delegator_new(cbox_loop_t *loop)
{
    cbox_delegator_t * delegator = (cbox_delegator_t *)malloc(sizeof(cbox_delegator_t));
    if (delegator == NULL)
        return NULL;
    
    delegator->loop = loop;
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd <= 0 )
        goto error;

    delegator->fd_event = cbox_fd_event_new(fd, CBOX_EVENT_READ, cbox_on_fd_event, delegator);
    if (delegator->fd_event == NULL)
        goto error;

    delegator->has_commit_run_req = 0;

    if (cbox_loop_fd_event_add(loop, delegator->fd_event) < 0)
        goto error;

    INIT_LIST_HEAD(&delegator->function_queue);
    pthread_mutex_init(&delegator->mutex, NULL);

    return delegator;

error:
    CBOX_SAFETY_FREE(delegator);
    return NULL;
}
void cbox_delegator_delete(cbox_delegator_t *delegator)
{
    run_in_loop_func_queue_node_t *pos = NULL, *tmp = NULL;

    if (!delegator)
        return;

    if (delegator->fd_event) {
        cbox_loop_fd_event_remove(delegator->loop, delegator->fd_event);
        cbox_fd_event_delete(delegator->fd_event);
    }

    pthread_mutex_destroy(&delegator->mutex);

    list_for_each_entry_safe(pos, tmp, &delegator->function_queue, node) {
        list_del(&pos->node);
        CBOX_SAFETY_FREE(pos);
    }
}

void cbox_delegator_delegate(cbox_delegator_t *de, cbox_run_in_loop_func_t cb, void *user)
{
    if (!de)
        return;

    run_in_loop_func_queue_node_t *node = (run_in_loop_func_queue_node_t *)malloc(sizeof(run_in_loop_func_queue_node_t));
    if (!node)
        return;

    node->callback = cb;
    node->arg = user;

    {
        pthread_mutex_lock(&de->mutex);

        // push node to queue tail
        list_add_tail(&node->node, &de->function_queue);
        if (de->fd_event != NULL) {
            cbox_commit_run_request(de);
        }

        pthread_mutex_unlock(&de->mutex);
    }
}

void cbox_on_fd_event(uint32_t events, void *user)
{
    cbox_delegator_t *de = (cbox_delegator_t *)user;
    if (!user || events != CBOX_EVENT_READ)
        return;

    struct list_head tmp;
    {
        pthread_mutex_lock(&de->mutex);
        INIT_LIST_HEAD(&tmp);
        list_swap(&tmp, &de->function_queue);
        pthread_mutex_unlock(&de->mutex);
        cbox_finish_run_request(de);
    }

    while (!list_empty(&tmp)) {
        run_in_loop_func_queue_node_t *node = DQUEUE_POP_FRONT(&tmp, run_in_loop_func_queue_node_t, node);

        if (node->callback)
            node->callback(node->arg);

        CBOX_SAFETY_FREE(node);
    }
}

static void cbox_commit_run_request(cbox_delegator_t *de)
{
    if (!de->has_commit_run_req) {
        uint64_t one = 1;
        TEMP_FAILURE_RETRY(write(cbox_fd_event_fd(de->fd_event), &one, sizeof(one)));
        de->has_commit_run_req = 1;
    }
}

static void cbox_finish_run_request(cbox_delegator_t *de)
{
    uint64_t one = 1;
    read(cbox_fd_event_fd(de->fd_event), &one, sizeof(one));
    de->has_commit_run_req = 0;
}
