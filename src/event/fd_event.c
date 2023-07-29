#include <stdio.h>
#include "base/pbl.h"
#include <sys/epoll.h>
#include <string.h>
#include <stdlib.h>
#include "fd_event.h"
#include "base/macros.h"

struct cbox_fd_event_shared_data
{
    int fd;
    int ref;

    struct epoll_event ev;

    // A fd may be bound multiple same events
    PblList *read_events;  // struct cbox_fd_event
    PblList *write_events;  // struct cbox_fd_event
    PblList *exception_events;  // struct cbox_fd_event
};

struct cbox_fd_event
{
    int fd;
    uint32_t events;
    void *user;
    cbox_loop_t *loop;
    cbox_fd_event_func_t handler;
    int enabled;
    int once;
    struct cbox_fd_event_shared_data *shared_data;
};

static inline uint32_t cbox_events_to_epoll_events(uint32_t /*cbox_events*/);
static inline uint32_t epoll_events_to_cbox_events(uint32_t /*cbox_events*/);
static void cbox_fd_event_unref_shared_data(cbox_fd_event_t *event);
static void cbox_fd_event_reload(cbox_fd_event_t *event);
static void trigger_event_callback(cbox_fd_event_t *obj, uint32_t event);


// from loop
extern int cbox_loop_epoll_fd(cbox_loop_t *loop);
extern int cbox_fd_node_add(cbox_loop_t *loop, int fd, void *value);
extern int cbox_fd_node_del(cbox_loop_t *loop, int fd);
extern void *cbox_fd_node_search(cbox_loop_t *loop, int fd);


cbox_fd_event_t *cbox_fd_event_new(cbox_loop_t *loop, int fd, uint32_t event_type, cbox_fd_event_func_t cb, int run_mode, void *user)
{
    cbox_fd_event_t *event = (cbox_fd_event_t *)malloc(sizeof(cbox_fd_event_t));
    if (event == NULL)
        return NULL;

    event->fd = fd;
    event->events = cbox_events_to_epoll_events(event_type);
    event->user = user;
    event->handler = cb;
    event->loop = loop;
    event->enabled = 0;
    event->once = (run_mode == CBOX_RUN_MODE_ONCE ? 1 : 0);
    event->shared_data = NULL;

    cbox_fd_event_unref_shared_data(event);
    // before create, check if the fd already bound with events
    struct cbox_fd_event_shared_data *d = NULL;
    d = cbox_fd_node_search(loop, fd);

    if (d == NULL) {
        d = (struct cbox_fd_event_shared_data *)malloc(sizeof(struct cbox_fd_event_shared_data));
        if (d == NULL)
            goto error;

        memset(&d->ev, 0, sizeof(d->ev));
        d->ref = 0;
        d->fd = fd;
        d->ev.data.ptr = d;

        d->read_events = pblListNewArrayList();
        d->write_events = pblListNewArrayList();
        d->exception_events = pblListNewArrayList();
        if (d->read_events == NULL || d->write_events == NULL || d->exception_events == NULL)
            goto error;

        cbox_fd_node_add(event->loop, fd, d);
    }

    event->shared_data = d;

    ++ d->ref;
    return event;
error:
    CBOX_SAFETY_FREE(event);
    return NULL;
}

void cbox_fd_event_delete(cbox_fd_event_t *event)
{
    cbox_fd_event_disable(event);
    cbox_fd_event_unref_shared_data(event);
    CBOX_SAFETY_FREE(event);
}

int cbox_fd_event_enable(cbox_fd_event_t *event)
{
    if (event == NULL)
        return -1;

    if (event->shared_data == NULL)
        return -1;

    if (event->enabled)
        return 0;

    if (event->events & EPOLLIN) {
        if (pblListAdd(event->shared_data->read_events, event) < 0)
            return -1;
    }

    if (event->events & EPOLLOUT) {
        if (pblListAdd(event->shared_data->write_events, event) < 0)
            return -1;
    }

    if ((event->events & EPOLLHUP) || (event->events & EPOLLERR)) {
        if (pblListAdd(event->shared_data->exception_events, event) < 0)
            return -1;
    }

    cbox_fd_event_reload(event);
    event->enabled = 1;

    return 0;
}

int cbox_fd_event_disable(cbox_fd_event_t *event)
{
    if (event == NULL)
        return -1;

    if (event->shared_data == NULL || !event->enabled)
        return 0;

    if (event->events & EPOLLIN) {
        pblListRemoveElement(event->shared_data->read_events, event);
    }

    if (event->events & EPOLLOUT) {
        pblListRemoveElement(event->shared_data->write_events, event);
    }

    if ((event->events & EPOLLHUP) || (event->events & EPOLLERR)) {
        pblListRemoveElement(event->shared_data->exception_events, event);
    }

    cbox_fd_event_reload(event);
    event->enabled = 0;

    return 0;
}

int cbox_fd_event_enabled(cbox_fd_event_t *event)
{
    if (event == NULL)
        return 0;

    return event->enabled;
}

int cbox_fd_event_modify(cbox_fd_event_t *obj, uint32_t new_events)
{
    if (obj == NULL)
        return -1;

    int before_enabled = obj->enabled;
    obj->events = cbox_events_to_epoll_events(new_events);

    if (before_enabled)
        cbox_fd_event_disable(obj);

    if (before_enabled)
        cbox_fd_event_enable(obj);

    return 0;
}

int cbox_fd_event_fd(cbox_fd_event_t *event)
{
    if (NULL == event) return -1;

    return event->fd;
}


uint32_t cbox_fd_event_events(cbox_fd_event_t *event)
{
    return epoll_events_to_cbox_events(event->events);
}

void cbox_fd_event_on_event(uint32_t events, void *ptr)
{
    struct cbox_fd_event_shared_data *d = (struct cbox_fd_event_shared_data *)ptr;
    int i = 0;

    if (!d)
        return;

    if (events & EPOLLIN) {
        for (i = 0; i < pblListSize(d->read_events); ++i) {
            cbox_fd_event_t *element = (cbox_fd_event_t *)pblListGet(d->read_events, i);
            trigger_event_callback(element, CBOX_EVENT_READ);
        }
    }

    if (events & EPOLLOUT) {
        for (i = 0; i < pblListSize(d->write_events); ++i) {
            cbox_fd_event_t *element = (cbox_fd_event_t *)pblListGet(d->write_events, i);
            trigger_event_callback(element, CBOX_EVENT_WRITE);
        }
    }

    if ((events & EPOLLERR) || (events & EPOLLHUP)) {
        for (i = 0; i < pblListSize(d->exception_events); ++i) {
            cbox_fd_event_t *element = (cbox_fd_event_t *)pblListGet(d->exception_events, i);
            trigger_event_callback(element, CBOX_EVENT_EXCEPTION);
        }
    }
}

static inline uint32_t cbox_events_to_epoll_events(uint32_t cbox_events)
{
    uint32_t events = 0;
    if (cbox_events & CBOX_EVENT_READ)
        events |= EPOLLIN;

    if (cbox_events & CBOX_EVENT_WRITE)
        events |= EPOLLOUT;

    if (cbox_events & CBOX_EVENT_EXCEPTION)
        events |= (EPOLLERR | EPOLLHUP);

    return events;
}

static inline uint32_t epoll_events_to_cbox_events(uint32_t epoll_events)
{
    uint32_t events = 0;
    if (epoll_events & EPOLLIN)
        events |= CBOX_EVENT_READ;

    if (epoll_events & EPOLLOUT)
        events |= CBOX_EVENT_WRITE;

    if (epoll_events & EPOLLERR || epoll_events & EPOLLHUP)
        events |= CBOX_EVENT_EXCEPTION;

    return events;
}

void cbox_fd_event_unref_shared_data(cbox_fd_event_t *event)
{
    if (!event) return;

    if (event->shared_data != NULL) {
        --event->shared_data->ref;
        if (event->shared_data->ref <= 0) {
            cbox_fd_node_del(event->loop, event->fd);
            CBOX_SAFETY_FUNC(pblListFree, event->shared_data->read_events);
            CBOX_SAFETY_FUNC(pblListFree, event->shared_data->write_events);
            CBOX_SAFETY_FUNC(pblListFree, event->shared_data->exception_events);
            CBOX_SAFETY_FREE(event->shared_data);
            event->fd = -1;
        }
    }
}

void cbox_fd_event_reload(cbox_fd_event_t *event)
{
    uint32_t old_events = event->shared_data->ev.events;
    uint32_t new_events = 0;

    if (!pblListIsEmpty(event->shared_data->read_events))
        new_events |= EPOLLIN;

    if (!pblListIsEmpty(event->shared_data->write_events))
        new_events |= EPOLLOUT;

    if (!pblListIsEmpty(event->shared_data->exception_events))
        new_events |= (EPOLLHUP | EPOLLERR);

    event->shared_data->ev.events = new_events;

    if (old_events == 0) {
        if (new_events != 0)
            epoll_ctl(cbox_loop_epoll_fd(event->loop), EPOLL_CTL_ADD, event->fd, &event->shared_data->ev);
    } else {
        if (new_events != 0) {
            epoll_ctl(cbox_loop_epoll_fd(event->loop), EPOLL_CTL_MOD, event->fd, &event->shared_data->ev);
        } else {
            epoll_ctl(cbox_loop_epoll_fd(event->loop), EPOLL_CTL_DEL, event->fd, NULL);
        }
    }
}

void trigger_event_callback(cbox_fd_event_t *obj, uint32_t event)
{
    if (obj->once)
        cbox_fd_event_disable(obj);

    if (obj->handler)
        obj->handler(obj->fd, event, obj->user);
}
