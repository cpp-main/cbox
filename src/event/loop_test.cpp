#include <gtest/gtest.h>
#include "loop.h"

TEST(Loop, NewLoop) {
    cbox_loop_t *loop = cbox_loop_new();
    ASSERT_TRUE(loop != NULL);
    cbox_loop_delete(loop);
}

TEST(Loop, NewFdEvent) {
    int fd = 0;
    uint32_t event_type = CBOX_EVENT_READ;
    void *user = NULL;
    cbox_fd_event_t *event = cbox_fd_event_new(fd, event_type, NULL, user);
    ASSERT_TRUE(event != NULL);
    cbox_fd_event_delete(event);
}

TEST(Loop, NewTimer) {
    int repeat = 1;
    void *user = NULL;
    cbox_timeout_func_t timeout_handler = NULL;
    cbox_basic_timer_t *timer = cbox_basic_timer_new(1000, repeat, timeout_handler, user);
    ASSERT_TRUE(timer != NULL);
    cbox_basic_timer_delete(timer);
}

TEST(Loop, AddFdEvent) {
    cbox_loop_t *loop = cbox_loop_new();
    int fd = 0;
    uint32_t event_type = CBOX_EVENT_READ;
    void *user = NULL;
    cbox_fd_event_t *event = cbox_fd_event_new(fd, event_type, NULL, user);
    ASSERT_TRUE(event != NULL);
    int ret = cbox_loop_fd_event_add(loop, event);
    ASSERT_EQ(ret, 0);
    ret = cbox_loop_fd_event_remove(loop, event);
    ASSERT_EQ(ret, 0);
    cbox_fd_event_delete(event);
    cbox_loop_delete(loop);
}

TEST(Loop, AddTimer) {
    cbox_loop_t *loop = cbox_loop_new();
    int repeat = 1;
    void *user = NULL;
    cbox_timeout_func_t timeout_handler = NULL;
    cbox_basic_timer_t *timer = cbox_basic_timer_new(100, repeat, timeout_handler, user);
    ASSERT_TRUE(timer != NULL);
    int ret = cbox_basic_timer_enable(loop, timer);
    ASSERT_EQ(ret, 0);
    cbox_basic_timer_delete(timer);
    cbox_loop_delete(loop);
}

TEST(Loop, DelFdEvent) {
    cbox_loop_t *loop = cbox_loop_new();
    int fd = 0;
    uint32_t event_type = CBOX_EVENT_READ;
    void *user = NULL;
    cbox_fd_event_t *event = cbox_fd_event_new(fd, event_type, NULL, user);
    ASSERT_TRUE(event != NULL);
    int ret = cbox_loop_fd_event_add(loop, event);
    ASSERT_EQ(ret, 0);
    ret = cbox_loop_fd_event_remove(loop, event);
    ASSERT_EQ(ret, 0);
    cbox_fd_event_delete(event);
    cbox_loop_delete(loop);
}

TEST(Loop, CancelTimer) {
    cbox_loop_t *loop = cbox_loop_new();
    int repeat = 1;
    void *user = NULL;
    cbox_timeout_func_t timeout_handler = NULL;
    cbox_basic_timer_t *timer = cbox_basic_timer_new(1000, repeat, timeout_handler, user);
    ASSERT_TRUE(timer != NULL);
    int ret = cbox_basic_timer_enable(loop, timer);
    ASSERT_EQ(ret, 0);
    ret = cbox_basic_timer_disable(loop, timer);
    ASSERT_EQ(ret, 0);
    cbox_basic_timer_delete(timer);
    cbox_loop_delete(loop);
}

static int g_count = 0;

static void handle_delegate(void *user)
{
    cbox_loop_t *loop = (cbox_loop_t *)user;
    ASSERT_NE(user, nullptr);
    ++g_count;

    if (g_count >= 3)
        cbox_loop_exit(loop);
}

TEST(Loop, Delegate) {
    cbox_loop_t *loop = cbox_loop_new();
    cbox_loop_delegate(loop, handle_delegate, loop);
    cbox_loop_delegate(loop, handle_delegate, loop);
    cbox_loop_delegate(loop, handle_delegate, loop);
    cbox_loop_dispatch(loop, CBOX_LOOP_RUN_MODE_FOREVER);
    ASSERT_EQ(g_count, 3);
    cbox_loop_delete(loop);
}

static int g_loop_once_count = 0;

static void handle_run_once_delegate(void *user)
{
    ASSERT_NE(user, nullptr);
    ++g_loop_once_count;
}
TEST(Loop, RunOnce) {
    cbox_loop_t *loop = cbox_loop_new();
    cbox_loop_delegate(loop, handle_run_once_delegate, loop);
    cbox_loop_dispatch(loop, CBOX_LOOP_RUN_MODE_ONCE);
    ASSERT_EQ(g_loop_once_count, 1);
    cbox_loop_delete(loop);
}
