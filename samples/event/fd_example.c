#include <libgen.h>
#include <stdio.h>
#include <unistd.h>
#include "cbox/event/loop.h"
#include "cbox/event/fd_event.h"
#include "cbox/base/log.h"

cbox_fd_event_t * read_event = NULL;
cbox_fd_event_t * write_event = NULL;

char send_cache[1024] = { 0 };

static void on_event(int fd, uint32_t events, void *user)
{
    cbox_loop_t *loop = (cbox_loop_t *)user;
    if (events & CBOX_EVENT_READ) {
        char input_buff[200];
        int rsize = read(fd, input_buff, sizeof(input_buff));
        input_buff[rsize - 1] = '\0';

        char ss[400];
        sprintf(ss, "INPUT %d : %s (input 'quit' to exit)\n", rsize, input_buff);
        strcat(send_cache, ss);
        cbox_fd_event_enable(write_event);
        if (strcmp(input_buff, "quit") == 0) {
            cbox_loop_exit(loop);
        }
    }

    if (events & CBOX_EVENT_WRITE) {
        int wsize = write(fd, send_cache, strlen(send_cache));
        if (wsize > 0) {
            memset(send_cache, 0, wsize);
            if (strlen(send_cache) != 0)     //! 如果没有发送完，要继续发
                cbox_fd_event_enable(write_event);
        }
    }

    (void)user;
}

int main(int argc, char **argv)
{
    CBOX_LOG_INIT(CBOX_LOG_LEVEL_DEBUG, basename(argv[0]));
    cbox_loop_t *loop = cbox_loop_new();
    read_event = cbox_fd_event_new(loop, STDIN_FILENO, CBOX_EVENT_READ, on_event, CBOX_RUN_MODE_FOREVER, loop);
    write_event = cbox_fd_event_new(loop, STDOUT_FILENO, CBOX_EVENT_WRITE, on_event, CBOX_RUN_MODE_ONCE, loop);
    cbox_fd_event_enable(read_event);
    (void)write_event;

    LOGM("please input something less than 200 characters:");
    cbox_loop_dispatch(loop, CBOX_RUN_MODE_FOREVER);
    cbox_loop_delete(loop);
    LOGD("destroy loop, bye~");
    CBOX_LOG_DESTROY();
    // after destroy we should not call LOG's function any more   
    (void)argc;
    return 0;
}
