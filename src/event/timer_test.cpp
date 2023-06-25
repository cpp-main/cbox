#include <gtest/gtest.h>
#include <chrono>
#include "timer.h"
#include <cbox/base/macros.h>

static int gcount = 0;

static void timeout_handler(void *user)
{
    cbox_loop_t *loop = (cbox_loop_t *)user;
    ASSERT_TRUE(loop != NULL);
    ++gcount;

    cbox_loop_exit(loop);
}

TEST(Timer, Once) {
    cbox_loop_t *loop = cbox_loop_new();
    ASSERT_TRUE(loop != NULL);
    int repeat = 1;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000 * 50;
    cbox_timer_t *timer = cbox_timer_new(loop, &ts, repeat, timeout_handler, loop);
    ASSERT_TRUE(timer != NULL);
    cbox_timer_enable(timer);
    cbox_loop_dispatch(loop, CBOX_LOOP_RUN_MODE_FOREVER);

    ASSERT_TRUE(gcount == 1);
    
    cbox_timer_delete(timer);
    cbox_loop_delete(loop);
}


static int g_repeat_count = 0;
static void repeat_timeout_handler(void *user)
{
    cbox_loop_t *loop = (cbox_loop_t *)user;
    ASSERT_TRUE(loop != NULL);
    ++g_repeat_count;
    if (g_repeat_count >= 5)
        cbox_loop_exit(loop);
}

TEST(Timer, Repeat) {
    cbox_loop_t *loop = cbox_loop_new();
    ASSERT_TRUE(loop != NULL);
    int repeat = 5;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 500000;
    cbox_timer_t *timer = cbox_timer_new(loop, &ts, repeat, repeat_timeout_handler, loop);
    ASSERT_TRUE(timer != NULL);
    cbox_timer_enable(timer);
    cbox_loop_dispatch(loop, CBOX_LOOP_RUN_MODE_FOREVER);

    ASSERT_EQ(g_repeat_count, 5);
    
    cbox_timer_delete(timer);
    cbox_loop_delete(loop);
}

static int g_forever_count = 0;
static void forever_timeout_handler(void *user)
{
    cbox_loop_t *loop = (cbox_loop_t *)user;
    ASSERT_TRUE(loop != NULL);
    ++g_forever_count;
    if (g_forever_count >= 50)
        cbox_loop_exit(loop);
}

TEST(Timer, Forever) {
    cbox_loop_t *loop = cbox_loop_new();
    ASSERT_TRUE(loop != NULL);
    int repeat = 0;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000 * 10;
    cbox_timer_t *timer = cbox_timer_new(loop, &ts, repeat, forever_timeout_handler, loop);
    ASSERT_TRUE(timer != NULL);
    cbox_timer_enable(timer);
    cbox_loop_dispatch(loop, CBOX_LOOP_RUN_MODE_FOREVER);

    ASSERT_EQ(g_forever_count, 50);
    
    cbox_timer_delete(timer);
    cbox_loop_delete(loop);
}


static int g_cancel= 0;
static void cancel_timeout_handler(void *user)
{
    cbox_loop_t *loop = (cbox_loop_t *)user;
    ASSERT_TRUE(loop != NULL);
    ++g_cancel;
    cbox_loop_exit(loop);
}

TEST(Timer, Disable) {
    cbox_loop_t *loop = cbox_loop_new();
    int repeat = 10;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000 * 10;
    cbox_timer_t *timer = cbox_timer_new(loop, &ts, repeat, cancel_timeout_handler, loop);
    ASSERT_TRUE(timer != NULL);
    int ret = cbox_timer_enable(timer);
    ASSERT_EQ(ret, 0);
    ret = cbox_timer_disable(timer);
    ASSERT_EQ(ret, 0);
    cbox_loop_dispatch(loop, CBOX_LOOP_RUN_MODE_ONCE);
    ASSERT_EQ(g_cancel, 0);
    cbox_timer_delete(timer);
    cbox_loop_delete(loop);
}

std::chrono::steady_clock::time_point start_ts, stop_ts;

static void nanosec_timeout_handler(void *user)
{
    cbox_loop_t *loop = (cbox_loop_t *)user;
    ASSERT_TRUE(loop != NULL);
    stop_ts = std::chrono::steady_clock::now();
    cbox_loop_exit(loop);
}

TEST(Timer, NanoSeconds)
{
    struct timespec ts_now;

    // Get number of nanoseconds from last second to the present
    ASSERT_EQ(clock_gettime(CLOCK_MONOTONIC, &ts_now), 0) << "Failed to get clock time";

    auto ns = ts_now.tv_nsec;
    auto prev_ns = ns - (ns % 1000000);
    auto min_interval_ns = ns - prev_ns;
    printf("Elapsed nanoseconds since last second: %ld\n", min_interval_ns);

    cbox_loop_t *loop = cbox_loop_new();
    struct timespec ts = {
        .tv_sec = min_interval_ns / 1000000000,
        .tv_nsec = min_interval_ns % 1000000000
    };

    cbox_timer_t *timer = cbox_timer_new(loop, &ts, 0, nanosec_timeout_handler, loop);

    EXPECT_TRUE(cbox_timer_enable(timer) == 0);
    start_ts = std::chrono::steady_clock::now();

    cbox_loop_dispatch(loop, CBOX_LOOP_RUN_MODE_FOREVER);

    uint64_t elapsed_ns = (stop_ts - start_ts).count();
    ASSERT_GE(elapsed_ns, min_interval_ns) << "Timer did not expire after " << min_interval_ns << "nanoseconds" << std::endl;
    ASSERT_LE(elapsed_ns, 2 * min_interval_ns) << "Timer expired too late, elapsed_ns=" << elapsed_ns << std::endl;
    cbox_timer_disable(timer);
    cbox_timer_delete(timer);
    cbox_loop_delete(loop);
}
