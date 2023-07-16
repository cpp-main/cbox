#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include "base/macros.h"
#include "loop.h"
#include "delegator.h"
#include "fd_event.h"
#include "base/pbl.h"


#define CBOX_MAX_EVENTS (64)
#define CBOX_DEFAULT_TIMER_HEAP_CAPACITY (64)
#define CBOX_TOKEN_SIZE (17)

typedef uint64_t cbox_basic_timer_token_t;

struct cbox_basic_timer
{
    cbox_basic_timer_token_t token;
    uint64_t interval;
    uint64_t expired;
    uint64_t repeat;
    cbox_timeout_func_t handler;
    void *user_data;
};

struct cbox_loop
{
    int epoll_fd;
    int exit_flag;
    uint32_t cbox_basic_timer_count; //!< used for generating timer token
    struct cbox_basic_timer **timer_heap;
    int timer_heap_capacity;
    int timer_heap_size;
    cbox_delegator_t *delegator;
    cbox_basic_timer_t *exit_timer;
    int running;
    PblMap *fd_nodes; //!<key:fd, value: struct cbox_fd_event_shared_data *
};

static void on_tick(cbox_loop_t *);
static inline uint64_t top_expired(cbox_loop_t *);
static void min_heap_percolate_down(cbox_loop_t *, int);
static inline cbox_basic_timer_token_t generate_timer_token(cbox_loop_t *, cbox_basic_timer_t *);

int cbox_fd_node_add(cbox_loop_t *loop, int fd, void *value);
int cbox_fd_node_del(cbox_loop_t *loop, int fd);
void *cbox_fd_node_search(cbox_loop_t *loop, int fd);

extern void cbox_fd_event_on_event(uint32_t events, void *ptr);

cbox_loop_t *cbox_loop_new()
{
    int i = 0;
    struct cbox_loop *loop = (struct cbox_loop *)calloc(1, sizeof(struct cbox_loop));
    if (loop == NULL) goto error;

    loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    loop->timer_heap_capacity = CBOX_DEFAULT_TIMER_HEAP_CAPACITY;
    loop->timer_heap_size = 0;
    loop->cbox_basic_timer_count = 1;
    loop->timer_heap = (struct cbox_basic_timer **)calloc(1, sizeof(struct cbox_basic_timer *) * loop->timer_heap_capacity);

    for (i = 0; i < loop->timer_heap_capacity; ++i)
        loop->timer_heap[i] = NULL;

    loop->fd_nodes = pblMapNewTreeMap();
    if (loop->fd_nodes == NULL)
        goto error;

    loop->exit_flag = 0;
    loop->delegator = cbox_delegator_new(loop);
    if (loop->delegator == NULL)
        goto error;

    loop->exit_timer = NULL;
    loop->running = 0;

    return loop;
error:
    CBOX_SAFETY_FREE(loop);
    return NULL;
}

void cbox_loop_delete(cbox_loop_t *loop)
{
    if (loop == NULL) return;

    CBOX_SAFETY_FUNC(cbox_delegator_delete, loop->delegator);

    CBOX_SAFETY_FUNC(pblMapClear, loop->fd_nodes);
    CBOX_SAFETY_FUNC(pblMapFree, loop->fd_nodes);

    if (loop->exit_timer) {
        cbox_basic_timer_disable(loop, loop->exit_timer);
        cbox_basic_timer_delete(loop->exit_timer);
        loop->exit_timer = NULL;
    }

    if (loop->epoll_fd != -1) {
        close(loop->epoll_fd);
        loop->epoll_fd = -1;
    }

    CBOX_SAFETY_FREE(loop->timer_heap);
    CBOX_SAFETY_FREE(loop);
}

void cbox_loop_dispatch(cbox_loop_t * loop, uint32_t mode)
{
    int i = 0;
    if (loop == NULL)
        return;

    loop->exit_flag = (mode == CBOX_RUN_MODE_ONCE) ? 1 : 0;
    loop->running = 1;

    do {
        struct epoll_event events[CBOX_MAX_EVENTS];
        int num_fds = epoll_wait(loop->epoll_fd, events, sizeof(events) / sizeof(events[0]), top_expired(loop));

        on_tick(loop);

        if (num_fds <= 0) continue;

        for (i = 0; i < num_fds; ++i)
            cbox_fd_event_on_event(events[i].events, events[i].data.ptr);

    } while (!loop->exit_flag);

    loop->running = 0;
}

void cbox_loop_delegate(cbox_loop_t *loop, cbox_run_in_loop_func_t cb, void *user)
{
    if (loop)
        cbox_delegator_delegate(loop->delegator, cb, user);
}

void cbox_loop_exit(cbox_loop_t *loop)
{
    if (!loop)
        return;

    if (loop->running)
        loop->exit_flag = 1;
}

static void cbox_loop_exit_after_timeout(void *user)
{
    cbox_loop_t *loop = (cbox_loop_t *)user;
    if (!loop)
        return;

    cbox_loop_exit(loop);

    if (loop->exit_timer) {
        cbox_basic_timer_disable(loop, loop->exit_timer);
        cbox_basic_timer_delete(loop->exit_timer);
        loop->exit_timer = NULL;
    }
}

void cbox_loop_exit_after(cbox_loop_t *loop, uint64_t miliseconds)
{
    if (loop == NULL)
        return;

    if (!loop->running)
        return;

    if (loop->exit_timer != NULL) {
        cbox_basic_timer_disable(loop, loop->exit_timer);
        cbox_basic_timer_delete(loop->exit_timer);
        loop->exit_timer = NULL;
    }

    cbox_basic_timer_t *timer = cbox_basic_timer_new(miliseconds, 1, cbox_loop_exit_after_timeout, loop);
    cbox_basic_timer_enable(loop, timer);
}

cbox_basic_timer_t *cbox_basic_timer_new(uint64_t ms, int repeat, cbox_timeout_func_t cb, void *user)
{
    cbox_basic_timer_t *timer = (cbox_basic_timer_t *)malloc(sizeof(cbox_basic_timer_t));
    if (timer == NULL) return NULL;

    timer->handler = cb;
    timer->repeat = repeat;
    timer->user_data = user;
    timer->interval = ms;
    timer->expired = CBOX_CURRENT_CLOCK_MILLISECONDS() + timer->interval;
    timer->token = 0;

    return timer;
}

void cbox_basic_timer_delete(cbox_basic_timer_t *timer)
{
    if (timer == NULL) return;

    timer->token = 0;
    CBOX_SAFETY_FREE(timer);
}

int cbox_basic_timer_enable(cbox_loop_t *loop, cbox_basic_timer_t *timer)
{
    if (loop == NULL || timer == NULL)
        return -1;

    timer->token = generate_timer_token(loop, timer);
    if (loop->timer_heap_size >= loop->timer_heap_capacity) {
        cbox_basic_timer_t **tmp = (struct cbox_basic_timer **)realloc(loop->timer_heap, (sizeof(struct cbox_basic_timer *) * (++loop->timer_heap_capacity)));
        if (!tmp)
            return -1;

        loop->timer_heap = tmp;
        loop->timer_heap[loop->timer_heap_capacity - 1] = NULL;
    }

    int hole = loop->timer_heap_size ++;
    int parent = 0;
    for (; hole > 0; hole = parent) {
        parent = (hole - 1) / 2;
        if (loop->timer_heap[parent]->expired > timer->expired)
            loop->timer_heap[hole] = loop->timer_heap[parent];
        else
            break;
    }

    loop->timer_heap[hole] = timer;
    return 0;
}

int cbox_basic_timer_disable(cbox_loop_t *loop, cbox_basic_timer_t *timer)
{
    if (loop == NULL || timer == NULL)
        return -1;

    int i = 0, found = 0, result = -1;
    for (i = 0; i < loop->timer_heap_size; ++i) {
        if (loop->timer_heap[i]->token == timer->token) {
            if (i != loop->timer_heap_size - 1)
                loop->timer_heap[i] = loop->timer_heap[loop->timer_heap_size - 1];

            loop->timer_heap_size --;
            found = 1;
            break;
        }
    }

    if (found) {
        for (i = loop->timer_heap_size / 2 - 1; i >= 0; i--)
            min_heap_percolate_down(loop, i);

        result = 0;
    }

    return result;
}


static inline uint64_t top_expired(cbox_loop_t *loop)
{
    uint64_t expired = 0;
    struct cbox_basic_timer *top_element = loop->timer_heap[0];
    if (top_element == NULL) return 0;
    expired = top_element->expired - CBOX_CURRENT_CLOCK_MILLISECONDS();

    return expired;
}

static void min_heap_percolate_down(cbox_loop_t *loop, int hole)
{
    struct cbox_basic_timer *tmp = loop->timer_heap[hole];
    int child = 0;
    for (; hole * 2 + 1 < loop->timer_heap_size; hole = child) {
        child = hole * 2 + 1;
        if (child < loop->timer_heap_size - 1 && loop->timer_heap[child]->expired > loop->timer_heap[child + 1]->expired)
            child ++;
        if (tmp->expired > loop->timer_heap[child]->expired)
            loop->timer_heap[hole] = loop->timer_heap[child];
        else
            break;
    }

    loop->timer_heap[hole] = tmp;
}

static void on_tick(cbox_loop_t *loop)
{
    uint64_t now = CBOX_CURRENT_CLOCK_MILLISECONDS();
    while (loop->timer_heap_size > 0) {
        struct cbox_basic_timer *top_element = loop->timer_heap[0];
        if (top_element->expired > now) break;

        cbox_timeout_func_t handler = top_element->handler;
        void *user = top_element->user_data;
        loop->timer_heap[0] = loop->timer_heap[loop->timer_heap_size - 1];
        min_heap_percolate_down(loop, 0);
        if (top_element->repeat == 1) {
            loop->timer_heap[loop->timer_heap_size --] = NULL;
        } else {
            top_element->expired += top_element->interval;
            if (top_element->repeat != 0) top_element->repeat --;

            // insert to last element again
            loop->timer_heap[loop->timer_heap_size - 1] = top_element;

            {
                int hole = loop->timer_heap_size - 1, parent = 0;
                for (; hole > 0; hole = parent) {
                    parent = (hole - 1) / 2;
                    if (loop->timer_heap[parent]->expired <= top_element->expired)
                        break;

                    loop->timer_heap[hole] = loop->timer_heap[parent];
                }

                loop->timer_heap[hole] = top_element;
            }
        }

        if (handler)
            handler(user);
    }
}

static cbox_basic_timer_token_t generate_timer_token(cbox_loop_t *loop, cbox_basic_timer_t *timer)
{
    if (loop == NULL || timer == NULL)
        return 0;

    return ++loop->cbox_basic_timer_count;
}

int cbox_fd_node_add(cbox_loop_t *loop, int fd, void *value)
{
    int ret = pblMapAdd(loop->fd_nodes, &fd, sizeof(fd), &value, sizeof(void *));
    if (ret < 0)
        return -1;

    return 0;
}

int cbox_fd_node_del(cbox_loop_t *loop, int fd)
{
    size_t val_len = 0;
    void *ret = pblMapRemove(loop->fd_nodes, &fd, sizeof(fd), &val_len);

    if (ret == NULL || ret == (void *)-1) //!< could not found key : fd
        return -1;

    return 0; 
}

void *cbox_fd_node_search(cbox_loop_t *loop, int fd)
{
    void *value = NULL;
    size_t ret_len = 0;
    value = pblMapGet(loop->fd_nodes, &fd, sizeof(fd), &ret_len);
    void **pvalue = &value;

    (void)ret_len;
    return *pvalue;
}

int cbox_loop_epoll_fd(cbox_loop_t *loop)
{
    if (loop)
        return loop->epoll_fd;

    return -1;
}
