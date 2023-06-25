#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <cbox/base/macros.h>
#include <pthread.h>
#include "loop.h"
#include "delegator.h"


#define CBOX_MAX_EVENTS (64)
#define CBOX_DEFAULT_TIMER_HEAP_CAPACITY (64)
#define CBOX_TOKEN_SIZE (17)

typedef char * cbox_basic_timer_token_t;

struct cbox_fd_event
{
    int fd;
    uint32_t events;
    void *user;
    cbox_fd_event_func_t handler;
    struct cbox_fd_event *next;
};

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
    uint32_t cbox_basic_timer_count; // used for generating timer token
    struct cbox_fd_event *event_list;
    struct cbox_basic_timer **timer_heap;
    int timer_heap_capacity;
    int timer_heap_size;
    cbox_delegator_t *delegator;
};

static void on_tick(cbox_loop_t *);
static inline uint64_t top_expired(cbox_loop_t *);
static void min_heap_percolate_down(cbox_loop_t *, int);
static cbox_basic_timer_token_t generate_timer_token(cbox_loop_t *, cbox_basic_timer_t *);

static inline uint32_t cbox_events_to_epoll_events(uint32_t /*cbox_events*/);
static inline uint32_t epoll_events_to_cbox_events(uint32_t /*epoll_events*/);

cbox_loop_t *cbox_loop_new()
{
    int i = 0;
    struct cbox_fd_event * head = NULL;
    struct cbox_loop *loop = (struct cbox_loop *)calloc(1, sizeof(struct cbox_loop));
    if (loop == NULL) goto error;
    head = (struct cbox_fd_event *)calloc(1, sizeof(struct cbox_fd_event));
    if (head == NULL) goto error;
    loop->event_list = head;

    loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    loop->event_list->next = NULL;
    loop->timer_heap_capacity = CBOX_DEFAULT_TIMER_HEAP_CAPACITY;
    loop->timer_heap_size = 0;
    loop->cbox_basic_timer_count = 1;
    loop->timer_heap = (struct cbox_basic_timer **)calloc(1, sizeof(struct cbox_basic_timer *) * loop->timer_heap_capacity);

    for (i = 0; i < loop->timer_heap_capacity; ++i)
        loop->timer_heap[i] = NULL;

    loop->exit_flag = 0;
    loop->delegator = cbox_delegator_new(loop);
    if (loop->delegator == NULL)
        goto error;

    return loop;

error:
    CBOX_SAFETY_FREE(loop);
    CBOX_SAFETY_FREE(head);
    return NULL;
}

void cbox_loop_delete(cbox_loop_t *loop)
{
    if (loop == NULL) return;

    if (loop->delegator)
        cbox_delegator_delete(loop->delegator);

    struct cbox_fd_event *node = loop->event_list;

    while (node->next != NULL) {
        node = node->next;
        epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, node->fd, NULL);
        if (node) free(node);
    }

    close(loop->epoll_fd);

    CBOX_SAFETY_FREE(loop->event_list);
    CBOX_SAFETY_FREE(loop->timer_heap);
    CBOX_SAFETY_FREE(loop);
}

void cbox_loop_dispatch(cbox_loop_t * loop, uint32_t mode)
{
    int i = 0;
    if (loop == NULL)
        return;

    loop->exit_flag = (mode == CBOX_LOOP_RUN_MODE_ONCE) ? 1 : 0;

    do {
        struct epoll_event events[CBOX_MAX_EVENTS];
        int num_fds = epoll_wait(loop->epoll_fd, events, sizeof(events) / sizeof(events[0]), top_expired(loop));

        on_tick(loop);

        if (num_fds <= 0) continue;

        for (i = 0; i < num_fds; ++i) {
            struct cbox_fd_event *node = loop->event_list, *pos = NULL;
            while (node->next != NULL) {
                node = node->next;
                if (node->fd == events[i].data.fd) {
                    pos = node;
                    break;
                }
            }

            if (pos && pos->handler) (*(pos->handler))(epoll_events_to_cbox_events((uint32_t)events[i].events), pos->user);
        }
    } while (!loop->exit_flag);

}

void cbox_loop_delegate(cbox_loop_t *loop, cbox_run_in_loop_func_t cb, void *user)
{
    if (loop)
        cbox_delegator_delegate(loop->delegator, cb, user);
}

void cbox_loop_exit(cbox_loop_t *loop)
{
    if (loop)
        loop->exit_flag = 1;
}

cbox_fd_event_t *cbox_fd_event_new(int fd, uint32_t event_type, cbox_fd_event_func_t cb, void *user)
{
    cbox_fd_event_t *event = (cbox_fd_event_t *)malloc(sizeof(cbox_fd_event_t));
    if (event == NULL)
        return NULL;

    event->fd = fd;
    event->events = cbox_events_to_epoll_events(event_type);
    event->user = user;
    event->handler = cb;
    event->next = NULL;

    return event;
}

void cbox_fd_event_delete(cbox_fd_event_t *event)
{
    CBOX_SAFETY_FREE(event);
}

int cbox_loop_fd_event_add(cbox_loop_t *loop, cbox_fd_event_t *event)
{
    if (loop == NULL || event == NULL) return -1;

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = event->fd;
    ev.events = event->events;
    int ret = epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, event->fd, &ev);
    if (ret == 0) {
        cbox_fd_event_t *head = loop->event_list;
        event->next = head->next;
        head->next = event;
    }

    if (ret == -1 && errno == EEXIST)
        ret = 0;

    return ret;
}

int cbox_loop_fd_event_mod(cbox_loop_t *loop, cbox_fd_event_t *event, uint32_t event_type)
{
    if (loop == NULL || event == NULL) return -1;

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    event->events = cbox_events_to_epoll_events(event_type);

    ev.data.fd = event->fd;
    ev.events = event->events;
    return epoll_ctl(loop->epoll_fd, EPOLL_CTL_MOD, event->fd, &ev);
}

int cbox_loop_fd_event_remove(cbox_loop_t *loop, cbox_fd_event_t *event)
{
    if (loop == NULL || event == NULL) return -1;

    int ret = -1;
    ret = epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, event->fd, NULL);

    cbox_fd_event_t *pos = loop->event_list;
    cbox_fd_event_t *prev = NULL;
    while (pos->next != NULL) {
        prev = pos;
        pos = pos->next;
        if (pos == event) {
            prev->next = pos->next;
            break;
        }
    }
    return ret;
}

cbox_fd_event_t *cbox_loop_fd_event(cbox_loop_t *loop, int fd)
{
    if (loop == NULL)
        return NULL;

    cbox_fd_event_t *pos = loop->event_list;
    cbox_fd_event_t *prev = NULL;
    cbox_fd_event_t *result = NULL;
    while (pos->next != NULL) {
        prev = pos;
        pos = pos->next;
        if (pos->fd == fd) {
            prev->next = pos->next;
            result = pos;
            break;
        }
    }

    return result;
}

int cbox_fd_event_fd(cbox_fd_event_t *event)
{
    if (NULL == event) return -1;

    return event->fd;
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
    timer->token = NULL;

    return timer;
}

void cbox_basic_timer_delete(cbox_basic_timer_t *timer)
{
    if (timer == NULL) return;

    CBOX_SAFETY_FREE(timer->token);
    CBOX_SAFETY_FREE(timer);
}

int cbox_basic_timer_enable(cbox_loop_t *loop, cbox_basic_timer_t *timer)
{
    if (loop == NULL || timer == NULL)
        return -1;

    CBOX_SAFETY_FREE(timer->token);
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
        if (memcmp(loop->timer_heap[i]->token, timer->token, CBOX_TOKEN_SIZE) == 0) {
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

static inline uint32_t cbox_events_to_epoll_events(uint32_t cbox_events)
{
    uint32_t events = 0;
    if (cbox_events & CBOX_EVENT_READ)
        events |= EPOLLIN;

    if (cbox_events & CBOX_EVENT_WRITE)
        events |= EPOLLOUT;

    if (cbox_events & CBOX_EVENT_ERROR)
        events |= EPOLLERR;

    return events;
}

static inline uint32_t epoll_events_to_cbox_events(uint32_t epoll_events)
{
    uint32_t events = 0;
    if (epoll_events & EPOLLIN)
        events |= CBOX_EVENT_READ;

    if (epoll_events & EPOLLOUT)
        events |= CBOX_EVENT_WRITE;

    if (epoll_events & EPOLLERR)
        events |= CBOX_EVENT_ERROR;

    return events;
}

static cbox_basic_timer_token_t generate_timer_token(cbox_loop_t *loop, cbox_basic_timer_t *timer)
{
    if (loop == NULL || timer == NULL)
        return NULL;
    char *token = (char *)calloc(1, CBOX_TOKEN_SIZE);
    if (token == NULL)
        return NULL;

    ++loop->cbox_basic_timer_count;
    uintptr_t address = (uintptr_t)timer;
    sprintf(token, "%08X%08X", loop->cbox_basic_timer_count, (uint32_t)address);
    return token;
}
