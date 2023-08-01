#include <libgen.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include "cbox/event/loop.h"
#include "cbox/base/log.h"
#include "cbox/base/utils.h"

cbox_loop_t *loop = NULL;
pthread_t t1, t2;

const int loop_times = 1000000;

static void on_loop_exit(void *user)
{
    cbox_loop_t *loop = (cbox_loop_t*)user;
    LOGD("on_loop_exit");
    cbox_loop_exit_after(loop, 10);
}

void do_in_loop(void *arg)
{
    static int count = 0;
    ++count;

    assert(cbox_utils_running_in_main());
    if (count == loop_times)
        LOGD("do_in_loop: all received");

    (void)arg;
}


void *thread_func1(void *arg)
{
    assert(!cbox_utils_running_in_main());
    LOGD("thread 1 start");
    for (int i = 0; i < loop_times; ++i) {
        cbox_loop_delegate(loop, do_in_loop, NULL);
    }

    LOGD("thread 1 exit");
    cbox_loop_delegate(loop, on_loop_exit, loop);

    (void)arg;
    return NULL;
}

void say_hello(void *arg)
{
    static int count = 0;
    ++count;

    if (count == loop_times)
        LOGD("say_hello, all received");

    (void)arg;
}

void *thread_func2(void *arg)
{
    LOGD("thread2 start");

    for (int i = 0; i < loop_times; ++i) {
        cbox_loop_delegate(loop, say_hello, NULL);
    }

    LOGD("thread2 exit");

    (void)arg;
    return NULL;
}

int main(int argc, char **argv)
{
    CBOX_LOG_INIT(CBOX_LOG_LEVEL_DEBUG, basename(argv[0]));
    loop = cbox_loop_new();
    LOGD("create loop");

    int ret = pthread_create(&t1, NULL, thread_func1, loop);
    assert(ret == 0);
    ret = pthread_create(&t2, NULL, thread_func2, loop);
    assert(ret == 0);

    cbox_loop_dispatch(loop, CBOX_RUN_MODE_FOREVER);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    cbox_loop_delete(loop);
    LOGD("destroy loop, bye~");
    CBOX_LOG_DESTROY();
    // after destroy we should not call LOG's function any more   
    (void)argc;
    (void)ret;
    return 0;
}
