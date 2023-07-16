#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <unistd.h>
#include "signal_event.h"
#include "fd_event.h"
#include "base/macros.h"

struct cbox_signal_event
{
    cbox_loop_t *loop;
    cbox_fd_event_t *fd_event;
    cbox_signal_function_t cb;
    void *user;
};

static void cbox_on_signal_event(int fd, uint32_t events, void *user);

cbox_signal_event_t *cbox_signal_event_new(cbox_loop_t *loop, const int *signo_array, size_t array_len, cbox_signal_function_t cb, int run_mode, void *user)
{
    sigset_t mask;
    int sfd = -1;
    size_t i = 0;

    cbox_signal_event_t *se = (cbox_signal_event_t*)malloc(sizeof(cbox_signal_event_t));
    if (se == NULL)
        return NULL;

    se->loop = loop;
    se->user = user;
    se->cb = cb;
    
    sigemptyset(&mask);
    for (i = 0; i < array_len; ++i)
        sigaddset(&mask, signo_array[i]);

    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
        goto error;

    sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sfd == -1)
        goto error;

    se->fd_event = cbox_fd_event_new(loop, sfd, CBOX_EVENT_READ, cbox_on_signal_event, run_mode, se);
    cbox_fd_event_enable(se->fd_event);
    return se;
error:
    CBOX_SAFETY_FREE(se);
    return NULL;
}

void cbox_signal_event_delete(cbox_signal_event_t *obj)
{
    if (obj == NULL)
        return;

    cbox_fd_event_disable(obj->fd_event);
    CBOX_SAFETY_FUNC(cbox_fd_event_delete, obj->fd_event);
    CBOX_SAFETY_FREE(obj);
}

void cbox_on_signal_event(int fd, uint32_t events, void *user)
{
    cbox_signal_event_t *se = (cbox_signal_event_t *)user;
    struct signalfd_siginfo fdsi;
    ssize_t len = -1;

    if (se == NULL)
        return;

    if (events & CBOX_EVENT_READ) {
        len = read(fd, &fdsi, sizeof(fdsi));
        if (len != sizeof(fdsi))
            return;

        if (se->cb)
            se->cb(fdsi.ssi_signo, se->user);
    }
}
