#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cbox/event/timer.h"
#include "cbox/event/loop.h"

static void print_usage_exit(char **argv)
{
    fprintf(stdout, "Usage: %s <miliseconds> <repeat>\n", argv[0]);
    exit(1);
}

static void on_timeout(void *user)
{
    static int cnt = 0;
    ++cnt;

    printf("timeout, repeat=%d\n", cnt);
    (void)user;
}

int main(int argc, char **argv)
{
    if (argc < 3)
        print_usage_exit(argv);

    cbox_loop_t *loop = cbox_loop_new();
    int miliseconds = atoi(argv[1]);
    int repeat = atoi(argv[2]);

    struct timespec ts;
    ts.tv_sec = miliseconds / 1000;
    ts.tv_nsec = (miliseconds % 1000) * 1000000;

    printf("press ctrl+c to exit:\n");

    cbox_timer_t *timer = cbox_timer_new(loop, &ts, repeat, on_timeout, loop);
    cbox_timer_enable(timer);
    cbox_loop_dispatch(loop, CBOX_RUN_MODE_FOREVER);

    cbox_timer_delete(timer);
    cbox_loop_delete(loop);
    return 0;
}
