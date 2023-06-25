#include <unistd.h>
#include <stdlib.h>
#include <sys/timerfd.h>
#include <cbox/base/macros.h>
#include "timer.h"

struct cbox_timer
{
    cbox_loop_t *loop;
    cbox_basic_timer_t *basic_timer;
    cbox_fd_event_t *fd_timer;
    int repeat;
    cbox_timeout_func_t callback;
    void *user;
    uint8_t enabled;
    struct timespec interval;
};

static int cbox_timer_create_timerfd();
static void on_cbox_timerfd_timeout(uint32_t, void *);

cbox_timer_t *cbox_timer_new(cbox_loop_t *loop, struct timespec *ts, int repeat, cbox_timeout_func_t cb, void *user)
{
    int use_basic = 0;
    cbox_timer_t *timer = (cbox_timer_t *)malloc(sizeof(cbox_timer_t));
    if (timer == NULL)
        return NULL;

    timer->loop = loop;
    timer->basic_timer = NULL;
    timer->fd_timer = NULL;
    timer->repeat = repeat;
    timer->user = user;
    timer->callback = cb;

    uint64_t ns = ts->tv_sec * 1000000000 + ts->tv_nsec;
    uint64_t ms = ns / 1000000;

    use_basic = (ms > 1) && (ns % 1000000 == 0);
    if (use_basic) {
        timer->basic_timer = cbox_basic_timer_new(ms, repeat, timer->callback, timer->user);
        if (timer->basic_timer == NULL)
            goto error;
    } else {
        int fd = cbox_timer_create_timerfd();
        if (fd < 0)
            goto error;

        timer->fd_timer = cbox_fd_event_new(fd, CBOX_EVENT_READ, on_cbox_timerfd_timeout, timer);
        if (timer->fd_timer == NULL)
            goto error;
    }

    timer->enabled = 0;
    timer->interval.tv_sec = ts->tv_sec;
    timer->interval.tv_nsec = ts->tv_nsec;
    return timer;

error:
    CBOX_SAFETY_FREE(timer->basic_timer);
    CBOX_SAFETY_FREE(timer->fd_timer);
    CBOX_SAFETY_FREE(timer);
    return NULL;
}

void cbox_timer_delete(cbox_timer_t *timer)
{
    if (!timer)
        return;

    if (timer->enabled) {
        if (timer->basic_timer)
            cbox_basic_timer_disable(timer->loop, timer->basic_timer);
        if (timer->fd_timer) {
            cbox_loop_fd_event_remove(timer->loop, timer->fd_timer);
            close(cbox_fd_event_fd(timer->fd_timer));
        }
        timer->enabled = 0;
    }

    CBOX_SAFETY_FUNC(cbox_basic_timer_delete, timer->basic_timer);
    CBOX_SAFETY_FUNC(cbox_fd_event_delete, timer->fd_timer);
    CBOX_SAFETY_FREE(timer);
}

int cbox_timer_enable(cbox_timer_t *timer)
{
    int ret = -1;

    if (!timer)
        return -1;

    if (timer->enabled)
        return 0;

    if (timer->fd_timer) {
        struct itimerspec ts = {
            .it_value.tv_sec = timer->interval.tv_sec,
            .it_value.tv_nsec = timer->interval.tv_nsec,
            .it_interval.tv_sec = timer->interval.tv_sec,
            .it_interval.tv_nsec = timer->interval.tv_nsec
        };

        if (timerfd_settime(cbox_fd_event_fd(timer->fd_timer), TFD_TIMER_CANCEL_ON_SET, &ts, NULL) < 0)
            return ret;
    }

    ret = timer->basic_timer ? cbox_basic_timer_enable(timer->loop, timer->basic_timer) : cbox_loop_fd_event_add(timer->loop, timer->fd_timer);

    timer->enabled = (ret == 0) ? 1 : 0;

    return ret;
}

int cbox_timer_enabled(cbox_timer_t *timer)
{
    int enabled = 0;
    if (timer)
        enabled = timer->enabled;

    return enabled;
}

int cbox_timer_disable(cbox_timer_t *timer)
{
    int ret = -1;

    if (!timer)
        return -1;

    if (!timer->enabled)
        return 0;

    if (timer->fd_timer) {
        struct itimerspec ts = { 0 };
        if (timerfd_settime(cbox_fd_event_fd(timer->fd_timer), TFD_TIMER_CANCEL_ON_SET, &ts, NULL) < 0)
            return ret;
    }

    ret = timer->basic_timer ? cbox_basic_timer_disable(timer->loop, timer->basic_timer) : cbox_loop_fd_event_remove(timer->loop, timer->fd_timer);

    timer->enabled = (ret == 0) ? 0 : 1;

    return ret;
}

static int cbox_timer_create_timerfd()
{
    return timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
}

static void on_cbox_timerfd_timeout(uint32_t event, void *user)
{
    cbox_timer_t *timer = (cbox_timer_t *)user;
    if (event & CBOX_EVENT_READ) {
        uint64_t exp = 0;
        -- timer->repeat;
        int len = read(cbox_fd_event_fd(timer->fd_timer), &exp, sizeof(exp));
        if (len != sizeof(exp))
            return;

        if (timer && timer->callback)
            timer->callback(timer->user);

        if (timer->repeat == 0)
            cbox_timer_disable(timer);

    }
}
