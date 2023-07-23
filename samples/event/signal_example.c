#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include "cbox/event/loop.h"
#include "cbox/base/macros.h"
#include "cbox/event/signal_event.h"


void new_signal_callback(int signo, void *user)
{
    printf("Got interupt signal:%d\nb", signo);
    (void)user;
}

int main(int argc, char *argv[])
{
    cbox_loop_t *loop = cbox_loop_new();
    int sigs[] = { SIGINT };

    printf("press ctrl+c to capture signal, press ctrl+z to exit\n");
    cbox_signal_event_t *psignal = cbox_signal_event_new(loop, sigs, CBOX_ARRAY_SIZE(sigs), new_signal_callback, CBOX_RUN_MODE_FOREVER, loop);
    cbox_loop_dispatch(loop, CBOX_RUN_MODE_FOREVER);
    cbox_signal_event_delete(psignal);
    cbox_loop_delete(loop);

    (void)argc;
    (void)argv;
    return 0;
}
